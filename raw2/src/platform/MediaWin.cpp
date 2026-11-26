// Fonds personnalisés sous Windows : images et GIF animés par WIC, vidéos par Media Foundation.
// Appelé dans le fil de décodage du fond (COM initialisé ici, jamais dans le fil audio).
#include "platform/Media.h"
#include "platform/MediaScale.h"

#include <windows.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propidl.h>

#include <algorithm>
#include <cstring>

namespace raw2::media {

namespace {

template <class T>
void rel(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

struct ComScope {
    bool ok;
    ComScope() : ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
    ~ComScope() { if (ok) CoUninitialize(); }
};

// valeur entière d'une métadonnée WIC (GIF : position, taille, délai, disposition)
int meta(IWICMetadataQueryReader* q, const wchar_t* name, int def) {
    if (!q) return def;
    PROPVARIANT v;
    PropVariantInit(&v);
    int r = def;
    if (SUCCEEDED(q->GetMetadataByName(name, &v))) {
        if (v.vt == VT_UI2) r = v.uiVal;
        else if (v.vt == VT_UI1) r = v.bVal;
        else if (v.vt == VT_UI4) r = (int)v.ulVal;
    }
    PropVariantClear(&v);
    return r;
}

// frame WIC convertie en BGRA prémultiplié, taille d'origine
bool frameBGRA(IWICImagingFactory* f, IWICBitmapSource* src, UINT w, UINT h, std::vector<unsigned char>& px) {
    IWICFormatConverter* cv = nullptr;
    bool ok = SUCCEEDED(f->CreateFormatConverter(&cv)) &&
              SUCCEEDED(cv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    if (ok) {
        px.assign((size_t)w * h * 4, 0);
        ok = SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()));
    }
    rel(cv);
    return ok;
}

bool decodeImage(const std::wstring& path, int maxW, int maxH, int maxFrames, Frames& out, std::string& error) {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) {
        error = "décodeur d'images indisponible";
        return false;
    }
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
        rel(f);
        error = "image illisible";
        return false;
    }
    UINT n = 0;
    dec->GetFrameCount(&n);
    n = std::min<UINT>(n, (UINT)std::max(1, maxFrames));
    // GIF animé : images partielles posées sur un écran logique (position, disposition)
    IWICMetadataQueryReader* dq = nullptr;
    dec->GetMetadataQueryReader(&dq);
    int sw = 0, sh = 0;
    if (n > 1) {
        sw = meta(dq, L"/logscrdesc/Width", 0);
        sh = meta(dq, L"/logscrdesc/Height", 0);
    }
    std::vector<unsigned char> canvas;
    for (UINT i = 0; i < n; ++i) {
        IWICBitmapFrameDecode* fr = nullptr;
        if (FAILED(dec->GetFrame(i, &fr))) continue;
        UINT w = 0, h = 0;
        fr->GetSize(&w, &h);
        std::vector<unsigned char> px;
        if (w && h && frameBGRA(f, fr, w, h, px)) {
            IWICMetadataQueryReader* q = nullptr;
            fr->GetMetadataQueryReader(&q);
            if (n > 1 && sw > 0 && sh > 0) {
                const int left = meta(q, L"/imgdesc/Left", 0), top = meta(q, L"/imgdesc/Top", 0);
                const int disp = meta(q, L"/grctlext/Disposal", 0);
                const float delay = (float)std::max(2, meta(q, L"/grctlext/Delay", 10)) / 100.f;
                if (canvas.empty()) canvas.assign((size_t)sw * sh * 4, 0);
                std::vector<unsigned char> before;
                if (disp == 3) before = canvas;  // « restaurer l'image précédente »
                for (UINT y = 0; y < h; ++y) {
                    const int cy = top + (int)y;
                    if (cy < 0 || cy >= sh) continue;
                    for (UINT x = 0; x < w; ++x) {
                        const int cx = left + (int)x;
                        if (cx < 0 || cx >= sw) continue;
                        const unsigned char* s = px.data() + ((size_t)y * w + x) * 4;
                        unsigned char* d = canvas.data() + ((size_t)cy * sw + cx) * 4;
                        const unsigned a = s[3];
                        for (int c = 0; c < 4; ++c) d[c] = (unsigned char)(s[c] + d[c] * (255 - a) / 255);
                    }
                }
                if (out.w == 0) fitSize(sw, sh, std::min(maxW, 640), std::min(maxH, 480), out.w, out.h);
                std::vector<unsigned char> small((size_t)out.w * out.h * 4);
                scaleBGRA(canvas.data(), sw, sh, sw * 4, small.data(), out.w, out.h);
                out.px.push_back(std::move(small));
                out.dur.push_back(delay);
                if (disp == 2) {  // effacer la zone de cette image
                    for (UINT y = 0; y < h; ++y)
                        for (UINT x = 0; x < w; ++x) {
                            const int cx = left + (int)x, cy = top + (int)y;
                            if (cx >= 0 && cx < sw && cy >= 0 && cy < sh) std::memset(canvas.data() + ((size_t)cy * sw + cx) * 4, 0, 4);
                        }
                } else if (disp == 3) {
                    canvas = std::move(before);
                }
            } else {
                if (out.w == 0) fitSize((int)w, (int)h, maxW, maxH, out.w, out.h);
                std::vector<unsigned char> small((size_t)out.w * out.h * 4);
                scaleBGRA(px.data(), (int)w, (int)h, (int)w * 4, small.data(), out.w, out.h);
                out.px.push_back(std::move(small));
                out.dur.push_back(1.f);
            }
            rel(q);
        }
        rel(fr);
    }
    rel(dq);
    rel(dec);
    rel(f);
    if (out.px.empty()) { error = "image illisible"; return false; }
    return true;
}

