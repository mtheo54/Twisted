// Twisted - identité machine + dossier de données sous Windows.
#if defined(_WIN32)
#include <windows.h>
#include <string>
#include "store.h"

namespace tw::license {

std::string platformMachineRaw() {
    // MachineGuid : identifiant d'installation stable, déjà opaque (haché ensuite).
    HKEY k;
    std::string id;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        char buf[256]; DWORD n = sizeof(buf), type = 0;
        if (RegQueryValueExA(k, "MachineGuid", nullptr, &type, (LPBYTE)buf, &n) == ERROR_SUCCESS && type == REG_SZ)
            id.assign(buf, n > 0 ? n - 1 : 0);
        RegCloseKey(k);
    }
    if (id.empty()) {
        char name[256]; DWORD n = sizeof(name);
        if (GetComputerNameA(name, &n)) id.assign(name, n);
    }
    return id.empty() ? std::string("raw-unknown-win") : id;
}

std::string platformDeviceName() {
    char name[256]; DWORD n = sizeof(name);
    return GetComputerNameA(name, &n) ? std::string(name, n) : std::string();
}

std::string platformDataDir() {
    char base[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    std::string dir = (n > 0 && n < MAX_PATH) ? std::string(base) + "\\RAW" : "C:\\RAW";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

} // namespace tw::license
#endif
