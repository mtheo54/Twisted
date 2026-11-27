#pragma once
// Décodage d'images et de vidéos pour les fonds personnalisés, avec les outils du système :
// ImageIO et AVFoundation (macOS), WIC et Media Foundation (Windows), PNG par Cairo (Linux, tests).
// Les images sont réduites pour tenir dans maxW x maxH ; une vidéo ou un GIF animé donne plusieurs
// images (au plus maxFrames, une toutes les 1/fps s). Pixels BGRA, alpha prémultiplié.
#include <string>
#include <vector>

namespace raw2::media {

struct Frames {
    int w = 0, h = 0;
    std::vector<std::vector<unsigned char>> px;  // une entrée par image, w * h * 4 octets
    std::vector<float> dur;                      // durée de chaque image (s)
    bool video = false;
    // version floutée de chaque image (effet verre), petite : remplie par ui::Backdrop
    int bw = 0, bh = 0;
    std::vector<std::vector<unsigned char>> blur;
};

// Vrai si l'extension est une image ou une vidéo que RAW 2 sait lire en fond.
bool isMediaFile(const std::string& path);
bool isVideoFile(const std::string& path);
// Décodage complet (à appeler hors du fil de l'interface : une vidéo peut prendre une seconde).
bool decode(const std::string& path, int maxW, int maxH, int maxFrames, float fps, Frames& out, std::string& error);

} // namespace raw2::media
