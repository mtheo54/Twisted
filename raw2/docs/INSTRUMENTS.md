# Banque d'instruments RAW 2

RAW 2 peut jouer de **vrais instruments multi-échantillonnés** sur l'OSC A et l'OSC B, à la manière
d'un ROMpler. Chaque instrument passe ensuite dans toute la chaîne du synthé : enveloppes, filtre
12/24 dB, LFO, modules de VIE, effets, accords et arpégiateur.

## Installation

La banque est livrée à part (`RAW2-instruments.zip`, environ 270 Mo une fois décompressée) :

1. Décompresser le dossier `Instruments` dans `Documents\Raw 2\`. On obtient alors
   `Documents\Raw 2\Instruments\Wurlitzer EP200\…`.
2. Décompresser le pack de presets `RAW 2 · INSTRUMENTS` dans `Documents\Raw 2\`.
3. Dans le plugin, page WAVETABLE, cliquer sur **INSTRUMENT ▾**, puis sur **RELIRE**. Les presets
   du pack se trouvent dans le navigateur, onglet **USER**.

Un preset qui utilise un instrument retient seulement son **nom**. Si l'instrument n'est pas
installé, l'oscillateur reste muet et affiche « instrument introuvable ».

## Ajouter ses propres instruments

N'importe quel instrument **SFZ dont les échantillons sont en WAV** (16, 24 ou 32 bits, mono ou
stéréo) peut être déposé dans `Documents\Raw 2\Instruments\<nom>\`. RAW 2 lit le sous-ensemble
suivant du format :

- **Structure** : en-têtes `<control>` (`default_path`), `<global>`, `<master>`, `<group>` et
  `<region>`, avec l'héritage des en-têtes ; `#define` ; commentaires.
- **Placement** : `sample`, `key`, `lokey` / `hikey`, `pitch_keycenter` (numéros ou noms de note),
  `lovel` / `hivel`.
- **Réglages** : `tune`, `transpose`, `volume`, `global_volume`, `master_volume`, `group_volume`,
  `offset`.
- **Boucles** : `loop_mode=loop_continuous`, `loop_start` / `loop_end`. Les boucles inscrites
  dans le WAV lui-même (bloc `smpl`) ne sont pas lues.
- **Alternances et déclencheurs** : `seq_position` (seule la première alternance est gardée) ;
  les zones `trigger=release` sont ignorées.

Ne sont pas lus : les échantillons FLAC et OGG (à convertir en WAV), les contrôleurs MIDI
(CC), les enveloppes et filtres propres au SFZ (ceux de RAW 2 les remplacent), ni les extensions
ARIA.

Les lignes `// category: KEYS` et `// license: …` en tête du fichier `.sfz` donnent la catégorie
et la licence de l’instrument.

## Contenu de la banque livrée

Ces 23 instruments ont été choisis pour la French touch et le hip-hop américain : claviers
électriques, string machines, cuivres et nappes analogiques, basses et sifflet G-funk, saxophone
et vibraphone.

| Instrument | Catégorie | Source | Licence |
|---|---|---|---|
| Wurlitzer EP200 | KEYS | Greg Sullivan, SFZ par kinwie | CC BY 3.0 |
| CP80 Electric Grand | KEYS | Greg Sullivan, SFZ par kinwie | CC BY 3.0 |
| Pianet T | KEYS | Greg Sullivan, SFZ par kinwie | CC BY 3.0 |
| FM Piano | KEYS | Versilian Community Sample Library (Yamaha TX81Z) | CC0 |
| Clavisynth | KEYS | Versilian Community Sample Library (Yamaha TX81Z) | CC0 |
| Vibraphone | MALLETS | Versilian Community Sample Library | CC0 |
| Grand Piano | PIANO | Splendid Grand Piano (Akai), SFZ sfzinstruments | domaine public (voir plus bas) |
| String Machine | PAD | Karoryfer Samples, String Cyborgs | CC0 |
| Alto Sax | WINDS | Karoryfer Samples, Weresax | CC0 |
| G-Funk Bass | BASS | XSXB Band Samples | CC0 |
| G-Funk Whistle | LEAD | XSXB Band Samples | CC0 |
| OB String Machine | STRINGS | cc0-obxf-multisamples | CC0 |
| OB Pad Warmth | PAD | cc0-obxf-multisamples | CC0 |
| OB Pad Vintage Swell | PAD | cc0-obxf-multisamples | CC0 |
| OB Brass J60 | BRASS | cc0-obxf-multisamples | CC0 |
| OB Brass Stabs | BRASS | cc0-obxf-multisamples | CC0 |
| OB Funk Lead | LEAD | cc0-obxf-multisamples | CC0 |
| OB Super Saw | LEAD | cc0-obxf-multisamples | CC0 |
| OB Bass | BASS | cc0-obxf-multisamples | CC0 |
| OB Slap Bass | BASS | cc0-obxf-multisamples | CC0 |
| OB Rotary Organ | ORGAN | cc0-obxf-multisamples | CC0 |
| OB Tonewheels | ORGAN | cc0-obxf-multisamples | CC0 |
| OB Choir Aah | VOX | cc0-obxf-multisamples | CC0 |

