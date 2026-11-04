// Fichier généré par tools/gen_data.py à partir de resources/data/*.json : ne pas éditer à la main.
#include "core/FactoryData.h"

namespace raw2 {

namespace {
constexpr Color C(int r, int g, int b, float a = 1.f) { return Color{r / 255.f, g / 255.f, b / 255.f, a}; }
}

const Theme kThemes[kNumThemes] = {
  { // alu
    "alu", "ALU", "Thème ALU",
    C(220,220,214), C(13,13,13,0.1f), C(244,244,240), C(26,26,26), C(13,13,13), C(13,13,13), C(122,122,117), C(13,13,13), C(13,13,13), C(38,38,38), C(58,58,58), C(255,255,255), C(122,122,117), C(58,58,58), C(255,77,0), C(13,13,13), C(255,77,0), C(255,77,0), C(255,77,0), C(13,13,13), C(255,255,255), C(13,13,13), C(244,244,240), C(13,13,13), C(255,255,255), C(255,77,0), C(244,244,240), C(122,122,117), C(255,77,0), C(122,122,117), C(244,244,240), C(13,13,13), C(236,236,231), C(204,204,204), C(204,204,204), C(236,236,231), C(255,255,255), C(13,13,13), C(13,13,13), C(122,122,117), C(220,220,214), C(255,255,255), C(204,204,204),
    Color{}, Color{}, Color{},
    false, false, false,
    {255,77,0}, {13,13,13}, {0,0,0}, false
  },
  { // nothing
    "nothing", "NOTHING", "Thème NOTHING",
    C(17,17,19), C(31,31,34), C(26,26,29), C(51,51,56), C(51,51,56), C(240,240,240), C(122,122,128), C(7,7,8), C(42,42,46), C(31,31,34), C(42,42,46), C(240,240,240), C(122,122,128), C(42,42,46), C(215,25,33), C(255,255,255), C(215,25,33), C(215,25,33), C(215,25,33), C(240,240,240), C(17,17,19), C(240,240,240), C(26,26,29), C(122,122,128), C(255,255,255), C(215,25,33), C(240,240,240), C(122,122,128), C(215,25,33), C(74,74,80), C(34,34,38), C(240,240,240), C(20,20,22), C(42,42,46), C(51,51,56), C(20,20,22), C(240,240,240), C(240,240,240), C(74,74,80), C(122,122,128), C(17,17,19), C(12,12,14), C(42,42,46),
    Color{}, Color{}, Color{},
    false, false, false,
    {215,25,33}, {240,240,240}, {0,0,0}, false
  },
  { // titane
    "titane", "TITANE", "Thème TITANE",
    C(138,141,143), C(13,13,13,0.16f), C(166,169,171), C(13,13,13), C(13,13,13), C(13,13,13), C(58,60,62), C(13,13,13), C(13,13,13), C(38,38,38), C(58,58,58), C(244,244,240), C(138,141,143), C(58,58,58), C(255,174,0), C(13,13,13), C(255,174,0), C(255,174,0), C(13,13,13), C(13,13,13), C(255,255,255), C(13,13,13), C(166,169,171), C(13,13,13), C(244,244,240), C(255,174,0), C(244,244,240), C(138,141,143), C(255,174,0), C(90,93,95), C(184,187,189), C(13,13,13), C(150,154,156), C(13,13,13), C(133,137,139), C(150,154,156), C(201,204,206), C(13,13,13), C(13,13,13), C(58,60,62), C(138,141,143), C(201,204,206), C(110,113,115),
    C(133,137,139), C(26,26,26), Color{},
    true, true, false,
    {255,174,0}, {26,26,26}, {0,0,0}, false
  },
  { // crt
    "crt", "CRT", "Thème CRT",
    C(7,9,7), C(16,32,16), C(12,16,12), C(30,58,30), C(30,58,30), C(51,255,119), C(31,138,66), C(4,6,4), C(30,58,30), C(17,34,17), C(30,58,30), C(51,255,119), C(31,138,66), C(30,58,30), C(51,255,119), C(4,6,4), C(255,136,0), C(4,6,4), C(51,255,119), C(51,255,119), C(4,6,4), C(4,6,4), C(12,16,12), C(31,138,66), C(51,255,119), C(31,138,66), C(51,255,119), C(31,138,66), C(255,136,0), C(30,90,46), C(12,16,12), C(51,255,119), C(8,12,8), C(30,58,30), C(30,58,30), C(8,12,8), C(4,6,4), C(51,255,119), C(31,138,66), C(31,138,66), C(7,9,7), C(4,6,4), C(30,58,30),
    Color{}, Color{}, Color{},
    false, false, false,
    {51,255,119}, {31,138,66}, {0,0,0}, false
  },
  { // ultraviolet
    "ultraviolet", "ULTRAVIOLET", "Thème ULTRAVIOLET",
    C(235,232,242), C(208,201,224), C(247,245,250), C(35,25,52), C(35,25,52), C(26,17,40), C(107,95,133), C(14,9,24), C(35,25,52), C(40,26,66), C(58,42,92), C(242,236,255), C(154,139,184), C(46,33,80), C(138,43,226), C(255,255,255), C(157,78,221), C(157,78,221), C(138,43,226), C(35,25,52), C(242,236,255), C(35,25,52), C(247,245,250), C(35,25,52), C(242,236,255), C(181,107,255), C(242,236,255), C(125,108,158), C(157,78,221), C(181,170,203), C(247,245,250), C(35,25,52), C(251,250,253), C(201,192,220), C(223,217,235), C(239,235,247), C(255,255,255), C(35,25,52), C(35,25,52), C(142,130,168), C(235,232,242), C(255,255,255), C(223,217,235),
    C(223,217,235), Color{}, C(157,78,221),
    true, false, true,
    {157,78,221}, {60,30,99}, {199,125,255}, true
  },
  { // onirique
    "onirique", "ONIRIQUE", "Thème ONIRIQUE",
    C(242,234,235), C(222,200,203), C(250,246,246), C(61,26,32), C(61,26,32), C(43,15,20), C(122,90,96), C(24,9,13), C(61,26,32), C(54,21,30), C(74,31,42), C(255,240,243), C(181,140,149), C(54,21,30), C(255,42,75), C(43,15,20), C(255,42,75), C(255,42,75), C(255,42,75), C(61,26,32), C(255,240,243), C(61,26,32), C(250,246,246), C(61,26,32), C(255,240,243), C(255,42,75), C(255,240,243), C(138,106,114), C(255,42,75), C(201,176,181), C(250,246,246), C(61,26,32), C(239,227,229), C(201,176,181), C(226,207,210), C(239,227,229), C(255,255,255), C(61,26,32), C(61,26,32), C(169,138,145), C(242,234,235), C(255,255,255), C(226,207,210),
    C(226,207,210), Color{}, Color{},
    true, false, false,
    {255,42,75}, {74,21,33}, {255,117,143}, true
  },
};

const FactoryPreset kFactoryPresets[kNumFactoryPresets] = {
  {"verre givré", Category::Lead, "dream pop", {Shape::Glass, 0.0f, 1.0f}, {Shape::Sine, 0.0f, 0.3f}, 0.42f, 0.66f, 0.78f, 0.53f, 0, 0, 0, 0, 0.39f, 0.3f, 0.6f, 45, -20, 0, false, 0.7f},
  {"neige lente", Category::Pad, "ambient", {Shape::Sine, 0.0f, 0.6f}, {Shape::Tri, 0.0f, 0.4f}, 0.3f, 0.55f, 0.62f, 0.55f, 0, 0, 0, 7, 0.12f, 0.38f, 0.7f, 30, 25, 35, false, 0.68f},
  {"sous la glace", Category::Bass, "sub", {Shape::Sine, 0.0f, 0.3f}, {Shape::Metal, 0.2f, 0.8f}, 0.2f, 0.5f, 0.92f, 0.4f, -2, 0, -1, 0, 0.28f, 0.22f, 0.45f, 20, 0, 5, false, 0.84f},
  {"brume perso", Category::Pad, "cloud", {Shape::Complex, 0.0f, 1.0f}, {Shape::Noise, 0.1f, 0.6f}, 0.55f, 0.4f, 0.6f, 0.45f, 0, 0, 0, 12, 0.15f, 0.42f, 0.75f, 35, -30, 55, true, 0.66f},
  {"sub velours", Category::Bass, "sub", {Shape::Sine, 0.0f, 0.2f}, {Shape::Tri, 0.0f, 0.1f}, 0.1f, 0.3f, 0.95f, 0.45f, -2, 0, -2, 0, 0.18f, 0.2f, 0.4f, 10, 0, 0, false, 0.86f},
  {"808 fumée", Category::Bass, "melodic trap", {Shape::Sine, 0.3f, 1.0f}, {Shape::Sine, 0.0f, 0.5f}, 0.25f, 0.4f, 0.94f, 0.5f, -2, 0, -1, 0, 0.22f, 0.18f, 0.35f, 25, -15, 10, false, 0.88f},
  {"plug lourd", Category::Bass, "plugg", {Shape::Saw, 0.2f, 0.8f}, {Shape::Square, 0.1f, 0.5f}, 0.35f, 0.5f, 0.9f, 0.55f, -1, 0, -2, 0, 0.34f, 0.25f, 0.5f, 40, -20, 15, true, 0.84f},
  {"rage saw", Category::Bass, "rage", {Shape::Saw, 0.6f, 1.0f}, {Shape::Saw, 0.4f, 1.0f}, 0.6f, 0.7f, 0.88f, 0.7f, -1, 0, -1, 7, 0.55f, 0.3f, 0.65f, 65, -30, 35, true, 0.82f},
  {"béton liquide", Category::Bass, "trap", {Shape::Metal, 0.0f, 1.0f}, {Shape::Sine, 0.0f, 0.2f}, 0.45f, 0.3f, 0.9f, 0.5f, -2, 0, -1, 0, 0.3f, 0.28f, 0.5f, 35, 10, 25, false, 0.85f},
  {"néon grave", Category::Bass, "hyperpop", {Shape::Square, 0.0f, 0.9f}, {Shape::Saw, 0.3f, 0.8f}, 0.55f, 0.62f, 0.86f, 0.62f, -1, 0, 0, 0, 0.48f, 0.32f, 0.6f, 55, -25, 20, true, 0.8f},
  {"cœur de sub", Category::Bass, "pop FR", {Shape::Sine, 0.0f, 0.15f}, {Shape::Sine, 0.2f, 0.4f}, 0.12f, 0.28f, 0.96f, 0.4f, -2, 0, -1, 0, 0.16f, 0.15f, 0.3f, 12, 5, 0, false, 0.88f},
  {"glitch bass", Category::Bass, "glitch", {Shape::Complex, 0.0f, 1.0f}, {Shape::Square, 0.2f, 0.9f}, 0.7f, 0.4f, 0.84f, 0.6f, -1, 0, -1, 5, 0.72f, 0.36f, 0.55f, 70, -45, 30, true, 0.8f},
  {"phonk nuit", Category::Bass, "phonk", {Shape::Saw, 0.1f, 0.5f}, {Shape::Tri, 0.0f, 0.6f}, 0.38f, 0.48f, 0.9f, 0.52f, -2, 0, -1, 0, 0.26f, 0.2f, 0.42f, 32, -10, 12, false, 0.86f},
  {"tram 04h", Category::Bass, "dark", {Shape::Tri, 0.0f, 0.8f}, {Shape::Metal, 0.1f, 0.7f}, 0.28f, 0.55f, 0.88f, 0.55f, -1, 0, -2, 0, 0.2f, 0.24f, 0.5f, 22, 15, 18, false, 0.84f},
  {"mur du son", Category::Bass, "rage", {Shape::Saw, 0.8f, 1.0f}, {Shape::Square, 0.5f, 1.0f}, 0.8f, 0.65f, 0.86f, 0.72f, -1, 0, -1, 0, 0.6f, 0.34f, 0.7f, 80, -35, 40, true, 0.8f},
  {"boue douce", Category::Bass, "lofi", {Shape::Sine, 0.4f, 0.9f}, {Shape::Noise, 0.0f, 0.3f}, 0.18f, 0.35f, 0.92f, 0.3f, -2, 0, -1, 0, 0.12f, 0.4f, 0.5f, 15, 0, 30, false, 0.85f},
  {"acid peluche", Category::Bass, "hyperpop", {Shape::Saw, 0.0f, 0.7f}, {Shape::Square, 0.3f, 0.6f}, 0.5f, 0.45f, 0.84f, 0.58f, -1, 0, 0, 0, 0.52f, 0.28f, 0.45f, 60, -40, 22, true, 0.8f},
  {"bas verglas", Category::Bass, "dream", {Shape::Metal, 0.5f, 1.0f}, {Shape::Sine, 0.0f, 0.4f}, 0.4f, 0.42f, 0.88f, 0.48f, -2, 0, -1, 0, 0.14f, 0.26f, 0.55f, 18, 20, 16, false, 0.85f},
  {"gros bébé", Category::Bass, "pop FR", {Shape::Square, 0.0f, 0.5f}, {Shape::Sine, 0.0f, 0.2f}, 0.3f, 0.22f, 0.94f, 0.5f, -2, 0, -2, 0, 0.24f, 0.22f, 0.4f, 26, -5, 8, false, 0.88f},
  {"hyper glace", Category::Lead, "hyperpop", {Shape::Saw, 0.5f, 1.0f}, {Shape::Saw, 0.6f, 1.0f}, 0.62f, 0.7f, 0.76f, 0.68f, 0, 0, 1, 0, 0.62f, 0.34f, 0.66f, 70, -30, 25, true, 0.72f},
  {"pop bubblegum", Category::Lead, "hyperpop", {Shape::Square, 0.0f, 0.4f}, {Shape::Tri, 0.0f, 0.3f}, 0.5f, 0.35f, 0.74f, 0.6f, 0, 0, 1, 0, 0.44f, 0.3f, 0.55f, 50, -15, 10, false, 0.72f},
  {"sirène rose", Category::Lead, "hyperpop", {Shape::Sine, 0.5f, 1.0f}, {Shape::Saw, 0.2f, 0.9f}, 0.7f, 0.55f, 0.72f, 0.58f, 1, 0, 0, 7, 0.58f, 0.38f, 0.6f, 75, -20, 28, true, 0.7f},
  {"cristal 2000", Category::Lead, "y2k", {Shape::Glass, 0.4f, 1.0f}, {Shape::Sine, 0.0f, 0.5f}, 0.58f, 0.64f, 0.7f, 0.52f, 1, 0, 0, 12, 0.4f, 0.32f, 0.58f, 45, 10, 15, false, 0.72f},
  {"laser tendre", Category::Lead, "melodic trap", {Shape::Saw, 0.0f, 0.6f}, {Shape::Sine, 0.3f, 0.8f}, 0.48f, 0.5f, 0.76f, 0.5f, 0, 0, 0, 0, 0.5f, 0.26f, 0.48f, 55, -10, 12, false, 0.72f},
  {"y2k diva", Category::Lead, "y2k", {Shape::Complex, 0.2f, 0.8f}, {Shape::Tri, 0.0f, 0.5f}, 0.44f, 0.6f, 0.72f, 0.56f, 0, 0, 0, 7, 0.46f, 0.34f, 0.62f, 48, -25, 18, false, 0.7f},
  {"supersaw cœur", Category::Lead, "hyperpop", {Shape::Saw, 0.8f, 1.0f}, {Shape::Saw, 0.9f, 1.0f}, 0.66f, 0.72f, 0.8f, 0.78f, 0, 0, 0, 12, 0.56f, 0.4f, 0.7f, 68, -35, 30, true, 0.7f},
  {"voix de verre", Category::Lead, "pop FR", {Shape::Glass, 0.0f, 0.7f}, {Shape::Tri, 0.2f, 0.6f}, 0.38f, 0.46f, 0.74f, 0.54f, 0, 0, 0, 0, 0.36f, 0.28f, 0.52f, 38, -12, 14, false, 0.72f},
  {"plug flûte", Category::Lead, "plugg", {Shape::Sine, 0.0f, 0.5f}, {Shape::Sine, 0.3f, 0.7f}, 0.28f, 0.36f, 0.72f, 0.5f, 1, 0, 0, 0, 0.42f, 0.22f, 0.4f, 40, 0, 5, false, 0.74f},
  {"étincelle", Category::Lead, "hyperpop", {Shape::Square, 0.3f, 1.0f}, {Shape::Metal, 0.0f, 0.6f}, 0.76f, 0.58f, 0.74f, 0.6f, 1, 0, 1, 0, 0.68f, 0.36f, 0.58f, 72, -28, 24, true, 0.7f},
  {"néon chanté", Category::Lead, "pop FR", {Shape::Tri, 0.0f, 1.0f}, {Shape::Glass, 0.3f, 0.9f}, 0.52f, 0.52f, 0.72f, 0.58f, 0, 0, 0, 7, 0.34f, 0.3f, 0.56f, 42, -8, 16, false, 0.72f},
  {"pixel love", Category::Lead, "chiptune pop", {Shape::Square, 0.0f, 0.2f}, {Shape::Square, 0.3f, 0.5f}, 0.34f, 0.44f, 0.7f, 0.6f, 0, 0, 1, 0, 0.54f, 0.28f, 0.45f, 52, -14, 6, false, 0.72f},
  {"candy square", Category::Lead, "hyperpop", {Shape::Square, 0.4f, 0.9f}, {Shape::Saw, 0.2f, 0.6f}, 0.6f, 0.5f, 0.74f, 0.62f, 0, 0, 0, 5, 0.46f, 0.32f, 0.55f, 56, -18, 14, true, 0.72f},
  {"lumière froide", Category::Lead, "dream pop", {Shape::Glass, 0.2f, 1.0f}, {Shape::Sine, 0.0f, 0.2f}, 0.46f, 0.62f, 0.7f, 0.48f, 1, 0, 0, 0, 0.3f, 0.26f, 0.52f, 34, 12, 20, false, 0.74f},
  {"tiktok bell", Category::Lead, "pop", {Shape::Metal, 0.3f, 0.9f}, {Shape::Sine, 0.0f, 0.4f}, 0.56f, 0.4f, 0.72f, 0.5f, 1, 0, 1, 0, 0.38f, 0.24f, 0.42f, 44, -5, 8, false, 0.74f},
  {"spirale", Category::Lead, "glitch", {Shape::Complex, 0.0f, 1.0f}, {Shape::Sine, 0.4f, 1.0f}, 0.64f, 0.68f, 0.72f, 0.56f, 0, 0, 0, 7, 0.6f, 0.38f, 0.62f, 66, -38, 26, true, 0.7f},
  {"diamant digital", Category::Lead, "y2k", {Shape::Saw, 0.3f, 0.7f}, {Shape::Glass, 0.5f, 1.0f}, 0.54f, 0.58f, 0.76f, 0.6f, 0, 0, 1, 0, 0.48f, 0.3f, 0.54f, 50, -22, 18, false, 0.72f},
  {"aurore tiède", Category::Pad, "ambient", {Shape::Glass, 0.0f, 0.8f}, {Shape::Tri, 0.0f, 0.3f}, 0.3f, 0.5f, 0.64f, 0.54f, 0, 0, 0, 7, 0.1f, 0.44f, 0.78f, 28, 30, 45, false, 0.68f},
  {"coton nuit", Category::Pad, "lofi", {Shape::Sine, 0.0f, 0.7f}, {Shape::Noise, 0.0f, 0.2f}, 0.22f, 0.4f, 0.66f, 0.4f, 0, 0, 0, 0, 0.08f, 0.48f, 0.8f, 20, 20, 40, false, 0.7f},
  {"velours bleu", Category::Pad, "dream pop", {Shape::Tri, 0.0f, 0.9f}, {Shape::Sine, 0.2f, 0.6f}, 0.36f, 0.46f, 0.64f, 0.52f, 0, 0, 0, 12, 0.14f, 0.4f, 0.72f, 32, 24, 38, false, 0.68f},
  {"cloud nine", Category::Pad, "cloud", {Shape::Saw, 0.0f, 0.3f}, {Shape::Sine, 0.0f, 0.5f}, 0.4f, 0.52f, 0.62f, 0.5f, 0, 0, 1, 0, 0.12f, 0.46f, 0.8f, 26, 18, 50, false, 0.66f},
  {"chambre d'écho", Category::Pad, "dub", {Shape::Complex, 0.2f, 0.6f}, {Shape::Metal, 0.0f, 0.4f}, 0.52f, 0.44f, 0.6f, 0.46f, 0, 0, 0, 7, 0.16f, 0.36f, 0.7f, 40, -20, 52, true, 0.66f},
  {"sirop de lune", Category::Pad, "dream pop", {Shape::Glass, 0.3f, 1.0f}, {Shape::Sine, 0.0f, 0.8f}, 0.44f, 0.58f, 0.64f, 0.5f, 0, 0, 0, 5, 0.09f, 0.42f, 0.76f, 24, 26, 44, false, 0.68f},
  {"satin", Category::Pad, "pop FR", {Shape::Tri, 0.2f, 0.7f}, {Shape::Tri, 0.0f, 0.5f}, 0.34f, 0.42f, 0.62f, 0.56f, 0, 0, 0, 12, 0.18f, 0.34f, 0.62f, 30, 10, 30, false, 0.7f},
  {"mer calme", Category::Pad, "ambient", {Shape::Sine, 0.1f, 0.9f}, {Shape::Glass, 0.0f, 0.4f}, 0.26f, 0.48f, 0.64f, 0.44f, 0, 0, -1, 0, 0.07f, 0.5f, 0.84f, 18, 22, 48, false, 0.68f},
  {"dream 3am", Category::Pad, "hyperpop", {Shape::Saw, 0.2f, 0.8f}, {Shape::Square, 0.1f, 0.5f}, 0.58f, 0.54f, 0.6f, 0.52f, 0, 0, 0, 7, 0.22f, 0.4f, 0.72f, 46, -26, 36, true, 0.66f},
  {"pastel", Category::Pad, "dream pop", {Shape::Square, 0.0f, 0.3f}, {Shape::Tri, 0.0f, 0.4f}, 0.28f, 0.38f, 0.6f, 0.54f, 0, 0, 0, 0, 0.15f, 0.32f, 0.6f, 22, 8, 24, false, 0.7f},
  {"halo", Category::Pad, "cloud", {Shape::Glass, 0.5f, 1.0f}, {Shape::Sine, 0.0f, 0.3f}, 0.62f, 0.6f, 0.62f, 0.42f, 1, 0, 0, 0, 0.11f, 0.44f, 0.78f, 34, 16, 42, false, 0.68f},
  {"soupir", Category::Pad, "lofi", {Shape::Noise, 0.0f, 0.2f}, {Shape::Sine, 0.0f, 0.5f}, 0.2f, 0.34f, 0.58f, 0.54f, 0, 0, 0, 0, 0.06f, 0.52f, 0.86f, 14, 30, 56, false, 0.68f},
  {"goutte", Category::Pluck, "pop", {Shape::Sine, 0.0f, 0.6f}, {Shape::Sine, 0.4f, 1.0f}, 0.3f, 0.4f, 0.74f, 0.46f, 1, 0, 1, 0, 0.76f, 0.16f, 0.3f, 48, 0, 4, false, 0.76f},
  {"marimba glacé", Category::Pluck, "y2k", {Shape::Metal, 0.0f, 0.6f}, {Shape::Tri, 0.0f, 0.4f}, 0.4f, 0.38f, 0.76f, 0.5f, 0, 0, 1, 0, 0.8f, 0.18f, 0.32f, 52, -6, 6, false, 0.76f},
  {"kalimba rose", Category::Pluck, "pop FR", {Shape::Tri, 0.1f, 0.5f}, {Shape::Sine, 0.2f, 0.7f}, 0.36f, 0.44f, 0.74f, 0.48f, 1, 0, 0, 7, 0.72f, 0.2f, 0.34f, 46, -4, 5, false, 0.76f},
  {"cloche mini", Category::Pluck, "y2k", {Shape::Glass, 0.5f, 1.0f}, {Shape::Metal, 0.2f, 0.7f}, 0.6f, 0.52f, 0.72f, 0.46f, 1, 0, 1, 12, 0.78f, 0.18f, 0.3f, 50, -8, 8, false, 0.76f},
  {"verre cassé doux", Category::Pluck, "glitch", {Shape::Complex, 0.3f, 1.0f}, {Shape::Square, 0.0f, 0.4f}, 0.68f, 0.46f, 0.72f, 0.5f, 0, 0, 1, 0, 0.84f, 0.22f, 0.36f, 58, -24, 14, true, 0.74f},
  {"pluck cœur", Category::Pluck, "melodic trap", {Shape::Saw, 0.0f, 0.4f}, {Shape::Sine, 0.0f, 0.3f}, 0.32f, 0.4f, 0.76f, 0.46f, 0, 0, 0, 0, 0.7f, 0.18f, 0.34f, 44, -10, 6, false, 0.76f},
  {"boîte à musique", Category::Pluck, "dream pop", {Shape::Sine, 0.2f, 0.8f}, {Shape::Glass, 0.0f, 0.5f}, 0.46f, 0.5f, 0.72f, 0.44f, 1, 0, 2, 0, 0.64f, 0.16f, 0.28f, 40, 0, 10, false, 0.76f},
  {"tic toc", Category::Pluck, "chiptune pop", {Shape::Square, 0.0f, 0.15f}, {Shape::Square, 0.1f, 0.3f}, 0.26f, 0.3f, 0.74f, 0.48f, 0, 0, 1, 0, 0.88f, 0.2f, 0.3f, 54, -12, 3, false, 0.76f},
  {"escalier", Category::Arp, "pop", {Shape::Saw, 0.0f, 0.4f}, {Shape::Tri, 0.0f, 0.3f}, 0.44f, 0.4f, 0.74f, 0.48f, 0, 0, 0, 0, 0.84f, 0.3f, 0.48f, 60, -14, 8, false, 0.74f},
  {"manège", Category::Arp, "hyperpop", {Shape::Square, 0.2f, 0.8f}, {Shape::Saw, 0.2f, 0.7f}, 0.56f, 0.52f, 0.72f, 0.54f, 0, 0, 1, 0, 0.9f, 0.34f, 0.52f, 66, -20, 12, true, 0.72f},
  {"dentelle", Category::Arp, "dream pop", {Shape::Glass, 0.0f, 0.9f}, {Shape::Sine, 0.1f, 0.5f}, 0.5f, 0.56f, 0.7f, 0.46f, 1, 0, 0, 7, 0.78f, 0.28f, 0.44f, 56, -8, 14, false, 0.74f},
  {"pulse pop", Category::Arp, "pop FR", {Shape::Tri, 0.0f, 0.6f}, {Shape::Square, 0.0f, 0.3f}, 0.38f, 0.36f, 0.74f, 0.5f, 0, 0, 0, 0, 0.86f, 0.32f, 0.46f, 62, -12, 6, false, 0.74f},
  {"ricochet", Category::Arp, "glitch", {Shape::Complex, 0.2f, 1.0f}, {Shape::Metal, 0.2f, 0.8f}, 0.66f, 0.6f, 0.72f, 0.52f, 0, 0, 1, 0, 0.94f, 0.38f, 0.56f, 74, -34, 20, true, 0.72f},
  {"lucioles", Category::Arp, "ambient", {Shape::Sine, 0.0f, 0.8f}, {Shape::Glass, 0.3f, 1.0f}, 0.48f, 0.54f, 0.66f, 0.44f, 1, 0, 2, 0, 0.7f, 0.26f, 0.38f, 50, 6, 18, false, 0.74f},
  {"texture neige", Category::Fx, "ambient", {Shape::Noise, 0.0f, 0.4f}, {Shape::Glass, 0.0f, 0.6f}, 0.5f, 0.5f, 0.56f, 0.5f, 0, 0, 0, 0, 0.1f, 0.5f, 0.8f, 20, 30, 80, false, 0.66f},
  {"grésil", Category::Fx, "glitch", {Shape::Noise, 0.3f, 1.0f}, {Shape::Square, 0.0f, 0.2f}, 0.7f, 0.3f, 0.58f, 0.5f, 0, 0, 1, 0, 0.66f, 0.46f, 0.7f, 60, -40, 90, true, 0.64f},
  {"radio morte", Category::Fx, "lofi", {Shape::Noise, 0.0f, 0.8f}, {Shape::Tri, 0.0f, 0.2f}, 0.4f, 0.44f, 0.54f, 0.44f, 0, 0, 0, 0, 0.2f, 0.52f, 0.82f, 28, -10, 85, false, 0.64f},
  {"souffle", Category::Fx, "ambient", {Shape::Noise, 0.0f, 0.1f}, {Shape::Sine, 0.0f, 0.2f}, 0.3f, 0.26f, 0.52f, 0.36f, 0, 0, 0, 0, 0.08f, 0.54f, 0.86f, 16, 20, 95, false, 0.62f},
  {"tempête rose", Category::Fx, "hyperpop", {Shape::Saw, 0.7f, 1.0f}, {Shape::Noise, 0.2f, 0.8f}, 0.82f, 0.7f, 0.6f, 0.6f, 0, 0, 0, 12, 0.74f, 0.44f, 0.78f, 85, -50, 70, true, 0.66f},
  {"eau de verre", Category::Fx, "dream", {Shape::Glass, 0.2f, 1.0f}, {Shape::Noise, 0.0f, 0.3f}, 0.54f, 0.46f, 0.58f, 0.46f, 0, 0, 0, 0, 0.18f, 0.4f, 0.66f, 32, 18, 60, false, 0.66f},
  {"static kiss", Category::Fx, "glitch", {Shape::Noise, 0.5f, 1.0f}, {Shape::Complex, 0.0f, 0.6f}, 0.76f, 0.58f, 0.6f, 0.5f, 0, 0, 0, 7, 0.62f, 0.42f, 0.72f, 70, -45, 75, true, 0.64f},
  {"portail", Category::Fx, "sci-fi", {Shape::Sine, 0.6f, 1.0f}, {Shape::Metal, 0.3f, 1.0f}, 0.6f, 0.72f, 0.58f, 0.56f, -1, 0, 1, 0, 0.3f, 0.36f, 0.64f, 52, 40, 50, false, 0.66f},
  {"réverse", Category::Fx, "pop", {Shape::Tri, 0.0f, 1.0f}, {Shape::Sine, 0.0f, 0.6f}, 0.8f, 0.2f, 0.58f, 0.48f, 0, 0, 0, 0, 0.24f, 0.3f, 0.5f, 44, -60, 30, false, 0.66f},
  {"poussière", Category::Fx, "lofi", {Shape::Noise, 0.0f, 0.5f}, {Shape::Glass, 0.0f, 0.3f}, 0.36f, 0.4f, 0.5f, 0.4f, 0, 0, 0, 0, 0.12f, 0.56f, 0.88f, 12, 10, 92, false, 0.62f},
};

} // namespace raw2
