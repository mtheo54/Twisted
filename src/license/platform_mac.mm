// Twisted - plateforme macOS : réseau (NSURLSession) + identité machine + dossier de données.
// Compilé avec ARC. Aucune bibliothèque tierce.
#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <memory>
#include <string>
#include "manager.h"
#include "store.h"

namespace tw::license {

// ---- identité machine ----
std::string platformMachineRaw() {
    // Identifiant matériel stable, déjà opaque ; on le hache ensuite de toute façon.
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                   IOServiceMatching("IOPlatformExpertDevice"));
    std::string id;
    if (svc) {
        CFStringRef uuid = (CFStringRef)IORegistryEntryCreateCFProperty(
            svc, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
        if (uuid) {
            char buf[128];
            if (CFStringGetCString(uuid, buf, sizeof(buf), kCFStringEncodingUTF8)) id = buf;
            CFRelease(uuid);
        }
        IOObjectRelease(svc);
    }
    if (id.empty()) {
        NSString* name = [[NSHost currentHost] localizedName];
        if (name) id = [name UTF8String];
    }
    return id.empty() ? std::string("raw-unknown-mac") : id;
}

std::string platformDeviceName() {
    NSString* name = [[NSHost currentHost] localizedName];
    return name ? std::string([name UTF8String]) : std::string();
}

std::string platformDataDir() {
    NSArray* dirs = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (dirs.count == 0) return "/tmp/Twisted";
    NSString* dir = [dirs[0] stringByAppendingPathComponent:@"Twisted"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return std::string([dir UTF8String]);
}

// ---- réseau ----
namespace {
class MacNet final : public Net {
public:
    NetResult post(const std::string& path, const std::string& body, const std::string& bearer) override {
        NetResult res;
        res.networkError = true;
        if (!urlAllowed(TWISTED_ABRASION_URL)) return res;

        @autoreleasepool {
            NSString* urlStr = [NSString stringWithUTF8String:(std::string(TWISTED_ABRASION_URL) + path).c_str()];
            NSURL* url = [NSURL URLWithString:urlStr];
            if (!url) return res;

            NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url];
            req.HTTPMethod = @"POST";
            req.timeoutInterval = 15;
            [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
            [req setValue:@"application/json" forHTTPHeaderField:@"Accept"];
            [req setValue:@"RAW/1.0 (macOS)" forHTTPHeaderField:@"User-Agent"];
            if (!bearer.empty())
                [req setValue:[NSString stringWithFormat:@"Bearer %s", bearer.c_str()] forHTTPHeaderField:@"Authorization"];
            req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];

            // NSURLSession utilise par défaut le magasin de certificats système et TLS 1.2+.
            NSURLSessionConfiguration* cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
            cfg.TLSMinimumSupportedProtocolVersion = tls_protocol_version_TLSv12;
            NSURLSession* session = [NSURLSession sessionWithConfiguration:cfg];

            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block NetResult local;
            local.networkError = true;
            NSURLSessionDataTask* task = [session dataTaskWithRequest:req
                completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                    if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
                        local.networkError = false;
                        local.status = int(((NSHTTPURLResponse*)response).statusCode);
                        if (data.length) local.body.assign((const char*)data.bytes, data.length);
                    }
                    dispatch_semaphore_signal(sem);
                }];
            [task resume];
            // On est déjà sur le thread réseau du contrôleur : bloquer ici ne gèle pas l'UI.
            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)20 * NSEC_PER_SEC));
            [session finishTasksAndInvalidate];
            res = local;
        }
        return res;
    }
};
} // namespace

std::unique_ptr<Net> makePlatformNet() { return std::make_unique<MacNet>(); }

} // namespace tw::license
#endif
