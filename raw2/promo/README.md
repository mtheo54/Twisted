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
- **L'appareil** est un modèle 3D fidèle à la page SON, avec potards, touches, LED et écrans animés par la musique.
- **La grille** est un vrai jeu de la vie (règle B3/S23, comme le module VIE).
- **La forme d'onde** est celle d'un vrai échantillon de la banque.
- **La musique** est jouée par le moteur de RAW 2 : presets d'usine (808 fumée, tic toc, escalier, portail) et presets d'instruments (Wurli, Cordes Disco, Vibes, Sax). Les partitions sont dans `lancement.txt` et `le-son.txt`.

## Pub HISTOIRE : le bonhomme d'ABRASION présente RAW 2 (≈ 29 s)

Le bonhomme d'ABRASION (`bonhomme.js`, d'après `game/reference/bonhomme_abrasion.png`) raconte
l'histoire sur le début de « shape » :
1. Sa tête tombe et rebondit, son corps surgit, il fait coucou.
2. Il traverse un RAW 2 géant posé à plat et éteint, puis appuie sur le logo : la machine s'allume en onde.
3. Il saute sur le clavier et frappe le do pile dans le silence d'avant le drop.
4. Il montre le plugin, un plan par mesure : il surfe la wavetable, fait naître des anneaux de LED dans la VIE, pousse un potard, court le long de la forme d'onde du Wurlitzer, passe devant les 6 thèmes.
5. Il s'assoit sur RAW 2 pour le carton ONIRIQUE.

Le scénario est dans `histoire.json` : dialogues, instants, bruitages. `sfx.py` synthétise les
bruitages (rebonds, pops, pas, clic, mise sous tension, grésillements, cascade de LED, envol, impact,
bips de texte, cliquets, glitchs, carillon) et les mixe sur la musique. Changer un instant dans
`histoire.json` déplace à la fois l'image et le son.

```sh
python3 promo/sfx.py shape.mp3 promo/histoire.json build/promo-assets/histoire.wav build/promo-assets/histoire-meta.json
python3 promo/audio_features.py build/promo-assets/histoire.wav build/promo-assets/histoire-audio.json
node promo/render.cjs histoire 1080 1920 build/promo-videos/RAW2-pub-histoire-9x16.mp4
```

## Avec une autre musique

```sh
promo/render.sh <dossier de la banque d'instruments> ma-musique.mp3
```

`pick_segment.py` s'occupe de caler la musique sur le montage :
- il détecte le tempo de la musique ;
- il choisit le meilleur passage pour chaque pub : il commence sur une mesure et monte en puissance pile quand l'appareil apparaît ;
- il découpe l'extrait et écrit le BPM dans `build/promo-assets/<pub>-meta.json`.

Le montage est écrit à 120 BPM : la scène l'étire ou le resserre au tempo trouvé, pour que les
coupes restent sur les temps. La grosse caisse, les touches enfoncées (notes présentes) et les
oscilloscopes viennent de l'analyse du son. Pour imposer le début du passage :
`python3 promo/pick_segment.py musique.mp3 lancement build/promo-assets/lancement.wav build/promo-assets/lancement-meta.json 42.5`
(puis `audio_features.py` sur l'extrait). Un 6e argument impose le BPM.

Avec un tempo connu, les impulsions (caméra, voyants, glitch) tombent sur la grille des temps. Leur
force suit l'attaque mesurée à chaque temps : elle est faible dans les silences et les breaks.

Version actuelle : « shape » d'omyll, à 132 BPM.
- LANCEMENT : de 10,85 s à 29,03 s. L'appareil apparaît sur le premier drop (14,48 s, juste après le silence).
- LE SON : de 56,30 s à 70,85 s. L'appareil se pose sur le drop de la mesure 32 (58,12 s).

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

- `synth3d.js` : RAW 2 modélisé en 3D d'après la page SON : panneaux, potards, clavier, LED de la VIE,
  écrans, afficheurs, sérigraphie. Les couleurs viennent de `resources/data/themes.json` (6 thèmes).
- `pick_segment.py` : choix du passage d'une musique externe et détection de son tempo.
- `scene.html`, `scene.js` : la scène 3D (three.js, dans `vendor/`, licence MIT) et le montage.
  Les instants clés (drop, coupes) tombent sur les temps de la musique (120 BPM). La caméra
  tremble sur chaque coup de 808, lu dans la partition.
- `render.cjs` : rend chaque image dans Chromium sans fenêtre, puis ffmpeg assemble les images et
  la musique en H.264 + AAC, à 30 images/s.
- `audio_features.py` : analyse de la musique pour les effets.
- `make-assets.sh` : prépare les captures en 2560 × 1920 (`raw2_snap` avec `RAW2_SNAP_SCALE=2`),
  les musiques (`raw2_promo_audio`) et la forme d'onde du Wurlitzer.
