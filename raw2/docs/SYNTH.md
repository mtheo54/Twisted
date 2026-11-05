# Page SYNTH (v9)

La page SYNTH réunit le cœur de la synthèse. Tout ce qui s'y règle est un paramètre automatisable
de l'hôte, et peut être modulé (poignées [+ MOD]). Les 72 presets d'usine n'utilisent pas ces
nouveautés : ils sonnent comme avant.

## Filtre multimode

| Type | Ce qu'il fait | MORPH |
|---|---|---|
| LP / HP / BP | passe-bas, passe-haut, passe-bande (SVF, un par côté) | — |
| NOTCH / PEAK | creux ou bosse à la coupure | — |
| PHASE | 2 (12 dB) ou 4 (24 dB) passe-tout mélangés au son direct : creux de phaser | écart entre les étages |
| COMB + / COMB − | retard d'une période de la coupure, retour positif ou négatif (son métallique, flanger figé) | amorti des aigus dans la boucle |
| FORMANT | trois bandes de voyelle (A E I O U), décalées par CUTOFF | voyelle |
| MORPH | fondu continu LP → BP → HP | position du fondu |

- **12 / 24 dB** : pente des types SVF (LP, HP, BP, NOTCH, PEAK, MORPH) et nombre d'étages de PHASE.
- **DRIVE** : saturation douce dans chaque étage. **MIX** : part filtrée. **CLAVIER** : suivi du
  clavier (50 % : une demi-octave par octave, le réglage d'avant).

## Warp des oscillateurs

12 modes par oscillateur, dosés par le potard WARP :

- **CLASSIQUE** (le warp d'avant), **SYNC** (oscillateur synchronisé jusqu'à 8 fois),
  **BEND + / BEND −** (forme pincée vers le centre ou les bords), **PWM** (forme comprimée,
  silence en fin de cycle), **ASYM** (première moitié comprimée), **MIRROR** (aller puis retour),
  **FLIP** (fin du cycle inversée), **QUANTIZE** (réduction de résolution).
- **FM ← B / AM ← B / RM ← B** (et l'inverse pour l'OSC B) : l'autre oscillateur module la phase,
  l'amplitude ou fait une modulation en anneau. Mettre B une quinte ou une octave au-dessus de A
  donne les basses et leads « growl » classiques.

Le niveau de mipmap tient compte de la largeur de bande ajoutée par le mode : pas de repliement.

## Bruit

BLANC (stéréo), ROSE, CRAQUE (craquements de vinyle), NUMÉRIQUE (bruit échantillonné à la hauteur
de la note). COULEUR : sombre (passe-bas) ↔ blanc ↔ clair (passe-haut). Le bruit suit l'enveloppe A
et passe dans le filtre.

## Unison

1 à 16 copies par note (32 voix au total). DÉSACCORD × ÉCART MAX (1 demi-ton, 2, une quinte, une
octave), LARGEUR stéréo, **BLEND** : niveau des copies écartées par rapport à celles du centre (la
puissance totale reste la même).

## Modulation

- **ENV 3 et ENV 4** (page ENV / LFO, bascule A/3 et B/4 de chaque carte) : deux enveloppes de
  modulation par note, avec leur poignée [+ MOD].
- **Routes par note** : une route dont la source change d'une note à l'autre (vélocité, pression,
  timbre, ENV 3, ENV 4, clavier, aléa par note) vers un réglage de voix (position, niveau, pan, warp
  des oscillateurs, coupure, résonance, drive, morph, mix du filtre, bruit, sub, blend) est calculée
  voix par voix : deux notes jouées ensemble ont chacune leur filtre.
- **Courbe et source qui dose** (page MODULES : cliquer une route dans PATCHS ACTIFS) : courbe de
  −100 (monte tard, exponentielle) à +100 (monte vite, logarithmique), et une deuxième source qui
  dose la route (par exemple ENV 3 → cutoff dosé par la vélocité).

## Expression et MPE

- **Pression** : aftertouch du canal (paramètre « Pression »), aftertouch polyphonique, ou
  pression MPE par note. **Timbre** : CC 74 ou glissé MPE.
- **MPE ON** : chaque note sur son canal (2 à 16), avec son pitch bend (amplitude BEND MPE,
  48 demi-tons par défaut), sa pression et son glissé. En Audio Unit, le MPE arrive en MIDI ; en
  VST3, par la Note Expression (hauteur, brillance, pression) et l'aftertouch polyphonique.
- **PITCH BEND** : amplitude de la molette (2 demi-tons par défaut, jusqu'à 24).
- Les réglages de jeu (MPE, pression, timbre) restent ceux de l'instance : charger un preset ne
  coupe pas le MPE.

## Tables d'onde de 256 frames

- Une table a de 1 à 256 frames (boutons − / + de la page WAVETABLE : 1, 8, 16, 32, 64, 128, 256,
  frames interpolées).
- **Import** (glisser un WAV) :
  - **AUTO** : un fichier fait de frames de 2048 points bout à bout (export Serum, Vital, RAW 2) est
    pris tel quel ; sinon le son est découpé en cycles.
  - **CYCLES** : la hauteur est détectée (YIN), puis un cycle est pris par frame du début à la fin
    du son, la période réajustée sur place (vibrato, glissando), chaque cycle aligné sur sa
    fondamentale (le fondu entre frames ne se creuse pas).
  - **RESYNTH.** : amplitudes des harmoniques de chaque frame, phases communes : un morph parfaitement
    doux d'un bout à l'autre du son.
  - 64, 128 ou 256 frames pour un son quelconque.
- Une frame importée garde ses **2048 points** (jusqu'à 1023 harmoniques, 511 jouées dans le grave) ;
  retouchée dans l'éditeur, elle repart de ses 256 points.
- **Export** : toutes les frames en 2048 points, 16 bits, compatible import Serum.
