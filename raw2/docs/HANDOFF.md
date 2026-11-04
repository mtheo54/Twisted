# RAW 2 — dossier de passation pour Claude Code

Marque : **ABRASION** · Plateforme de vente : **ONIRIQUE** · Plugin : **RAW 2** (VST3, suite de RAW)

Ce dossier décrit tout ce qui a été conçu dans la maquette interactive « RAW 2 — maquette » (un composant web de 1280 × 960 px). Le but : porter ça en vrai plugin VST3 en C++20, sans JUCE.

> La maquette est une simulation visuelle. Les animations et les valeurs affichées sont réelles, mais **le son n'est pas un vrai moteur audio** : les oscilloscopes et le spectre sont dessinés à partir de formules. La section 9 sépare ce qui est spécifié de ce qui reste à concevoir.

## 0. Contenu du dossier

| Fichier | Rôle |
|---|---|
| `HANDOFF.md` | ce brief |
| `themes.json` | les 6 thèmes (tous les tokens de couleur) |
| `presets.json` | les 72 presets d'usine, paramètres nommés |
| `reference/Main.dc.html` | code de la maquette complète (référence de comportement, ~2400 lignes, JS lisible) |
| `reference/Presets.dc.html` | maquette de l'écran « banque de presets » (760 × 900) |

Comment lire `Main.dc.html` : tout l'état vit dans `this.S`. La méthode `renderVals()` reconstruit l'interface à chaque image. Les formules utiles sont dans `frame()`, `renderVals()`, `views()`, `wtView()`, `nextGen()`, `genShape()`, `wtSample()`, `applyPreset()`. **Ne pas recopier le code JS** : il sert à comprendre le comportement et les constantes.

## 1. Contraintes du projet (à respecter)

- Windows, Visual Studio 2026, CMake, compilation en PowerShell. Test dans FL Studio.
- C++20, **sans JUCE** et sans framework tiers. Seul le SDK VST3 de Steinberg est admis (il est nécessaire pour faire un VST3).
- Interface dessinée sur mesure (style matériel / terminal). Pas de widgets système.
- Interface d'abord, puis le code audio, comme sur les autres plugins du même auteur.

## 2. Fenêtre et organisation générale

