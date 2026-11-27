# Skins RAW 2 (.raw2skin) et ABRASION Skin Studio

Un skin habille toute l'interface de RAW 2 : couleurs, fond (image, GIF animé ou vidéo), cartes en
verre dépoli et **place de chaque bloc** sur chaque page. On le crée avec **ABRASION Skin Studio**,
une page web autonome (gratuite, hors ligne, rien n'est envoyé sur internet), et on le glisse sur
RAW 2 pour l'installer.

## Pour l'utilisateur

- **Choisir un skin** : bouton à droite du logo › panneau THÈME › SKIN, flèches `<` `>`.
  `SANS SKIN` revient au thème. Skins d'usine : VERRE NOIR, VIE NÉON, ONIRIQUE, ALU GIVRÉ.
- **Installer** : glisser un `.raw2skin` (ou le dossier d'un skin) n'importe où sur RAW 2. Il est
  copié dans `Documents/Raw 2/Skins/<nom>` et appliqué. Un skin du même nom est remplacé.
- **Fond perso sans skin** : glisser une image ou une vidéo sur RAW 2 (panneau THÈME › FOND).
  Avec un skin, une image glissée remplace son fond ; RETIRER remet celui du skin.
- **VERRE**, **VOILE**, **CARTES** : réglables dans le panneau, que le skin soit choisi ou non.

## ABRASION Skin Studio

`tools/skin-studio/ABRASION-Skin-Studio.html` (aussi dans le .dmg et le menu Démarrer sous Windows).

1. **Skin** : nom, auteur, un modèle de départ (VERRE NOIR, ALU GIVRÉ, ONIRIQUE, VIE NÉON).
2. **Couleurs** : base (un des 6 thèmes), teinte de l'accent, châssis, cartes, écrans (opacité :
   écrans translucides pour un verre complet), texte, bords ; tous les jetons en mode avancé.
3. **Fond** : image ou vidéo importée, ou fond **généré** (Liquide, Aurore, Vie, Ondes, Plasma,
   Grain, Glitch ; 3 couleurs, échelle, vitesse, tirage) fixe (PNG 1600 × 1200) ou animé (GIF en
   boucle, 480 × 360, 12 i/s) ; voile, grille de points.
4. **Verre** : flou, teinte, reflets ; opacité des cartes sans verre.
5. **Blocs** — ils se comportent comme des **widgets** (Android, iOS) : chacun change de taille à sa
   façon et son contenu se réorganise au lieu d'être zoomé :
   - **SORTIE (clavier)** : largeur seule (poignée sur le bord droit), 1 à 3 octaves, MASTER gardé ;
   - **OSC A / OSC B** : taille libre (300 × 430 à 640 × 867) ; l'écran s'agrandit, et une carte haute
     ajoute un panneau d'infos (source, frame, position, warp, phase, réglages, aperçu des frames) ;
     **si un OSC est retiré, l'autre affiche un sélecteur A | B et règle les deux** ;
   - **HARMONIQUES · ESPACE · TEXTURE** : largeur, sur 1, 2 ou 3 colonnes (hauteur déduite) ;
   - les autres blocs : à l'échelle (50 à 160 %, proportions gardées).
   **Grille invisible** (comme un écran de téléphone) : 24 colonnes × 12 rangées, 10 px entre les
   blocs. Au premier glisser, la page se range sur la grille (même proportions qu'à l'origine) ;
   ensuite les blocs se posent et s'étirent **case par case**. Pendant un glisser, le bloc suit le
   pointeur et ses cases d'arrivée s'affichent (rouges si elles sont prises). Les blocs à l'échelle
   sont centrés dans leurs cases.
   Dans l'aperçu, glisser un bloc pour le déplacer (aimant 8 px et bords des autres
   blocs, avec lignes de repère) et son **coin bas droit** pour le **rapetisser ou l'agrandir**
   (50 à 160 %, proportions gardées). **Tous les blocs restent visibles** : un bloc ne peut pas
   grandir par-dessus un autre ; lâché sur un autre, il va à la place libre la plus proche, ou
   échange sa place avec un bloc de même taille (OSC A ⇄ OSC B). Clavier : flèches (Maj : 8 px),
   `+` et `−` pour la taille ; la liste de la section 05 donne x, y et taille au chiffre près.
   **Retirer un bloc** d'une page : ✕ en haut à droite du bloc (ou touche Suppr, ou ✕ dans la
   liste). Les blocs retirés s'affichent sous l'aperçu (« + nom ») : un clic les remet, à leur
   place si elle est libre, sinon à la place libre la plus proche.

