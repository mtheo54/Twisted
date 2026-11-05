#include "core/Files.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <filesystem>
#endif
#ifdef __APPLE__
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace raw2::files {

#ifdef __APPLE__
bool macMoveToTrash(const std::string& path);  // MacFiles.mm (NSFileManager)
#endif

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static FILE* openFile(const std::string& p, const char* mode) {
    return _wfopen(widen(p).c_str(), widen(mode).c_str());
}
#else
static FILE* openFile(const std::string& p, const char* mode) { return std::fopen(p.c_str(), mode); }
#endif

bool read(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = openFile(path, "rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        out.insert(out.end(), buf, buf + n);
        if (out.size() > (size_t)256 * 1024 * 1024) break;  // garde-fou : 256 Mo
    }
    std::fclose(f);
    return true;
}

// Écriture sûre : le contenu va d'abord dans « fichier.tmp~ », puis remplace l'ancien fichier
// d'un coup (renommage). Disque plein, coupure, plantage pendant l'écriture : l'ancien preset ou
// pack reste intact au lieu d'être tronqué.
bool write(const std::string& path, const std::vector<unsigned char>& data) {
    const std::string tmp = path + ".tmp~";
    FILE* f = openFile(tmp, "wb");
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fflush(f) == 0 && ok;
    ok = std::fclose(f) == 0 && ok;
#ifdef _WIN32
    ok = ok && MoveFileExW(widen(tmp).c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(widen(tmp).c_str());
#else
    ok = ok && std::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) std::remove(tmp.c_str());
#endif
    return ok;
}

bool writeText(const std::string& path, const std::string& text) {
    return write(path, std::vector<unsigned char>(text.begin(), text.end()));
}

bool readText(const std::string& path, std::string& out) {
    std::vector<unsigned char> b;
    if (!read(path, b)) return false;
    out.assign(b.begin(), b.end());
    return true;
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    return (a.back() == '/' || a.back() == '\\') ? a + b : a + sep + b;
}

void logLine(const std::string& msg) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    const std::string dir = raw2Dir();
    makeDirs(dir);
    const std::string path = join(dir, "RAW2-journal.txt");
    // au-delà de 512 Ko : on garde la seconde moitié
    std::vector<unsigned char> old;
    if (read(path, old) && old.size() > 512 * 1024) {
        size_t cut = old.size() / 2;
        while (cut < old.size() && old[cut] != '\n') ++cut;
        std::vector<unsigned char> keep(old.begin() + (long)std::min(old.size(), cut + 1), old.end());
        write(path, keep);
    }
    const std::time_t t = std::time(nullptr);
    char stamp[32] = "";
    if (const std::tm* tm = std::localtime(&t)) std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tm);
    if (FILE* f = openFile(path, "ab")) {
        std::fprintf(f, "%s  %s\n", stamp, msg.c_str());
        std::fclose(f);
    }
}

bool exists(const std::string& path) {
#ifdef _WIN32
    return GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool makeDirs(const std::string& path) {
    if (path.empty() || exists(path)) return true;
    const size_t s = path.find_last_of("/\\");
    if (s != std::string::npos && s > 0) makeDirs(path.substr(0, s));
#ifdef _WIN32
    return CreateDirectoryW(widen(path).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

std::vector<std::string> listFiles(const std::string& dir, const std::string& ext) {
    std::vector<std::string> out;
    auto hasExt = [&](const std::string& n) {
        if (n.size() <= ext.size()) return false;
        std::string e = n.substr(n.size() - ext.size());
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return e == ext;
    };
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(join(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::string n = narrow(fd.cFileName);
        if (hasExt(n)) out.push_back(join(dir, n));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != ".." && hasExt(n)) out.push_back(join(dir, n));
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

std::string documentsDir() {
#ifdef _WIN32
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p)) && p) r = narrow(p);
    if (p) CoTaskMemFree(p);
    return r;
#else
    const char* h = std::getenv("HOME");
    return h ? join(h, "Documents") : std::string(".");
#endif
}

std::string raw2Dir() { return join(documentsDir(), "Raw 2"); }
std::string myPresetsDir() { return join(raw2Dir(), "Mes presets"); }
std::string wavetableDir() { return join(raw2Dir(), "Wavetables"); }
std::string exportsDir() { return join(raw2Dir(), "Exports"); }
std::string legacyPresetDir() { return join(documentsDir(), "RAW2 banque presets"); }

std::vector<std::string> listDirs(const std::string& dir) {
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(join(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::string n = narrow(fd.cFileName);
        if (n != "." && n != "..") out.push_back(join(dir, n));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        if (isDir(join(dir, n))) out.push_back(join(dir, n));
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    return out;
}

bool isDir(const std::string& path) {
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::string baseName(const std::string& path) {
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

bool copyFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return CopyFileW(widen(from).c_str(), widen(to).c_str(), FALSE) != 0;
#else
    std::vector<unsigned char> b;
    return read(from, b) && write(to, b);
#endif
}

bool copyTree(const std::string& fromDir, const std::string& toDir) {
    if (!makeDirs(toDir)) return false;
    bool ok = true;
    for (const std::string& f : listFiles(fromDir, "")) ok = copyFile(f, join(toDir, baseName(f))) && ok;
    for (const std::string& d : listDirs(fromDir)) ok = copyTree(d, join(toDir, baseName(d))) && ok;
    return ok;
}

bool removeFile(const std::string& path) {
#ifdef _WIN32
    return DeleteFileW(widen(path).c_str()) != 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

bool moveToTrash(const std::string& path) {
    if (!exists(path)) return false;
#ifdef _WIN32
    std::wstring w = widen(path);
    w.push_back(L'\0');  // liste terminée par deux zéros
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = w.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0;
#elif defined(__APPLE__)
    return macMoveToTrash(path);  // corbeille du Finder (récupérable)
#else
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
#endif
}

bool openFolder(const std::string& dir) {
#ifdef _WIN32
    return (INT_PTR)ShellExecuteW(nullptr, L"open", widen(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#elif defined(__APPLE__)
    // Finder : /usr/bin/open <dossier>
    pid_t pid = 0;
    char* argv[] = {(char*)"/usr/bin/open", (char*)dir.c_str(), nullptr};
    if (posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv, environ) != 0) return false;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    (void)dir;
    return false;
#endif
}

std::string settingsPath() {
#ifdef _WIN32
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)) && p) r = narrow(p);
    if (p) CoTaskMemFree(p);
    return join(join(r, "ABRASION"), "RAW2.json");
#elif defined(__APPLE__)
    const char* h = std::getenv("HOME");  // ~/Library/Application Support/ABRASION/RAW2.json
    return join(join(join(join(h ? h : ".", "Library"), "Application Support"), "ABRASION"), "RAW2.json");
#else
    const char* h = std::getenv("HOME");
    return join(join(h ? h : ".", ".config"), "raw2.json");
#endif
}

} // namespace raw2::files