Chaque dossier d'instrument contient un `LICENSE.txt` qui donne la licence, la source et
l'auteur. **Ces fichiers doivent accompagner toute redistribution de la banque.**

### Crédits (attribution obligatoire, CC BY 3.0)

> Wurlitzer EP200, Yamaha CP80 and Hohner Pianet T samples by Greg Sullivan
> (http://www.sullivang.net/), SFZ mapping by kinwie
> (https://github.com/sfzinstruments/GregSullivan.E-Pianos), licensed under CC BY 3.0
> (https://creativecommons.org/licenses/by/3.0/). Modified: trimmed, converted, re-mapped for
> RAW 2.

Le texte ci-dessus doit figurer dans les crédits du produit : page de vente, manuel ou fenêtre
« à propos ».

### Remerciements (CC0 : pas d'obligation, mais mérité)

- Versilian Studios, **VCSL** : https://github.com/sgossner/VCSL
- **Karoryfer Samples** (String Cyborgs, Weresax) : https://github.com/sfzinstruments
- **cc0-obxf-multisamples** (rendus de patchs OB-Xf) : https://github.com/User1362/cc0-obxf-multisamples
- **XSXB Band Samples** : https://github.com/sparklecatta-lang/XSXB-Band-Samples

### Grand Piano : à propos de la licence

Le dépôt sfzinstruments/SplendidGrandPiano présente ces échantillons comme placés dans le
**domaine public par Akai en 2000**. Le dépôt ne contient cependant aucun fichier de licence
formel. Pour une vente commerciale sans aucun doute possible, ce piano peut être retiré de la
banque, puisque le preset « Piano Trap » est le seul à l'utiliser. Cela réduit aussi la taille
de la banque d'environ 90 Mo.

## Comment la banque est construite

`tools/build_instruments.py <sources> <sortie>` reconstruit la banque à partir des dépôts
d'origine (Python 3, numpy, ffmpeg). Pour chaque instrument, l'outil effectue ces étapes :

1. Il lit le SFZ ou les noms de fichiers d'origine.
2. Il garde au plus 1 à 3 couches de vélocité et recouvre tout le clavier.
3. Il décode les fichiers, les rééchantillonne à 44,1 ou 48 kHz, coupe le silence de début et
   limite la durée (avec un fondu).
4. Il normalise l'instrument entier à −1 dBFS et écrit des WAV 16 bits.
5. Il calcule un **gain de compensation** (`<global> global_volume=…`). Ce gain permet à un piano
   ou à un vibraphone, dont le son décroît vite, de sonner aussi fort qu'une nappe.
6. Il écrit un SFZ propre et le fichier `LICENSE.txt`, puis vérifie la hauteur (estimation de
   f0, contrôle d'octave).

Le rapport `REPORT.md` est écrit dans le dossier de sortie.

Pour reconstruire un seul instrument : `RAW2_ONLY="Alto Sax" python3 tools/build_instruments.py …`.
Pour recalculer seulement les gains : `RAW2_MAKEUP_ONLY=1 python3 tools/build_instruments.py …`.

Le pack de presets est généré par `raw2_inst_pack <sortie>`, construit avec l'option
`RAW2_BUILD_TRAINER`. Quand la banque est installée sur la machine, ce même outil règle le MASTER
de chaque preset pour viser le volume des presets d'usine : RMS −20 dB, crêtes sous −3 dBFS.