L'aperçu utilise les vraies vignettes de l'interface (rendues par RAW 2, `make_sprites.py`) : la
place des blocs, l'accent, les cartes, le verre et le fond sont ceux que RAW 2 affichera. Les autres
jetons de couleur ne s'y voient pas mais s'appliquent dans RAW 2.

**Exporter .raw2skin** télécharge le fichier (`.raw2skin.zip` depuis la version en ligne : RAW 2 lit aussi les `.zip`) ; **Ouvrir** reprend un skin exporté.

## Format

Une archive ZIP **sans compression** (ce qu'écrit le studio) ou un dossier, avec :

```json
{
  "format": "raw2skin", "version": 1,
  "name": "Verre noir", "author": "ABRASION",
  "base": "nothing",
  "colors": { "acc": "#FF4D00", "card": "#141418", "scr": "#050508A0" },
  "background": { "file": "fond.mp4", "voile": 0.25, "grille": false },
  "glass": { "on": true, "flou": 0.7, "teinte": 0.45, "reflet": 0.7 },
  "cards": { "opacite": 0.85 },
  "layout": { "son.oscA": [894, 73], "son.oscB": [24, 73], "son.sortie": [300, 740, 0.68] },
  "hidden": [ "son.modules" ]
}
```

- `base` : `alu`, `nothing`, `titane`, `crt`, `ultraviolet`, `onirique`.
- `colors` : jetons de `resources/data/themes.json` (`#RGB`, `#RRGGBB`, `#RRGGBBAA`, `rgba()`),
  plus `old` / `mid` (dégradé d'âge des cellules). Les inconnus sont ignorés.
- `background.file` : image (PNG, JPG, GIF animé, BMP, TIFF, WebP ; HEIC sur Mac) ou vidéo (MP4,
  MOV, M4V ; AVI, WMV sous Windows), décodée par le système. Vidéo : 10 s en boucle, 480 × 360,
  12 i/s ; image : jusqu'à 1600 × 1200.
- `layout` : coin haut gauche de chaque bloc placé (fenêtre 1280 × 960, bornée à la zone utile) et,
  en option, son échelle (0,4 à 1,6 ; 1 si absente) : `[x, y, échelle]`. Les widgets adaptatifs
  (`son.oscA`, `son.oscB`, `son.modules`, `son.sortie`) prennent `{"x", "y", "w", "h"}` ; leurs
  bornes et leur façon de changer de taille sont dans `src/ui/Blocks.cpp`. RAW 2 dessine le bloc à cette taille ; ses
  boutons, potards (même sensibilité), menus et zones de dépôt suivent.
- `hidden` : blocs retirés de leur page (ni dessinés, ni cliquables). Leurs réglages restent
  actifs et automatisables depuis la DAW.
  Clés : `src/ui/Blocks.cpp` (37 blocs, jamais renommés). Un bloc absent reste à sa place.
- Seuls `skin.json` et les images / vidéos sont installés ; les chemins dans l'archive sont
  ignorés (rien ne peut être écrit hors du dossier du skin). Une archive recompressée (deflate)
  est refusée avec un message : déposer alors le dossier.

## Fabrication

```
python3 tools/skin-studio/make_sprites.py build/raw2_snap tools/skin-studio/sprites   # après un changement d'interface
python3 tools/skin-studio/build_studio.py tools/skin-studio/ABRASION-Skin-Studio.html
python3 tools/skin-studio/make_factory_videos.py resources/factory/Skins              # vidéos des skins d'usine
```

`build_studio.py` lit la table des blocs dans `src/ui/Blocks.cpp` : le studio et le plugin
partagent les mêmes clés et les mêmes rectangles.