bool decodeVideo(const std::wstring& path, int maxW, int maxH, int maxFrames, float fps, Frames& out, std::string& error) {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { error = "Media Foundation indisponible"; return false; }
    IMFAttributes* attr = nullptr;
    IMFSourceReader* rd = nullptr;
    IMFMediaType* mt = nullptr;
    IMFMediaType* cur = nullptr;
    bool ok = SUCCEEDED(MFCreateAttributes(&attr, 1)) && SUCCEEDED(attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)) &&
              SUCCEEDED(MFCreateSourceReaderFromURL(path.c_str(), attr, &rd));
    if (ok) {
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        ok = SUCCEEDED(MFCreateMediaType(&mt)) && SUCCEEDED(mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
             SUCCEEDED(mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) &&
             SUCCEEDED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt)) &&
             SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur));
    }
    UINT32 w = 0, h = 0;
    if (ok) ok = SUCCEEDED(MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0;
    if (ok) {
        fitSize((int)w, (int)h, maxW, maxH, out.w, out.h);
        const double step = 1.0 / std::max(1.f, fps);
        double next = 0.0;
        std::vector<unsigned char> full((size_t)w * h * 4);
        while ((int)out.px.size() < maxFrames) {
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* s = nullptr;
            if (FAILED(rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &s))) break;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { rel(s); break; }
            if (!s) continue;
            const double t = (double)ts / 1e7;
            if (t + 1e-3 >= next) {
                IMFMediaBuffer* b = nullptr;
                if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
                    IMF2DBuffer* b2 = nullptr;
                    BYTE* scan0 = nullptr;
                    LONG pitch = 0;
                    BYTE* p = nullptr;
                    DWORD len = 0;
                    bool locked2d = SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&b2))) && SUCCEEDED(b2->Lock2D(&scan0, &pitch));
                    if (!locked2d && SUCCEEDED(b->Lock(&p, nullptr, &len))) { scan0 = p; pitch = (LONG)w * 4; }
                    if (scan0 && (locked2d || len >= (DWORD)w * h * 4)) {
                        for (UINT32 y = 0; y < h; ++y) std::memcpy(full.data() + (size_t)y * w * 4, scan0 + (LONG)y * pitch, (size_t)w * 4);
                        std::vector<unsigned char> small((size_t)out.w * out.h * 4);
                        scaleBGRA(full.data(), (int)w, (int)h, (int)w * 4, small.data(), out.w, out.h);
                        for (size_t k = 3; k < small.size(); k += 4) small[k] = 255;  // RGB32 : image opaque
                        out.px.push_back(std::move(small));
                        out.dur.push_back((float)step);
                        next += step;
                    }
                    if (locked2d) b2->Unlock2D();
                    else if (p) b->Unlock();
                    rel(b2);
                    rel(b);
                }
            }
            rel(s);
        }
    }
    rel(cur);
    rel(mt);
    rel(rd);
    rel(attr);
    MFShutdown();
    if (out.px.empty()) { error = "vidéo illisible"; return false; }
    out.video = true;
    return true;
}

} // namespace

bool decode(const std::string& path, int maxW, int maxH, int maxFrames, float fps, Frames& out, std::string& error) {
    out = Frames{};
    ComScope com;
    const std::wstring w = wide(path);
    return isVideoFile(path) ? decodeVideo(w, maxW, maxH, maxFrames, fps, out, error) : decodeImage(w, maxW, maxH, maxFrames, out, error);
}

} // namespace raw2::media