- Taille de référence **1280 × 960**. Prévoir une mise à l'échelle homothétique (zoom 75 % à 150 %) plutôt qu'un redimensionnement libre.
- Plaque avec marges de 24 px, quatre repères « coins » à 10 px du bord, et la mention centrée en bas : `DISTRIBUÉ SUR ONIRIQUE` (ONIRIQUE en gras).
- **Header** (hauteur 40 px), de gauche à droite :
  1. Logo : carré noir 40 px avec un carré orange, puis le mot `RAW2` (le « 2 » en couleur d'accent) surmonté de `ABRASION` en 9 px très espacé.
  2. Languette fine de choix du thème (voir §4).
  3. Onglets `SON` · `VIE` · `MODULES` · `MIX IA` · `WAVETABLE`.
  4. Boîte CPU (`CPU: 1.2%` + barre, et `64 SAMPLES` cliquable pour changer la taille de buffer 32 → 64 → 128 → 256) et bouton `PANIC`.
  5. Sélecteur de preset : `<`, bouton nom `01 · verre givré ▾` (ouvre la fenêtre de choix), `>`, puis `ENREGISTRER`.
- **Appui long (600 ms) sur le logo** : bascule le mode oscilloscope X-Y (§7.9), avec un effet visuel volontairement « choquant » (voir §7.9).

## 3. Les cinq pages

### SON (page principale)
Rangée 1, trois cartes : `OSC A` · `VIE` (grille 36×16, centrale, 1,35× plus large) · `OSC B`.
- Chaque `OSC` : en-tête avec `OCT` et `ST` (cases cliquables, molette), badge `WAVETABLE` (nom de la table, ouvre l'éditeur), écran noir avec la forme d'onde, deux boutons rotatifs `POSITION` et `NIVEAU`.
- `VIE` : grille de cellules, légende d'âge (6 teintes), bouton `CIBLE : FILTRE ▾`, boutons `RESET` / `GEL` / `SEED ▾`, trois boutons `VITESSE` / `NAISSANCE` / `QUANTITÉ`, interrupteur `HZ | BPM`.
Rangée 2 : bande de trois modules `HARMONIQUES` / `ESPACE` / `TEXTURE` (voyant, curseur de profondeur −100 … +100 %).
Rangée 3 : carte `SORTIE` (scope + spectre 48 bandes, clavier de 14 touches blanches C3–B4, bouton `MASTER`) et carte noire `PROPOSITION (MIX · IA)`.

### VIE
Automate plein écran 64×58 + panneau `CONTRÔLES` : pinceau (dessiner / gomme / motif), taille 1–3, motifs (Glider, Pulsar, Gosper Gun, Spaceship, R-pentomino), snapshots (8 emplacements, `CAPT.`, `SYNC`, `AUTO`, `VIDER`), réglages naissance / vitesse, règle `B3/S23` ou `B36/S23`, boutons `RESET` `GEL` `EFFACER` `INJECTER`.

### MODULES
Matrice de routage 6 sources × 8 destinations (clic = relier, re-clic = couper, taille du point = dosage), panneau `LIAISON` (curseur de dosage −100 … +100, `COUPER`, `TOUT EFFACER`, `ALÉATOIRE`), liste `PATCHS ACTIFS`, mini-scope `SORTIE` avec la valeur de chaque destination.
- Sources : VIE, LFO 1, LFO 2, ENV, CLAVIER, ALÉA.
- Destinations : PITCH A, PITCH B, POS. A, POS. B, CUTOFF, RÉSO, GRAIN, MASTER.

### MIX IA
Analyseur 48 bandes (20 Hz – 20 kHz), lectures `NIVEAU` / `CRÊTE` / `DYNAMIQUE` / `CORRÉLATION`, courbe d'EQ cumulée, bandes graves / médiums / aigus avec cible, image stéréo, compresseur et limiteur (bypass), score du mix, assistant (analyse simulée de 4,5 s, journal en machine à écrire, 3 suggestions maximum par analyse), historique `APPLIQUÉ` / `IGNORÉ`.

### WAVETABLE
Voir §7.5.

## 4. Système de design

- **Tokens** : `themes.json`. Six thèmes : `alu` (défaut), `nothing`, `titane`, `crt`, `ultraviolet`, `onirique`. Chacun donne chassis, carte, lignes, encre, accent, écran (`scr*`), ondes (`waveA/waveB`), barres, touches du clavier, etc. Changement de thème à la volée, sans redémarrage.
- **Choix du thème** : une languette fine sans nom à gauche des onglets. Elle montre la couleur d'accent actuelle et une flèche. Au clic, un panneau se déroule en 5 paliers (160 ms) avec 6 pastilles de 26 px et le nom du thème. Il se ferme au choix d'un thème, au reclic, au clic dehors ou avec Échap.
- **Polices** : `JetBrains Mono` pour le texte, `Silkscreen` pour les titres. En C++, les embarquer dans la DLL (pas de dépendance à des polices installées).
- **Formes** : angles secs, rayon 2 px, bordures 1 px. Boutons avec ombre dure de 2 px. Au clic : le bouton se déplace de (2, 2) et l'ombre disparaît. Chaque carte porte deux petits « + » aux coins opposés.
- **Animation mécanique obligatoire** : aucun easing (ni élastique, ni ease-in-out). Tout est par paliers (pas d'interpolation douce), 60 FPS pour les visualiseurs audio. Le curseur clignote à 1 Hz en tout-ou-rien. Les cellules de la vie « s'éteignent » avec une rémanence de phosphore de 80 ms.
- **Couleur des cellules de la vie** : 6 teintes selon l'âge. La couleur devient plus vive et plus saturée avec l'âge, on ne change pas le caractère.

## 5. Paramètres globaux (modèle)

| Paramètre | Plage | Affichage / mapping |
|---|---|---|
| `aPos`, `bPos` | 0 … 1 | position dans la table, en % (frame = pos × 63) |
| `aLvl`, `bLvl` | 0 … 1 | dB = −(1 − v) × 18,2 ; 0 dB en haut |
| `master` | 0 … 1 | idem dB |
| oct A / B | −2 … +2 | affichage `+1 OCT` |
| st A / B | −12 … +12 | affichage `+3 ST` |
| `speed` (vie) | 0 … 1 | Hz = 0,5 × 40^v ; en BPM : × 60 |
| `birth` | 0 … 1 | densité de naissance au `RESET` |
| `qty` | 0 … 1 | quantité injectée |
| `m1` HARMONIQUES | −100 … +100 | profondeur |
| `m2` ESPACE | −100 … +100 | profondeur |
| `m3` TEXTURE | 0 … 100 | profondeur |
| règle de la vie | B3/S23 ou B36/S23 | |
| matrice | 6×8 | dosage −100 … +100 par liaison |

Interactions : boutons rotatifs = glisser verticalement (160 px pour la course complète). Curseurs = glisser horizontalement. Molette sur `OCT` / `ST` = ±1 cran. Les cibles de la proposition IA s'appliquent par paliers (jamais en continu).

## 6. Presets

- 72 presets d'usine dans `presets.json` : 18 lead, 16 basse, 14 pad, 10 fx, 8 pluck, 6 arp. Les 4 premiers sont ceux d'origine (verre givré, neige lente, sous la glace, brume perso).
- Chaque preset donne : forme de table de A et de B (`shape` + morph de début et de fin sur les 64 frames), positions, niveaux, octave et demi-tons, paramètres de la vie, profondeurs des 3 modules, règle, master, catégorie et un tag de style.
- Styles visés : hyperpop, melodic trap, plugg, pop FR, y2k, dream pop, glitch, rage, lofi, ambient. Aucun nom d'artiste n'est utilisé dans les noms.
- Les basses sont réglées avec un master de 0,80 à 0,88 et un niveau d'osc principal de 0,84 à 0,96. C'est volontaire : certains presets de RAW sortaient trop bas. **À vérifier à l'oreille** une fois le moteur audio réel branché, car ces valeurs n'ont jamais été écoutées.
- Fenêtre de choix : recherche (nom, catégorie, style, sans accents), filtres `tous` / `basse` / `lead` / `pad` / `pluck` / `arp` / `fx`, un clic charge et laisse la fenêtre ouverte. Échap ferme.
- Presets utilisateur : fichiers `.raw` dans `Documents/RAW2 banque presets/`. La maquette `Presets.dc.html` montre l'écran (liste, onglet `mes presets`, champ de nom, bouton `ENREGISTRER`). Le format `.raw` reste à définir (JSON versionné conseillé).

## 7. Spécification fonctionnelle détaillée

### 7.1 Oscillateurs A et B (wavetable)
- Une **table** = 64 frames × 256 points (flottants, −1 … +1). Lecture par interpolation linéaire entre points, puis entre les deux frames voisines selon `pos`. `genShape()` donne les formes de base.
- Formes générées : `sine` (FM selon le morph), `saw` (somme de partielles, 2 à 32), `square` (rapport cyclique 50 % → 8 %), `tri` (repliement), `complex` (7 partielles déphasées), `noise` (bruit lissé reproductible), `glass` (sinus + partielles qui apparaissent avec le morph), `metal` (saw vers square).
- Si aucune table n'est « montée » sur un oscillateur, un oscillateur additif de 8 partielles de secours est utilisé (simple repli de la maquette).
- Un WAV long déposé sur un oscillateur devient une table de 64 frames (§7.6).

### 7.2 La Vie (automate cellulaire)
- Un seul monde de 64 × 58 (page VIE) ; la page SON en montre la zone écoutée (36 × 16 au centre). Tore (les bords se rejoignent). NAISSANCE entretient la vie (naissances spontanées quand le monde s'éteint ou se fige).
- Règles : `B3/S23` (Conway) ou `B36/S23` (HighLife). La vitesse suit `speed`. `GEL` fige. Les motifs se posent au pinceau.
- L'âge de chaque cellule (en générations) pilote la couleur (6 stades).
- **Lien avec le son (à confirmer)** : la maquette utilise la densité vivante `lv` pour moduler les harmoniques (`m1`), le timbre et la position. Dans le vrai plugin, définir clairement quelles mesures de la grille (densité, centre de masse, nombre de naissances par pas) pilotent quelles destinations.

### 7.3 Modulation (page MODULES)
- Matrice de 6 sources vers 8 destinations avec dosage −100 … +100 par liaison. La maquette fait agir les liaisons sur la forme d'onde, la position, le pitch et le spectre.
- Sources dans la maquette : `VIE` (densité), LFO 1 (sinus 0,35 Hz) et LFO 2 (triangle 0,11 Hz), `ENV` (enveloppe de note), `CLAVIER` (note jouée) et `ALÉA`.

### 7.4 MIX IA (assistant)
- L'IA de mixage **propose, jamais n'impose** : rien ne change sans accord (`[ ACCEPTER ]` / `[ IGNORER ]`).
- Rôles de la proposition (menu `[ CIBLE : BASSE ▾ ]`) : AUTO (PAD), BASSE (20–250 Hz), LEAD (500 Hz–8 kHz), PAD (80 Hz–2 kHz), ARP / SEQ (200 Hz–4 kHz), TEXTURE (2–16 kHz). Chaque rôle met en évidence sa zone du spectre et propose 3 formulations uniques, avec effet typé (par exemple « Ajouter +2 dB sur l'osc A ? »). Machine à écrire à 20 ms par caractère.
- Analyse globale : 4,5 s de balayage, journal par étapes (`LECTURE`, `SPECTRE`, `DYNAMIQUE`, `RÔLE`, `SYNTHÈSE`), 3 suggestions au plus choisies dans un lot de 6 (bell/shelf EQ, compresseur 2:1, limiteur −1 dBFS), jamais deux fois la même.
- Dans le vrai plugin, la « petite IA neuronale audio » est une **idée à valider** (modèle léger, exécution locale, sans blocage du thread audio). La maquette ne contient aucun modèle.

### 7.5 Page WAVETABLE
- **Cascade** : les 64 frames en 3D isométrique, frame 1 devant et frame 64 au fond, les premières cachant celles du fond. La frame active est en accent, la position réelle (modulée) de l'oscillateur en pointillés. Vue alternative `GRILLE` 8×8 (un clic choisit la frame).
- **Éditeur 2D** : écran noir 256 points avec trois outils : `CRAYON` (tracé libre interpolé), `LISSAGE` (lissage local à la souris), `DISTORSION` (repliement harmonique local). Les frames voisines sont en pointillés, le spectre H1–H32 s'affiche en dessous. `AUTO-NORM` normalise après chaque trait.
- **Panneau** : curseurs `MORPH · POSITION` (cran par frame, choisit la frame et la position de l'osc), `LISSAGE`, `DISTORSION HARM.`, `LISSAGE ENTRE FRAMES`, chacun avec son bouton d'application (`INTERP. 01→64`, `LISSER`, `DISTORDRE`, `FONDRE`).
- **Préréglages de formes** : Sine, Saw, Square, Triangle, Complex, Noise, appliqués à la `FRAME` ou à la `TABLE` entière (avec morph progressif sur 64 frames).
- **Outils** : normaliser, dupliquer, inverser, miroir, copier, coller, annuler (12 niveaux), RAZ.
- **Import / export** : zone `> DROP WAV TO LOAD` en **glisser-déposer uniquement** (jamais d'explorateur au clic). Import sur la frame active ou la table entière. Export `.wav` mono 16 bits : 64 frames × 2048 points (compatible Serum).
- Bouton `MONTER SUR OSC A / B` pour utiliser la table de l'éditeur sur l'oscillateur.

### 7.6 Import de WAV sur un oscillateur
- Glisser-déposer sur l'écran de l'osc (clic = rien). Lecture PCM 8/16/24/32 bits et float32.
- Fichier de plus de 4096 échantillons : découpé en frames de 2048 points (jusqu'à 64), rééchantillonnées à 256, normalisées, complétées par interpolation jusqu'à 64. Fichier plus court : cycle unique (fenêtre d'énergie maximale, DC et rampe retirés, normalisé) dupliqué sur 64 frames.
- Badge `nom.wav [ x ]` : cliquer sur `[ x ]` retire la table.

### 7.7 Clavier
14 touches blanches (C3–B4), 10 noires. Une note jouée multiplie la fréquence de lecture de la table par 2^((note − 12) / 12) dans la maquette, avec une pulsation visuelle à chaque note.

### 7.8 Snapshots de la vie
8 emplacements (grille + âge + génération), `CAPT.` capture, clic rappelle, `SYNC` applique à la génération suivante, `AUTO` capture toutes les 16 générations, `VIDER` efface.

### 7.9 Mode X-Y (appui long sur le logo)
- OSC A et B : dessinent chaque oscillateur contre lui-même retardé d'un quart de période (figure de Lissajous). SORTIE : fA contre fB.
- L'appui long montre : barre de progression par paliers sous le logo, logo qui grossit, puis à 600 ms un effet plein écran (secousse de la page, stroboscope, barres de glitch, grand texte). À remplacer par une version plus sobre si l'effet gêne en vrai.

### 7.10 CPU, buffer et PANIC
- `CPU` = charge (simulée dans la maquette, à mesurer en vrai ; affichée à 0,1 % près, mise à jour toutes les 500 ms par paliers). `SAMPLES` = taille de buffer, clic = 32 → 64 → 128 → 256.
- `PANIC` : coupe toutes les notes, vide les cibles, les fondus et les buffers du spectre, annule l'analyse en cours, affiche `RESET` 700 ms.

## 8. Architecture C++ proposée

1. **Cœur audio** (`raw2_dsp`) : synthé polyphonique (voix = 2 oscillateurs wavetable + enveloppes + filtre), moteur de la vie sur un thread à part avec échange sans verrou vers l'audio, matrice de modulation à sources/destinations indexées. Aucune allocation ni verrou dans `process()`.
2. **Paramètres** : liste centrale unique des paramètres (id stable, plage, mapping dB, texte). La même liste sert à VST3, aux presets et à l'automation.
3. **UI** (`raw2_ui`) : rendu 2D sur mesure (Direct2D/DirectWrite sur Windows en premier), arbre de composants simple (carte, bouton, bouton rotatif, curseur, grille, scope), gestion souris, molette, glisser-déposer de fichiers. Boucle de rendu à 60 FPS, **sans animation douce**.
4. **VST3** (`raw2_vst3`) : usine de composants, contrôleur, processeur, éditeur (vue HWND). Pas de dépendance autre que le SDK Steinberg.
5. **Fichiers** : presets `.raw` (JSON versionné), import / export WAV maison (aucune bibliothèque).
6. **CMake** : une cible par module, cible VST3 finale qui déploie dans `%CommonProgramFiles%\VST3`.

## 9. Ce qui est spécifié et ce qui reste à concevoir

Spécifié par la maquette : toute l'interface, le comportement visuel, les paramètres et leurs plages, les presets (valeurs), les thèmes, les interactions.

À concevoir avec l'auteur (décisions ouvertes) :
1. Le **vrai lien entre la vie et le son** (quelles mesures pilotent quoi).
2. Le **filtre** (type, pente, résonance) : `CUTOFF` et `RÉSO` n'existent dans la maquette que comme destinations de modulation.
3. Les **enveloppes** (ADSR) et les **LFO** : seuls des modèles visuels existent.
4. Les modules annoncés par l'auteur : **spatialisation vivante** et **texture granulaire / spectrale** (voir `HARMONIQUES`, `ESPACE`, `TEXTURE`). La maquette n'a que leur profondeur.
5. La **petite IA audio** de mixage : à valider (taille du modèle, exécution locale, données d'entraînement). Sans elle, garder une analyse spectrale classique avec règles.
6. Format `.raw` des presets utilisateur et gestion de leur dossier.
7. Mise à l'échelle et accessibilité (clavier, contraste) de l'interface.
8. Écoute des 72 presets et réglage fin du volume des basses.

## 10. Ordre de travail conseillé

1. Squelette CMake + VST3 vide qui se charge dans FL Studio.
2. Fenêtre, thèmes, header et onglets (tout l'habillage, sans audio).
3. Page SON : cartes, boutons rotatifs, scopes (données factices), clavier.
4. Moteur wavetable monophonique + écran des oscillateurs réels.
5. Vie (SON puis VIE), snapshots.
6. Modulation, page MODULES.
7. Page WAVETABLE (éditeur, import, export).
8. Presets (chargement, fenêtre de choix, enregistrement).
9. MIX IA (analyse + propositions, puis idée neuronale à valider).
10. Optimisation, tests dans FL Studio, vérification du volume des presets.
