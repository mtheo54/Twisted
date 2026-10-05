# Twisted · 物見櫓 (monomi) : spécification de l'interface

Statut : **proposition à valider**. La maquette interactive est dans
[`ui-prototype/index.html`](../ui-prototype/index.html) (ouvrir dans un navigateur).

## 1. Concept

Une tour d'observation japonaise en 3D low poly, inspirée de la Tokyo Tower
(treillis d'acier, deux plateformes, antenne bicolore). Elle domine un
quartier japonais : maisons à toits pyramidaux, machiya à étage, immeubles
aux fenêtres éclairées, pagode à cinq toits, torii, lanternes de rue.

Touche dark fantasy, gardée légère :

- acier rouge sang, presque noir la nuit ;
- pointes d'acier sur les avant-toits des plateformes ;
- arbres morts entre les maisons, braises et cendres qui montent ;
- montagnes en silhouette dans le brouillard, lune pâle avec halo.

## 2. Comportements de la scène

### Sur chaque coup de basse

- Une onde part du sommet de l'antenne : anneau horizontal qui s'élargit,
  second anneau qui descend, coque filaire qui se dilate, flash de la pointe.
- **La caméra tourne autour de la tour** : chaque coup lui donne une
  impulsion de rotation qui s'amortit en ~0,6 s (un tour complet en une
  vingtaine de secondes à 92 BPM avec le Punch par défaut). Désactivable avec
  l'option « Caméra sur les basses ».
- Une onde de choc au sol traverse la ville ; les immeubles pompent à son
  passage (si Compression > 0).

