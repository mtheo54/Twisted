#pragma once
// Skins (.raw2skin) : habillage complet de l'interface, créé avec ABRASION SKIN STUDIO.
//
// Un skin est un dossier (ou une archive .raw2skin de ce dossier, sans compression) qui contient
// skin.json et, au choix, une image ou une vidéo de fond. skin.json :
//   {
//     "format": "raw2skin", "version": 1,
//     "name": "Néon glacé", "author": "...",
//     "base": "nothing",                       // thème d'usine de départ (clé de themes.json)
//     "colors": { "acc": "#00C8FF", "card": "#14141880", ... },   // jetons de couleur du thème
//     "background": { "file": "fond.mp4", "voile": 0.35, "grille": true },
//     "glass": { "on": true, "flou": 0.6, "teinte": 0.35, "reflet": 0.6 },
//     "cards": { "opacite": 0.85 },
//     "layout": { "son.oscA": [24, 73, 0.8], ... }  // coin haut gauche et échelle (1 si absente) de chaque bloc placé
//   }
// Installé dans Documents/Raw 2/Skins/<nom> ; les skins d'usine sont dans le dossier d'usine.
#include "core/Color.h"
#include "core/FactoryData.h"

#include <string>
#include <utility>
#include <vector>

namespace raw2 {

struct Skin {
    std::string name, author;
    std::string dir;                  // dossier installé (vide pour un skin pas encore installé)
    std::string base = "alu";
    std::vector<std::pair<std::string, Color>> colors;
    std::string background;           // fichier du fond dans dir ("" : aucun)
    float voile = 0.35f;              // couleur du thème posée sur le fond (0..0,9)
    bool grid = true;                 // grille de points du châssis
    bool glass = false;
    float blur = 0.6f, tint = 0.35f, edge = 0.6f;  // flou (0..1), teinte des cartes, reflets
    float cards = 0.85f;              // opacité des cartes sans verre (0,5..1)
    struct Place {
        float x = 0.f, y = 0.f, scale = 1.f;
    };
    std::vector<std::pair<std::string, Place>> layout;
};

bool parseColor(const std::string& s, Color& out);   // #RGB, #RRGGBB, #RRGGBBAA, rgb(), rgba()
std::string colorHex(const Color& c);                // #RRGGBB ou #RRGGBBAA
const Theme* themeByKey(const std::string& key);     // nullptr si inconnu
// Change un jeton de couleur du thème par son nom (celui de themes.json) ; faux si inconnu.
bool setThemeToken(Theme& t, const std::string& name, Color c);
std::vector<std::string> themeTokenNames();

bool parseSkin(const std::string& json, Skin& out, std::string& error);
std::string skinToJson(const Skin& s);
bool loadSkin(const std::string& dir, Skin& out, std::string& error);  // dir/skin.json
// Installe un .raw2skin, un dossier de skin ou un skin.json dans Documents/Raw 2/Skins ; dir reçoit
// le dossier installé (un skin du même nom est remplacé).
bool installSkin(const std::string& path, std::string& dir, std::string& error);
bool isSkinFile(const std::string& path);  // .raw2skin, .zip, skin.json, ou dossier contenant skin.json
// Skins disponibles : ceux de l'utilisateur puis ceux d'usine (dossiers), triés par nom.
std::vector<std::string> listSkins();

} // namespace raw2
