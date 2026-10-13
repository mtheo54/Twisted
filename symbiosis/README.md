# SYMBIOSIS

Processeur VST3 de **transfert spectral à énergie conservée** et d'**alignement
vectoriel de phase**. Quand deux pistes se masquent (kick contre 808, voix contre
instrumentale), Symbiosis ne baisse pas le gain de la piste secondaire. Il retire
l'énergie de la zone masquée et la remet sur les partiels et harmoniques que cette
piste possède déjà. Sur la bande de conflit, il recale aussi la phase de la piste
secondaire sur celle de la piste maître, pour que la somme soit constructive
(cos Δφ → 1).

![Symbiosis en fonctionnement : 808 sur l'entrée principale, kick en sidechain](docs/img/symbiosis-live.png)

*Capture prise sous Linux (Xvfb) avec de l'audio réel : une 808 à 55 Hz sur l'entrée
principale, un kick sur le sidechain, Phase Link actif. Anneau rouge = esclave
redistribué, anneau orange = maître.*

## Routage

| Bus VST3 | Rôle |
|---|---|
| Entrée 0 (Main), stéréo ou mono | piste esclave x₂(t) : 808, pad, sample… |
| Entrée 1 (Aux, sidechain), stéréo ou mono | piste maître x₁(t) : kick, voix lead… |
| Sortie 0 (Main) | esclave restructuré x̃₂(t) |

Insérer Symbiosis sur la piste esclave, puis router la piste maître vers son entrée
sidechain. Sans sidechain, le plugin laisse passer le signal intact, retardé de la
latence annoncée.

**Latence : 2048 échantillons**, déclarés à l'hôte (`getLatencySamples`). Le DAW
compense automatiquement. Le bypass sort le signal sec retardé d'autant, sans clic.

## Compiler

Prérequis : **CMake ≥ 3.25** (exigé par le SDK VST3 3.8) et un compilateur C++20 :
Visual Studio 2022/2026 sous Windows, les outils Xcode sous macOS.

Le SDK Steinberg officiel (MIT depuis la 3.8, avec VSTGUI4 sous BSD) est téléchargé
automatiquement à la configuration (tag `v3.8.1_build_84`). Pour utiliser un SDK
local, passer `-DVST3_SDK_ROOT=/chemin/vers/vst3sdk` ou le placer dans
`symbiosis/vst3sdk`.

Windows : **Visual Studio 2026** (Community suffit) avec la charge de travail
« Développement Desktop en C++ ». Dans « Developer PowerShell for VS 2026 » :

```powershell
cd $HOME
git clone -b claude/determined-wright-6vzgq5 https://github.com/mtheo54/Twisted.git
cd Twisted
cmake -S symbiosis -B build-symbiosis -G "Visual Studio 18 2026" -A x64
cmake --build build-symbiosis --config Release
```

Si CMake répond « could not create named generator », remplace
`-G "Visual Studio 18 2026" -A x64` par `-G Ninja -DCMAKE_BUILD_TYPE=Release`.
Supprime le dossier `build-symbiosis` avant de relancer.

Résultat dans `build-symbiosis\plugins\` : `Symbiosis.vst3\` (un dossier, à copier en
entier). Installation, depuis un terminal lancé en administrateur :

```powershell
cmake --install build-symbiosis --config Release --prefix "C:/Program Files/Common Files"
```

La commande copie `Symbiosis.vst3` dans `C:\Program Files\Common Files\VST3\`.

macOS (binaire universel Apple Silicon + Intel) :

```bash
cmake -S symbiosis -B build-symbiosis -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build-symbiosis --parallel
cmake --install build-symbiosis --prefix ~/Library/Audio/Plug-Ins
```

Après chaque compilation, le **validateur Steinberg** tourne tout seul sur le plugin.

Linux (test uniquement) : le SDK demande les paquets de développement xcb, xkbcommon,
cairo, pango, fontconfig, freetype et gtkmm-3.0 (voir `.github/workflows/build.yml`).

Options CMake :

| Option | Défaut | Effet |
|---|---|---|
| `SYMBIOSIS_BUILD_PLUGIN` | ON | plugin VST3 (demande le SDK) |
| `SYMBIOSIS_BUILD_TESTS` | ON | tests DSP, sans le SDK (`ctest --test-dir build-symbiosis -C Release`) |
| `SYMBIOSIS_BUILD_SDK_HOSTS` | OFF | compile aussi `editorhost` du SDK, pour ouvrir l'interface sans DAW |
| `VST3_SDK_ROOT` | `symbiosis/vst3sdk` | SDK local ; téléchargé s'il est absent |

## Paramètres

| ID | Nom | Plage | Défaut | Rôle |
|---|---|---|---|---|
| `kParamPullId` | Pull | 0 – 1 | 0.68 | γ : énergie extraite de la zone masquée, E = min(A₂², γ·A₁²) |
| `kParamHarmonicsId` | Harmonics | 0 – 1 | 0.42 | η : part de l'énergie extraite réinjectée sur les partiels |
| `kParamInertiaId` | Inertia | 1 – 150 ms | 35 ms | attaque du transfert |
| `kParamReactionId` | Reaction | 5 – 500 ms | 80 ms | relâchement du transfert |
| `kParamTrackingId` | Tracking | 0 – 1 | 0.75 | adhérence aux partiels réels (seuil de détection et largeur des bassins) |
| `kParamTransShieldId` | Trans-Shield | 0 – 1 | 0.20 | protection des transitoires |
| `kParamPhaseLinkId` | Phase Link | Off / On | On | alignement vectoriel constructif |
| `kParamDeltaSoloId` | Delta Solo | Off / On | Off | écoute de la différence x̃₂ − x₂ |
| `kParamTargetModeId` | Target Mode | Exact / Harmonic | Exact | gabarit de redistribution |
| `kParamOversamplingId` | Oversampling | Off / 2x / 4x | 4x | suréchantillonnage spectral (voir plus bas) |
| `kParamBypassId` | Bypass | Off / On | Off | bypass hôte (`kIsBypass`), compensé en latence |

Double-clic, ou Ctrl-clic (Cmd-clic sur Mac), sur un potentiomètre : retour à la valeur par défaut.

## Le traitement, trame par trame

STFT : N = 2048, H = 512 (recouvrement 75 %), fenêtre de Hann périodique (COLA).
L'analyse est liée en stéréo : les décisions sont prises sur la puissance
L + R, puis appliquées aux deux canaux, ce qui garde l'image stable. Un esclave
mono avec un sidechain stéréo analyse le mixage mono du maître, (L + R)/2.

1. **Énergie masquée** : ρ[k] = min(1, γ·|X₁|²/|X₂|²), lissé par Inertia
   (montée) et Reaction (descente). Le résidu vaut |X₂|²·(1 − ρ).
2. **Redistribution**, avec un budget de η·Σ ρ|X₂|² :
   - *Exact Peaks* : crêtes locales de |X₂|² au-dessus du seuil Tracking, en
     excluant les crêtes elles-mêmes masquées (ρ ≥ 0,5). Poids W = |X₂[m]|²/Σ|X₂[mⱼ]|².
   - *Harmonic Series* : f₀ par YIN, lissée (un saut de plus de 6 % est une nouvelle
     note) et maintenue 10 sauts si YIN décroche un instant. Elle n'est affinée sur la
     crête spectrale que si le lobe de la fondamentale est résolu : sur une 808 à
     30-80 Hz, une trame de 2048 échantillons ne contient qu'une ou deux périodes et la
     crête dérive (±1,7 Hz mesurés), alors que YIN reste juste. Cibles 2f₀, 3f₀, 4f₀
     avec des poids (1/h)/Σ(1/i). Une cible est retenue si le maître ne la masquerait
     pas de nouveau une fois l'énergie déposée. Sans f₀, le mode se replie sur Exact
     Peaks pour la trame ; avec une f₀, il n'y bascule jamais (sinon l'énergie
     arroserait les autres partiels par à-coups).
   - Un partiel est relevé d'un seul bloc : tous les bins de son bassin (de vallée
     à vallée, limité à ±Δ bins par Tracking) reçoivent le même gain
     g = √(1 + ΔE/R). L'énergie arrive donc sur le partiel sans déformer son lobe.
     Un partiel réel proche de h·f₀ ne reçoit l'énergie que s'il lui suffit de
     +6 dB : au-delà, on gonflerait surtout le bruit de son bassin (un son
     « caoutchouteux »). Sinon l'harmonique est synthétisée : un lobe de Hann calculé
     exactement, porté par un oscillateur continu par harmonique (phase avancée de
     h·2π·f₀·H/fs à chaque saut, ramenée vers h·ψ₀ quand la fondamentale est résolue),
     dont l'amplitude est lissée : l'harmonique monte et s'éteint en douceur.
   - Le budget (énergie extraite) est lissé sur ~3 sauts : la puissance d'un partiel
     grave mesurée sur une seule trame varie d'environ 1 dB selon sa phase, ce qui
     modulait en amplitude les partiels relevés.
   - Avec η = 1, Σ|Y|² = Σ|X₂|² : les tests mesurent un écart relatif de 10⁻⁸.
3. **Phase Link** : φ₂ ← φ₂ + β·wrap(φ₁ − φ₂). β vaut 1 sur la bande de conflit
   (maître et esclave à moins de 20 dB l'un de l'autre), décroît jusqu'à 0 à 40 dB
   d'écart, et suit Inertia / Reaction. Les bins d'un bassin tournent ensemble, de
   l'angle mesuré sur la crête (*spectral phase locking*).
   - La rotation est **liée en stéréo** : un seul angle par bin, mesuré sur
     l'interspectre Σ X₁·conj(X₂) des deux canaux, appliqué à L et à R. L'écart de
     phase entre L et R, donc la largeur de l'image, est conservé.
   - La rotation est un état par bin qui rejoint sa cible par le plus court arc,
     **au plus 30° par saut**, et jamais plus vite que ce qui désaccorde le partiel de
     2 % (une rotation qui varie de d rad par saut décale la fréquence de
     d/(2π·H/fs) Hz : 30° par saut, c'était ±7 Hz sur une 808 à 33 Hz). Avec un maître désaccordé et β < 1, wrap(φ₁ − φ₂)
     saute de +π à −π ; sans cette limite, deux trames voisines tourneraient en
     sens opposés et l'overlap-add s'annulerait (trous de −10 dB, craquements).
4. **Trans-Shield** : une attaque dans le saut le plus récent de la trame est
   connue 2048 échantillons avant d'être entendue. ρ et β sont réduits sur les 4
   trames qui la contiennent, ce qui évite le pré-écho de la STFT.
5. iFFT, fenêtre de synthèse w/1,5 (Σw² = 1,5 sur 4 sauts), overlap-add.

Changer l'Oversampling en cours de lecture ré-échantillonne l'état lissé (ρ, β,
rotations) vers la nouvelle résolution, sans relâcher puis reprendre le transfert.
Bypass et Delta Solo passent par une rampe de 20 ms (pas de clic).

Avec Pull = 0 et Phase Link coupé, la sortie est exactement l'entrée retardée de
2048 échantillons (reconstruction parfaite, vérifiée par les tests).

### Écarts assumés par rapport au cahier des charges

- **Oversampling** : c'est un suréchantillonnage *spectral*, pas temporel. La trame
  d'analyse est complétée de zéros jusqu'à 2N ou 4N avant la FFT. La chaîne STFT
  est linéaire et ne contient aucun étage non linéaire, donc un suréchantillonnage
  temporel polyphase n'éviterait aucun repliement. Il ajouterait de la latence et
  élargirait les bins en Hz. Le zero-padding rend plus précis le repérage des
  crêtes, les bassins, l'estimation de f₀ et le placement des harmoniques
  synthétisées.
- **Plafond de gain par partiel : +24 dB.** Un partiel très faible ne peut pas
  absorber une énergie démesurée. La conservation reste exacte tant que le plafond
  n'est pas atteint.
- **Bypass** ajouté (paramètre hôte standard, compensé en latence).
- Les paramètres sont appliqués à chaque saut (10,7 ms à 48 kHz), pas à
  l'échantillon près. Les transitions sont fondues par l'overlap-add.
- Traitement en 32 bits flottants uniquement (le validateur le signale).
- CMake : le SDK 3.8 exige CMake 3.25 (le cahier demandait 3.22), et la fonction
  `smtg_initialize_plugin_package` n'existe pas dans le SDK. Le paquet est décrit par
  `smtg_add_vst3plugin`, `smtg_target_add_plugin_resources` (polices) et
  `smtg_target_set_bundle` (macOS).

## Temps réel

- `process()` n'alloue rien et ne prend aucun verrou. Tous les tampons (trames, FFT
  2048 / 4096 / 8192, tables trigonométriques, YIN) sont alloués dans
  `setupProcessing()`, et `setActive(true)` les remet à zéro.
- FTZ/DAZ (`_MM_SET_FLUSH_ZERO_MODE`, `_MM_SET_DENORMALS_ZERO_MODE`) sur x86, bit
  FZ du FPCR sur ARM64, pendant toute la durée de `process()`.
- Visualiseur : le thread audio publie au plus 60 images par seconde dans une
  « boîte aux lettres » partagée en mémoire avec le contrôleur (`hud_mailbox.h` :
  file SPSC sans verrou, dont l'identifiant arrive dans un seul `IMessage` à la
  connexion), que l'interface lit à 60 FPS. Ce chemin ne dépend d'aucun minuteur de
  l'hôte : le repli `IMessage` du SDK en utilise un, créé sur le thread qui active le
  plugin, et certains hôtes l'activent depuis un thread sans boucle de messages
  (interface vide dans FL Studio). L'API *Data Exchange* reste en secours (plugin
  ponté, autre processus) ; l'interface prend l'image la plus récente des deux.
- `setState()` peut arriver depuis le thread de l'interface pendant le traitement.
  Les valeurs sont donc atomiques et prises en compte par le `process()` suivant.
- Charge mesurée (un cœur, stéréo, 48 kHz) : 1,8 % (Off), 3,7 % (2x), 7,7 % (4x)
  en Exact. Le mode Harmonic coûte environ 2,5 points de plus (YIN).

## Interface

VSTGUI4, construite en code (pas de `.uidesc`) d'après la maquette « astrolabe » :
canevas de 860 × 560 affiché à 120 % (1032 × 672). Les éléments fixes sont calculés
une seule fois dans une image en mémoire, à la résolution de l'écran.

### Textures

Ce que le dessin vectoriel de `CDrawContext` rend mal (éclairage par pixel, biseaux,
flou, bloom) est pré-calculé hors ligne par `tools/generate_assets.py` (numpy +
Pillow, sortie déterministe) dans `resource/textures/`, puis copié dans
`<bundle>/Resources` par CMake :

| Texture | Contenu | Densité (px / unité de canevas) |
|---|---|---|
| `background.png` | papier marbré ivoire : fumée d'encre grise à domaine déformé, veinures, éclaboussures d'encre noire, grain du papier, bords gris clair et zones calmes derrière les faders et libellés | 2 |
| `astrolabe_ring.png` | anneau en acier bronzé / chrome noir : profil bombé, biseaux, rainures, brossage concentrique anisotrope, couronne de 180 dents, bande claire avec 46 glyphes noirs en relief (runes, zodiaque, planètes, étoiles) et graduation au degré, ombre portée | 2,4 |
| `knob_gem_smoke.png` | cristal fumé noir facetté (table octogonale, 16 facettes en étoile, 16 facettes de rondiste) : arêtes lumineuses, contraste entre facettes, réfraction simulée, étoile vue à travers la table ; **filmstrip de 16 images** sur une période de 45° de la taille | 3 |
| `knob_cap.png` | dôme d'acier noir tourné (HARMONICS), anneaux d'usinage concentriques | 3 |
| `knob_gloss.png` | brillance à lumière fixe : reflet de fenêtre courbé par le dôme, point spéculaire, lumière de bord neutre, assombrissement de Fresnel | 3 |
| `knob_bezel.png` | lunette moletée en acier sombre avec ombre portée | 3 |
| `plasma_a.png`, `plasma_b.png` | volutes en anneau pour l'Orbit Visualizer : fumée d'encre noire et brume rouge | 2 |

L'éditeur déclare la densité de chaque PNG à VSTGUI
(`IPlatformBitmap::setScaleFactor`) : les textures restent nettes au zoom de 120 % et
sur écran HiDPI. S'il en manque une, un rendu vectoriel simplifié prend le relais.

Pour régénérer : `python3 symbiosis/tools/generate_assets.py`.

### Rendu

- **Fond** : papier marbré ivoire et fumée d'encre, constellations et arabesques
  à l'encre de Chine (halo rouge près de PULL), plaque d'aluminium brossé en bas
  avec vis sombres et motifs art déco. Palette : ivoire, encre anthracite, acier
  bronzé, rouge sang pour les accents.
- **Astrolabe central (Orbit Visualizer)** : bande zodiacale, couronne dentée,
  anneau d'acier bronzé gravé, rivets, boule sommitale, deux groupes d'engrenages,
  plaque « ORBIT VISUALIZER ». À l'intérieur, les deux spectres tressés en anneaux
  d'encre, en miroir (graves en bas, aigus en haut, le rayon donne le niveau) :
  esclave redistribué en rouge sang, plus vif à gauche, maître en encre noire, plus
  vif à droite. Les erreurs de phase font vriller le tracé rouge.
  - **Plasma** : fumée d'encre dominante et brume rouge en fondu enchaîné lent, dont l'opacité suit
    le signal, et un remplissage gazeux entre le médaillon et l'onde.
  - **Bloom** : passe large translucide (16 px, alpha 0,25, rouge sombre ou gris) et corps
    saturé (6,5 px), rendus dans un tampon hors écran à demi-résolution et rafraîchis
    une image sur deux. Ensuite, à pleine résolution et à chaque image : trait net
    saturé, puis cœur sombre qui garde le trait net sur le disque clair.
- **Médaillon** : **ACTIVE INFLUENCE** = moyenne de (1 + cos(φ₁ − φ̃₂))/2 sur la
  bande de conflit, pondérée par l'énergie (100 % = sommation entièrement
  constructive).
- **Potentiomètres** PULL (cristal fumé noir facetté, graduations et lueur rouges)
  et HARMONICS (dôme d'acier noir, graduations à l'encre). La pierre
  tourne avec la valeur (image du filmstrip choisie selon l'angle, chaque image
  éclairée hors ligne sous la même lumière fixe), tandis que la brillance et la
  lunette restent immobiles. Anneau de graduations en vectoriel. **Glissières** : fente noire à lueur rouge
  (INERTIA, TRANS SHIELD) ou blanche (REACTION, DYNAMIC TRACKING), curseur rubis dans
  un cadre d'acier sombre. Glisser verticalement (Maj = réglage fin),
  molette, double-clic ou Ctrl-clic (Cmd-clic sur Mac) pour la valeur par défaut.
- **Menus** : `[SIDECHAIN: MASTER]` indique si le sidechain reçoit un signal (sinon
  `NONE`) ; `ROUTAGE` = Target Mode ; `OVERSAMPLING` = suréchantillonnage
  (comme les boutons OS 1X / 2X / 4X) ; `PRESET` = presets d'usine (DEFAULT, KICK / 808, VOCAL / BEAT, GENTLE, INTENSE, ou CUSTOM).
- **Douze boutons à LED** (corps noir, LED rouge ou blanche) :

  | | colonne 1 | colonne 2 | colonne 3 |
  |---|---|---|---|
  | gauche, rang 1 | PHASE LINK | SOLO DELTA | BYPASS |
  | gauche, rang 2 | EXACT | HARMONIC | FREEZE (fige l'affichage) |
  | droite, rang 1 | OS 1X | OS 2X | OS 4X |
  | droite, rang 2 | MASTER | SLAVE | INPUT (tracés affichés) |

- **En-tête** : titre SYMBIOSIS et mention « ONIRIQUE × ABRASION » juste dessous, vumètres de la sortie et du maître (dB RMS), roue dentée (version,
  latence), bouton marche = bypass. **Oscilloscopes** en bas : sortie (rouge) et
  maître (encre noire), sur fond clair. Vumètres à segments gris, crête rouge.
- Police **Montserrat** embarquée dans le paquet (`Resources/Fonts`, licence SIL OFL).

### Coût à 60 FPS

Tout le dessin se fait sur le thread de l'interface, qui ne touche que l'état du HUD
lu dans la file SPSC. Aucune rotation de bitmap à l'exécution : le backend Cairo de
VSTGUI découpe mal les bitmaps tournés. Le mouvement vient donc des images du
filmstrip et des fondus. Médaillon, afficheur et plasma sont rendus une fois dans des
caches à la résolution de l'écran (l'afficheur est refait quand son texte change).
Les ondes sont des polylignes de 192 sommets, tracées avec des jointures biseautées.

Mesure dans le pire cas, rendu 100 % logiciel (Cairo sous Xvfb) : Orbit Visualizer
**6,9 ms par image** (22,7 ms avant ces optimisations). Direct2D sous Windows et
CoreGraphics sous macOS sont nettement plus rapides.

Les libellés de la maquette qui ne correspondaient à aucune fonction
(« COMPLIANCE », « PIOLAGE », « LOCK MODE », « PHASE MIRC », « PHASE LRRS »,
« SRAGON », « RULT ») ont été remplacés par les commandes réelles ci-dessus, aux
mêmes emplacements.

## Vérifications

- Tests DSP (`tests/dsp_test.cpp`, également sous ASan et UBSan) : FFT contre DFT
  directe, file SPSC entre deux threads, YIN à ±1 %, reconstruction parfaite en
  1x/2x/4x, latence exacte de 2048 échantillons, conservation de l'énergie
  (10⁻⁸) et RMS préservée à ±0,05 dB, Phase Link (des phases opposées qui
  s'annulaient donnent |x₁ + x̃₂| = A₁ + A₂), série harmonique en 1/h (rapport
  2f₀/4f₀ = √2), Trans-Shield sans pré-écho, absence de NaN, Delta Solo, bypass.
  Tests d'artefacts : maître désaccordé sous PHASE LINK sans trou de niveau, image
  stéréo conservée, changement d'Oversampling sans saut, Delta Solo sans clic,
  sidechain stéréo sur esclave mono, f₀ stable sur une 808 à 33 Hz sous des kicks.
  Ces tests échouent sur la version du moteur qui précédait leur correctif.
- Validateur Steinberg : 47 tests réussis, 0 échec (stéréo et mono, bypass,
  tailles de bloc variables, traitement sur un autre thread, état).
- Interface ouverte dans `editorhost` du SDK sous Linux, avec de l'audio réel
  traversant le processeur (capture ci-dessus), et superposée à la maquette pour
  vérifier l'emplacement de chaque élément.
- Tests DSP également compilés avec libc++ (la bibliothèque standard de macOS).
- **Pas encore essayé dans un DAW sous Windows ou macOS.** La CI GitHub compile et
  valide le plugin sur Windows, macOS et Linux à chaque push.

## Arborescence

```
symbiosis/
├── CMakeLists.txt
├── source/
│   ├── version.h
│   ├── symbiosis_cids.h           FUID, ID et plages des paramètres
│   ├── symbiosis_processor.*      AudioEffect : bus, état, STFT, flux du HUD
│   ├── symbiosis_controller.*     EditController + IDataExchangeReceiver
│   ├── hud_mailbox.h              boîte aux lettres du visualiseur (même processus)
│   ├── symbiosis_entry.cpp        fabrique VST3
│   ├── dsp/
│   │   ├── fft_engine.h           FFT réelle radix-2, sans dépendance
│   │   ├── ring_buffer.h          FIFO SPSC sans verrou + historique circulaire
│   │   ├── spectral_engine.h      transfert, phase, harmoniques, Trans-Shield
│   │   └── yin_pitch.h            YIN temps réel
│   └── ui/
│       └── symbiosis_editor.*     VSTGUI4 : papier, astrolabe, cristal, LED, menus
├── resource/                      polices (OFL), textures PNG, ressource de version Windows
├── tools/generate_assets.py       générateur des textures (numpy + Pillow)
├── tests/dsp_test.cpp
└── docs/img/
```
