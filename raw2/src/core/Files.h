#pragma once
// Accès fichiers en UTF-8 sur toutes les plateformes (chemins accentués sous Windows compris).
#include <string>
#include <vector>

namespace raw2::files {

bool read(const std::string& utf8Path, std::vector<unsigned char>& out);
bool write(const std::string& utf8Path, const std::vector<unsigned char>& data);
bool writeText(const std::string& utf8Path, const std::string& text);
bool readText(const std::string& utf8Path, std::string& out);
bool makeDirs(const std::string& utf8Path);
bool exists(const std::string& utf8Path);
std::vector<std::string> listFiles(const std::string& dir, const std::string& extension);  // noms complets
std::vector<std::string> listDirs(const std::string& dir);  // sous-dossiers (noms complets)
bool isDir(const std::string& path);
std::string baseName(const std::string& path);  // dernier élément du chemin
bool copyFile(const std::string& from, const std::string& to);
bool copyTree(const std::string& fromDir, const std::string& toDir);  // copie récursive
bool removeFile(const std::string& path);       // suppression définitive d'un fichier
// Journal de RAW 2 (Documents/Raw 2/RAW2-journal.txt) : une ligne datée par évènement (version,
// hôte, presets chargés, erreurs). Jamais depuis le fil audio. Le fichier reste sous 512 Ko.
void logLine(const std::string& msg);
bool moveToTrash(const std::string& path);      // corbeille (Windows, macOS), suppression ailleurs
bool openFolder(const std::string& dir);        // ouvre l'Explorateur / le Finder sur un dossier
std::string documentsDir();      // Documents de l'utilisateur
std::string raw2Dir();           // Documents/Raw 2 : packs de presets
std::string myPresetsDir();      // Documents/Raw 2/Mes presets
std::string wavetableDir();      // Documents/Raw 2/Wavetables
std::string exportsDir();        // Documents/Raw 2/Exports (sons glissés vers la DAW)
std::string legacyPresetDir();   // Documents/RAW2 banque presets (versions 1 et 2)
std::string settingsPath();      // réglages globaux (thème, zoom par défaut)
std::string join(const std::string& a, const std::string& b);

#ifdef _WIN32
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);
#endif

} // namespace raw2::files
