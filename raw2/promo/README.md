# Vidéos promo de RAW 2

Ce dossier contient deux publicités en 3D, faites dans le style d'ABRASION : le châssis alu, l'orange
`#FF4D00`, la police pixel du logo, les références techniques (« SPEC.NO: RAW-02 », « MIL-STD »), les
coins du plugin et la mention « DISTRIBUÉ SUR ONIRIQUE ». Chaque vidéo existe en 9:16 (Reels,
TikTok, Shorts, stories) et en 1:1 (fil Instagram, Facebook).

| Vidéo | Durée | Contenu |
|---|---|---|
| **LANCEMENT** | 20 s | La wavetable se dessine, puis l'appareil RAW 2 tombe sur le drop. Ensuite viennent 5 fiches : oscillateurs, vie cellulaire, instruments, synthèse et thèmes. La vidéo finit sur le carton ONIRIQUE. |
| **LE SON** | 16 s | La vraie forme d'onde d'un Wurlitzer s'affiche, suivie des 23 instruments réels au rythme de la musique. La séquence « fusionne-les » montre la wavetable au-dessus de la grille vivante. La vidéo finit sur le carton ONIRIQUE. |

**Effets « TouchDesigner », pilotés par la musique** : `audio_features.py` analyse le WAV image par
image (graves, médiums, aigus, attaques, 32 bandes). La scène s'en sert pour :
- le feedback à traînées (image précédente zoomée, tournée, déplacée par du bruit) ;
- le nuage de particules : les 76 800 pixels de l'interface tournent dans un champ de bruit, se rangent pour former l'écran au drop, puis explosent ;
- le sol en grille de lignes, qui ondule et lance des ondes sur la 808 ;
- le glitch par tranches, l'aberration chromatique et la trame pixel (dither noir / orange / blanc) sur les coupes ;
- l'analyseur de spectre vertical, dans la marge.

Aucun objet n'en touche un autre :
- le sol reste calme sous les objets ;
- les thèmes sont posés en pile (9:16) ou en carrousel (1:1) avec un espacement supérieur à leur taille ;
- dans « fusionne-les », la wavetable flotte au-dessus de la grille.

Tout ce qu'on voit et entend vient de RAW 2 :
- **Les écrans** de l'appareil 3D sont de vraies captures de l'interface v1.0.0.
- **La grille** est un vrai jeu de la vie (règle B3/S23, comme le module VIE).
- **La forme d'onde** est celle d'un vrai échantillon de la banque.
- **La musique** est jouée par le moteur de RAW 2 : presets d'usine (808 fumée, tic toc, escalier, portail) et presets d'instruments (Wurli, Cordes Disco, Vibes, Sax). Les partitions sont dans `lancement.txt` et `le-son.txt`.

## Changer les textes, ajouter un prix

Tous les textes sont dans `textes.json`. Le champ `offre` est vide par défaut. Si on y met, par
exemple, `"19,99 € · PRIX DE LANCEMENT"`, la ligne apparaît en orange sur le carton de fin. Il
suffit ensuite de relancer le rendu.

## Refaire les vidéos (Linux)

Prérequis :
- un build avec les outils : `cmake -B build -DRAW2_BUILD_TRAINER=ON && cmake --build build` ;
- la banque d'instruments (voir `docs/INSTRUMENTS.md`) ;
- Node.js avec Playwright et Chromium ;
- ffmpeg, Python 3 et numpy.

```sh
promo/render.sh <dossier de la banque d'instruments>
# -> build/promo-videos/RAW2-pub-{lancement,le-son}-{9x16,1x1}.mp4
```

Pour voir quelques images sans tout rendre :

```sh
node promo/render.cjs lancement 1080 1920 --apercu /tmp/apercu 4.1,9,18
```

## Fichiers

- `scene.html`, `scene.js` : la scène 3D (three.js, dans `vendor/`, licence MIT) et le montage.
  Les instants clés (drop, coupes) tombent sur les temps de la musique (120 BPM). La caméra
  tremble sur chaque coup de 808, lu dans la partition.
- `render.cjs` : rend chaque image dans Chromium sans fenêtre, puis ffmpeg assemble les images et
  la musique en H.264 + AAC, à 30 images/s.
- `audio_features.py` : analyse de la musique pour les effets.
- `make-assets.sh` : prépare les captures en 2560 × 1920 (`raw2_snap` avec `RAW2_SNAP_SCALE=2`),
  les musiques (`raw2_promo_audio`) et la forme d'onde du Wurlitzer.
