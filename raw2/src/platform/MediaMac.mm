// Fonds personnalisés sous macOS : images et GIF animés par ImageIO, vidéos par AVFoundation.
// Compilé avec ARC. Tout se fait dans le fil de décodage (jamais dans le fil audio).
#include "platform/Media.h"
#include "platform/MediaScale.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>

namespace raw2::media {

// dessine une CGImage réduite dans un tampon BGRA prémultiplié (lignes du haut en premier)
static bool drawScaled(CGImageRef im, int tw, int th, std::vector<unsigned char>& px) {
    px.assign((size_t)tw * th * 4, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px.data(), (size_t)tw, (size_t)th, 8, (size_t)tw * 4, cs,
                                             kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(cs);
    if (!ctx) return false;
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, tw, th), im);
    CGContextRelease(ctx);
    return true;
}

static float gifDelay(CGImageSourceRef src, size_t i) {
    float d = 0.1f;
    CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex(src, i, nullptr);
    if (props) {
        CFDictionaryRef gif = (CFDictionaryRef)CFDictionaryGetValue(props, kCGImagePropertyGIFDictionary);
        if (gif) {
            CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(gif, kCGImagePropertyGIFUnclampedDelayTime);
            if (!n) n = (CFNumberRef)CFDictionaryGetValue(gif, kCGImagePropertyGIFDelayTime);
            double v = 0;
            if (n && CFNumberGetValue(n, kCFNumberDoubleType, &v) && v > 0.01) d = (float)v;
        }
        CFRelease(props);
    }
    return d;
}

static bool decodeImage(NSURL* url, int maxW, int maxH, int maxFrames, Frames& out, std::string& error) {
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!src) { error = "image illisible"; return false; }
    const size_t n = std::min<size_t>(CGImageSourceGetCount(src), (size_t)std::max(1, maxFrames));
    for (size_t i = 0; i < n; ++i) {
        CGImageRef im = CGImageSourceCreateImageAtIndex(src, i, nullptr);
        if (!im) continue;
        if (out.w == 0) {
            // plusieurs images (GIF animé) : plus petites, pour rester léger en mémoire
            const int mw = n > 1 ? std::min(maxW, 640) : maxW, mh = n > 1 ? std::min(maxH, 480) : maxH;
            fitSize((int)CGImageGetWidth(im), (int)CGImageGetHeight(im), mw, mh, out.w, out.h);
        }
        std::vector<unsigned char> px;
        if (drawScaled(im, out.w, out.h, px)) {
            out.px.push_back(std::move(px));
            out.dur.push_back(n > 1 ? gifDelay(src, i) : 1.f);
        }
        CGImageRelease(im);
    }
    CFRelease(src);
    if (out.px.empty()) { error = "image illisible"; return false; }
    return true;
}

static bool decodeVideo(NSURL* url, int maxW, int maxH, int maxFrames, float fps, Frames& out, std::string& error) {
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
    AVAssetTrack* track = tracks.firstObject;
    if (!track) { error = "vidéo illisible (pas de piste image)"; return false; }
    NSError* err = nil;
    AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
    if (!reader) { error = "vidéo illisible"; return false; }
    NSDictionary* settings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:settings];
    output.alwaysCopiesSampleData = NO;
    if (![reader canAddOutput:output]) { error = "vidéo illisible"; return false; }
    [reader addOutput:output];
    if (![reader startReading]) { error = "vidéo illisible"; return false; }
    const double step = 1.0 / std::max(1.f, fps);
    double next = 0.0;
    while ((int)out.px.size() < maxFrames) {
        CMSampleBufferRef sb = [output copyNextSampleBuffer];
        if (!sb) break;
        const double t = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
        CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sb);
        if (pb && (!std::isfinite(t) || t + 1e-3 >= next)) {
            CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            const int w = (int)CVPixelBufferGetWidth(pb), h = (int)CVPixelBufferGetHeight(pb);
            const int stride = (int)CVPixelBufferGetBytesPerRow(pb);
            const unsigned char* base = (const unsigned char*)CVPixelBufferGetBaseAddress(pb);
            if (out.w == 0) fitSize(w, h, maxW, maxH, out.w, out.h);
            if (base && w > 0 && h > 0) {
                std::vector<unsigned char> px((size_t)out.w * out.h * 4);
                scaleBGRA(base, w, h, stride, px.data(), out.w, out.h);
                for (size_t k = 3; k < px.size(); k += 4) px[k] = 255;  // image opaque
                out.px.push_back(std::move(px));
                out.dur.push_back((float)step);
            }
            CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            next += step;
        }
        CFRelease(sb);
    }
    [reader cancelReading];
    if (out.px.empty()) { error = "vidéo illisible"; return false; }
    out.video = true;
    return true;
}

bool decode(const std::string& path, int maxW, int maxH, int maxFrames, float fps, Frames& out, std::string& error) {
    out = Frames{};
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        if (!url) { error = "fichier introuvable"; return false; }
        return isVideoFile(path) ? decodeVideo(url, maxW, maxH, maxFrames, fps, out, error)
                                 : decodeImage(url, maxW, maxH, maxFrames, out, error);
    }
}

} // namespace raw2::media