Détection : transitoires sur la bande 20-150 Hz (thread d'analyse) ; la force
du coup règle l'intensité de tous ces effets.

### Effet de chaque knob sur la scène

| Knob | Effet dans la scène | Type |
|---|---|---|
| Entrée | L'antenne s'allonge ou se rétracte (les ondes partent de plus haut) | Ville |
| Elevate | Saturation + enhance : les braises s'embrasent, l'acier de la tour rougeoie, les étoiles et les fenêtres scintillent, la pointe brille plus | Ville |
| Compression | Les immeubles pompent (ressort amorti) au passage de l'onde de choc | Physique |
| Punch | Rotation plus forte de la caméra à chaque basse + petit zoom d'impact | Caméra |
| Sub | La tour et l'image tremblent sur les basses | Physique / caméra |
| Space | Stéréo + ambiance : la caméra recule, les ondes s'élargissent, le brouillard s'épaissit, chaque onde laisse 1 à 3 échos | Caméra / ville |
| Dry / Wet | Heure du jour : 0 % jour, 40 % après-midi, 60 % crépuscule, 100 % nuit | Ville |
| Clipper | Un plafond lumineux (grille circulaire) descend sur la ville et écrase les toits qui dépassent ; il flashe sur les basses. **Hard** : coupe nette, grille rouge. **Soft** : toits arrondis en douceur, grille ambre. **Analog** : coude doux + toits qui vibrent sur les coups, grille dorée | Physique 3D |
| Limiter | Une bulle filaire entoure l'antenne et rétrécit quand on monte le knob ; les ondes s'y arrêtent et la font briller | 3D |
| Sortie | Exposition de l'image | Image |
| Bypass | Travelling avant sur l'antenne, image désaturée, bandeau « BYPASS · 素通し » | Caméra |

Survoler ou tourner un knob affiche son effet en haut de la scène.

### Prise de vue

Caméra en orbite autour de la tour, en légère contre-plongée depuis les toits.
Elle dérive lentement d'elle-même, accélère à chaque basse et suit la souris
en léger parallaxe. Les immeubles hauts ne sont placés que dans un anneau
intermédiaire, ni collés à la tour ni sur le trajet de la caméra, et leur
hauteur est limitée pour que la tour reste visible sous tous les angles.
Le brouillard suit la distance de la caméra pour que la tour ne disparaisse
jamais. Animations réduites si le système demande `prefers-reduced-motion`.

## 3. Disposition

Les knobs sont posés directement sur la scène 3D, en bas de l'image, sur un
dégradé sombre qui garde la ville lisible. Ils sont volontairement simples :
un anneau fin, un trait d'index, le nom et la valeur. Le Dry/Wet, plus grand,
est au centre et affiche aussi l'heure du jour. Les autres réglages
(suréchantillonnage, caractère, topologie, interrupteurs) restent dans une
barre sous la scène.

```
┌──────────────────────────────────────────────────────────────────┐
│ TWISTED 物見櫓          Preset [Bus batterie ▾]  ◎ Analyser  ● Bypass │
├──────────────────────────────────────────────────────────────────┤
│ [◎ Détecté : Bus batterie]  [ effet du knob ]  ┌Niveaux─────┐ │
│                                                    │ IN / OUT    │ │
│                            (scène 3D 16:9)         │ LUFS        │ │
│                                                     └────────────┘ │
│ ░░░░░░░░░░░░░░░░░░░░░ dégradé ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│  ◯ Entrée  ◯ Elevate   ◯ Comp ◯ Punch ◯ Sub  (◯ DRY/WET)             │
│     ◯ Space   ◯ Clipper ◯ Limiter   ◯ Sortie                         │
├──────────────────────────────────────────────────────────────────┤
│ Suréchantillonnage [1×…16×]  Caractère [▾]  Clipper [Hard|Soft|Analog] │
│ ◉ Gain auto  ◉ Caméra sur les basses                               │
├──────────────────────────────────────────────────────────────────┤
│ Latence 0 échantillon · OS 4× · SIMD AVX2 · Threads … · CPU 3 %     │
└──────────────────────────────────────────────────────────────────┘
```

(Sur grand écran, les 12 knobs tiennent sur une seule ligne.)

Il n'y a pas de panneau de reconnaissance : le bouton **Analyser** affiche
un petit bandeau en haut à gauche de la scène (« Écoute… 2,0 s », puis
« Détecté : Bus batterie 74 % · preset appliqué »), qui disparaît après
quelques secondes.

Taille par défaut de la fenêtre : 1180 × 830 px, redimensionnable. Les knobs
restent sur la scène à toutes les tailles : une ligne en grand, deux lignes
sous 900 px (vumètres masqués), knobs compacts sous 520 px.

## 4. Contrôles

Les knobs sont regroupés par espacement (sans titres) : Gain · Couleur ·
Dynamique · Mix · Espace · Loudness · Sortie.

| Groupe | Contrôle | Plage | Défaut | Rôle audio |
|---|---|---|---|---|
| Gain | Entrée | −24 … +24 dB | 0 dB | Trim d'entrée, piloté par le gain staging auto |
| Couleur | Elevate | 0 … 100 % | 30 % | Saturation + excitation harmonique liées sur un seul knob (« le son en mieux ») |
| Dynamique | Compression | 0 … 100 % | 30 % | Compresseur de bus (glue), seuil et ratio liés |
| Dynamique | Punch | 0 … 100 % | 40 % | Transient shaper (attaque) |
| Dynamique | Sub | 0 … 100 % | 35 % | Renfort psychoacoustique et dynamique du sub (< 100 Hz) |
| Mix | Dry / Wet | 0 … 100 % | 80 % | Mix parallèle |
| Espace | Space | 0 … 100 % | 40 % | Largeur Mid/Side (graves gardés mono) + ambiance courte sans pré-délai, liées |
| Loudness | Clipper | 0 … 100 % | 20 % | Écrêtage des crêtes avant le limiteur (sans latence) |
| Loudness | Limiter | 0 … 100 % | 40 % | Limiteur de sortie, plafond true peak −1 dBTP, sans look-ahead |
| Sortie | Sortie | −24 … +24 dB | 0 dB | Gain de sortie |
| | Suréchantillonnage | 1×, 2×, 4×, 8×, 16× | 4× | Facteur autour des étages non linéaires |
| | Caractère | Bande, Lampe triode, Transistor, Wavefolder | Bande | Courbe de saturation |
| | Clipper (style) | Hard, Soft, Analog | Soft | Courbe d'écrêtage : coupe nette, coude doux (tanh), coude doux avec légère coloration |
| | Gain staging auto | on / off | on | Vise −18 dBFS RMS en entrée, compense en sortie |
| | Caméra sur les basses | on / off | on | Rotation de la caméra à chaque basse |
| | Analyser | bouton | | Écoute 2 s, reconnaît la source, applique le preset |
| | Bypass | bouton | off | Bypass sans clic (crossfade court) |

Interaction des knobs : glisser verticalement (Maj = réglage fin), molette,
flèches du clavier, Début/Fin, double-clic = valeur par défaut. Les knobs de
gain sont bipolaires (l'arc part du centre).

## 5. Reconnaissance audio et presets

Classes proposées : **Kick / 808**, **Basse**, **Voix**, **Bus batterie**,
**Guitare / synthé**, **Master**. La classe la plus probable s'affiche avec sa
confiance dans le bandeau d'analyse ; elle choisit le preset, et les knobs glissent vers leurs
nouvelles valeurs en 0,7 s pour qu'on voie ce qui change.

Dans la maquette, la reconnaissance est une heuristique spectrale (rapport
graves / médiums / aigus, transitoires). Le plugin utilisera un petit
classifieur entraîné sur des descripteurs (MFCC, centroïde, crest factor,
flux spectral), exécuté hors du thread audio.

## 6. Engagements techniques affichés dans la barre d'état

Ces points concernent le DSP, pas l'interface ; ils sont listés ici parce que
la barre d'état les montre et qu'il faudra les tenir :

- **Latence 0** : suréchantillonnage par filtres polyphase IIR à phase
  minimale (pas de FIR à phase linéaire), aucun look-ahead.
- **SIMD** : traitement vectorisé (SSE2 / AVX2 / NEON) par blocs.
- **Multithread sans blocage** : le thread audio ne prend jamais de verrou ;
  analyse et UI communiquent avec lui par files SPSC sans verrou et
  paramètres atomiques.
- **Build** : CMake uniquement.

## 7. Choix techniques pour l'interface du plugin

À valider avant d'écrire du code :

- Rendu 3D en OpenGL 3.3 (ou Metal / Vulkan via une couche comme bgfx),
  dans un thread de rendu séparé du thread audio.
- Scène générée en code comme dans la maquette (pas de fichiers de modèles),
  graine fixe pour que le quartier soit toujours le même.
- L'UI lit niveaux et événements « coup de basse » depuis une file SPSC
  alimentée par le thread d'analyse ; elle n'appelle jamais le thread audio.

## 8. Questions ouvertes

1. Garder le nom **Twisted** ?
2. Dry/Wet : jour = dry et nuit = wet (comme la maquette), ou l'inverse ?
3. Ajouter un mode « vue libre » (orbite manuelle à la souris) en plus de l'orbite sur les basses ?
4. Liste des classes de reconnaissance et des presets à compléter.

## 9. Historique des décisions

- Saturation + Enhance fusionnés en **Elevate**.
- Stéréo + Space fusionnés en **Space**.
- Ajout du **Clipper** (3 styles) et du **Limiter**.
- Filtres (coupe-bas, coupe-haut, topologie) retirés.
- Panneau de reconnaissance remplacé par un bandeau d'analyse.
