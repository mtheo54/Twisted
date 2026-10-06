// Twisted - client réseau Windows (WinHTTP). Aucune bibliothèque tierce.
// HTTPS via le magasin de certificats du système, TLS 1.2+, User-Agent normal.
#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#include <memory>
#include <string>
#include "manager.h"
#pragma comment(lib, "winhttp.lib")

namespace tw::license {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Découpe TWISTED_ABRASION_URL en {https?, hôte, port, chemin de base}.
struct Url { bool https; std::wstring host; int port; std::string base; bool ok; };

Url parseBase() {
    std::string u = TWISTED_ABRASION_URL;
    Url r{true, {}, 443, "", false};
    size_t p;
    if (u.rfind("https://", 0) == 0) { r.https = true; r.port = 443; p = 8; }
    else if (u.rfind("http://", 0) == 0) { r.https = false; r.port = 80; p = 7; }
    else return r;
    std::string rest = u.substr(p);
    size_t slash = rest.find('/');
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    r.base = slash == std::string::npos ? "" : rest.substr(slash);
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        r.host = widen(hostport.substr(0, colon));
        r.port = std::atoi(hostport.c_str() + colon + 1);
    } else {
        r.host = widen(hostport);
    }
    r.ok = true;
    return r;
}

class WinNet final : public Net {
public:
    NetResult post(const std::string& path, const std::string& body, const std::string& bearer) override {
        NetResult res;
        res.networkError = true;

        // Garde-fou : jamais d'appel si l'adresse de base n'est pas autorisée.
        if (!urlAllowed(TWISTED_ABRASION_URL)) return res;

        static const Url url = parseBase();
        if (!url.ok || url.host.empty()) return res;

        HINTERNET sess = WinHttpOpen(L"RAW/1.0 (Windows)",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!sess) return res;

        // TLS 1.2 minimum (Cloudflare). On laisse le magasin système valider le certificat.
        DWORD secure = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(sess, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure, sizeof(secure));
        // Délais raisonnables : jamais bloquer longtemps (le thread réseau ne gèle pas l'UI).
        WinHttpSetTimeouts(sess, 6000, 6000, 8000, 10000); // résolution, connexion, envoi, réception (ms)

        HINTERNET conn = WinHttpConnect(sess, url.host.c_str(), INTERNET_PORT(url.port), 0);
        if (!conn) { WinHttpCloseHandle(sess); return res; }

        std::wstring full = widen(url.base + path);
        HINTERNET req = WinHttpOpenRequest(conn, L"POST", full.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           url.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(sess); return res; }

        // En-têtes exactement comme le serveur les attend.
        std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
        if (!bearer.empty()) headers += L"Authorization: Bearer " + widen(bearer) + L"\r\n";

        BOOL sent = WinHttpSendRequest(req, headers.c_str(), DWORD(-1),
                                       (LPVOID)body.data(), DWORD(body.size()),
                                       DWORD(body.size()), 0);
        if (sent) sent = WinHttpReceiveResponse(req, nullptr);

        if (sent) {
            DWORD code = 0, len = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
            res.status = int(code);
            res.networkError = false;

            std::string out;
            DWORD avail = 0;
            do {
                avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) break;
                if (!avail) break;
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, chunk.data(), avail, &read)) break;
                out.append(chunk.data(), read);
            } while (avail > 0);
            res.body = out;
        }

        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(sess);
        return res;
    }
};

} // namespace

std::unique_ptr<Net> makePlatformNet() { return std::make_unique<WinNet>(); }

} // namespace tw::license
#endif
