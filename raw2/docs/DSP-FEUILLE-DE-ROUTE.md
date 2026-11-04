# RAW 2 : audit DSP, architecture cible et feuille de route

Ce document répond au cahier des charges « qualité sonore irréprochable et CPU optimisé ». Il part
du code tel qu'il est aujourd'hui, mesuré, et non d'un plugin imaginaire. Tous les chiffres ont été
mesurés sur cette version (Linux, `-O2`, un cœur, 48 kHz).

## 0. Mise au point : ce qu'est RAW 2 aujourd'hui

Le cahier des charges décrit un **multi-effet modulaire** avec un éditeur de courbes (splines),
des suiveurs d'enveloppe, des macros et des modulations en glisser-déposer. RAW 2 est autre chose :

- un **synthé wavetable polyphonique** : 8 voix, 2 oscillateurs par voix, 64 frames de 2048
  échantillons, 14 niveaux de mipmap ;
- par voix, un filtre SVF 12 dB/oct, un drive, deux enveloppes et deux LFO ;
- une matrice de modulation 6 × 8 et une « VIE » (automate cellulaire) qui module le son ;
- un **rack de 10 effets** réordonnables (page EFX) et une chaîne de mastering (MIX IA : EQ,
  compresseur, limiteur).

L'éditeur de courbes, les suiveurs d'enveloppe, les macros et le glisser-déposer de modulation
**n'existent pas encore**. Ils sont traités plus bas comme des chantiers (§ 4, P7). Le reste du
cahier des charges s'applique bien à RAW 2 : la distorsion, les filtres, la dynamique, les délais,
la réverbération, le gain, le temps réel et le SIMD.

**Mesure CPU actuelle** :

| Cas | CPU (un cœur) |
|---|---|
| 8 voix tenues, moyenne des 72 presets | **3,1 %** |
| Pire preset (dream 3am) | 4,6 % |
| Les 10 effets actifs en même temps | 3,8 à 5,0 % |

**Conséquence stratégique : le CPU n'est pas le goulot.** On a une marge d'environ ×10 avant de
gêner un projet chargé. La priorité est donc de **dépenser du CPU pour la qualité** (ADAA,
suréchantillonnage, interpolations plus fines, réverbération plus dense). Le SIMD vient ensuite,
pour regagner ce qu'on aura dépensé.

---

## 1. Ce qui est corrigé dans cette version (les clics à l'arrêt dans FL Studio)

On a cherché toutes les sources possibles de clic, mesuré chacune et corrigé :

| Cause trouvée | Effet | Correctif |
|---|---|---|
| **Composante continue (DC)** dans 28 presets, jusqu'à 33 % du RMS (pulse étroit, warp, LOOP) | Quand les notes s'arrêtent, le continu disparaît d'un coup : « toc » grave | Couplage AC par oscillateur : suivi du continu à 3 Hz, **initialisé exactement** au départ de note (moyenne sur 128 points du cycle), donc aucune transitoire. Plus un coupe-continu final à 5 Hz **en double** |
| Enveloppes recalculées « au début de chaque bloc » de l'hôte | FL Studio envoie des blocs de taille variable. L'enveloppe repartait d'un point décalé, d'où un petit saut à chaque bloc pendant l'attaque et la relâche, et des durées faussées | État d'interpolation gardé dans la voix : le rendu est maintenant **identique au bit près quel que soit le découpage** (écart mesuré 3,7·10⁻⁹) |
| Rampes de paramètres calées sur la longueur du morceau rendu | Le résultat dépendait du découpage de l'hôte | Rampes calculées sur ce qui reste du bloc de contrôle |
| Bloc plus grand que la taille annoncée : le plugin rendait la main **sans rien écrire** | Un trou dans le son, donc un clic (ou des déchets si l'hôte ne vide pas ses buffers) | Le bloc est traité par tranches |
| PANIC (et changement de preset) : coupure sèche | Clic | Fondu de sortie de 5 ms puis remise à zéro, fondu d'entrée de 5 ms. Les notes jouées pendant le fondu sont gardées |
| Automations appliquées en début de bloc, sans lissage des gains | « Zipper », sauts quand l'hôte remet une valeur à l'arrêt | **Automation à l'échantillon près** (`sampleOffset` respecté) et lissage de 12 ms (en linéaire pour les gains) sur master, niveaux, pans, cutoff, résonance, warp et positions |
| `fastTanh` bornée à ±4 | Entre 3 et 4 la courbe dépassait 1, puis retombait d'un coup à 4 : petite discontinuité, crénelage | Bornée à ±3. L'approximant de Padé (3,2) y vaut exactement 1 avec une pente nulle, la courbe est C¹ |

Des tests de non-régression ont été ajoutés dans `tests/engine_test.cpp` :

- invariance au découpage (blocs aléatoires de 1 à 500 échantillons) ;
- continu inférieur à 2 % du RMS ;
- automation qui ne bouge rien avant son offset ;
- PANIC sans saut plus grand que la variation normale du son.

> Si un clic persiste dans FL Studio, ouvrir *Fenêtre du plugin → icône engrenage → Processing* et
> essayer « Use fixed size buffers ». Si le clic disparaît, il venait du découpage de l'hôte : ce
> cas est maintenant traité, mais l'information aidera à cibler.

---

## 2. Audit : état actuel face au cahier des charges

✅ conforme · ⚠️ partiel · ❌ absent

### Pilier 1 : qualité sonore

| Exigence | État | Constat |
|---|---|---|
| ADAA ordre 1 et 2 sur la distorsion | ❌ | Façonneurs évalués point à point (`atan`, `tanh` asymétrique, `sin`, clip dur) |
| Suréchantillonnage IIR polyphase (HIIR) | ✅ | Demi-bandes de Valenzuela-Constantinides (méthode de Laurent de Soras) : 2× avec 8 coefficients, rejection mesurée **95,4 dB** ; étage 4× avec 4 coefficients, **100,2 dB** |
| Crénelage résiduel | ⚠️ | Sinus de 5 kHz, drive à fond, rapport crénelage / harmoniques : voir le tableau ci-dessous. Cible commerciale : < −90 dB |
| FIR à phase linéaire en rendu (`kOffline`) | ❌ | `processMode` n'est pas lu. Voir l'objection au § 3.4 : changer de filtre en rendu change la latence |
| DC blocker en double | ✅ (nouveau) | Sortie 5 Hz en double ; la distorsion a le sien (10 Hz, en float) |
| Asymétrie dynamique, hystérésis | ❌ | Le BIAS est statique |
| Filtres ZDF/TPT (Simper, Zavalishin) | ✅ | SVF trapézoïdal de Cytomic dans les voix et dans l'effet FILTRE, coefficients `g, k` interpolés par échantillon (stable en modulation rapide) |
| Non-linéarité dans la boucle du filtre | ⚠️ | Voix : `tanh` **avant** le filtre, et ce drive n'est pas suréchantillonné (crénelage à fort drive). Effet FILTRE : saturation douce de l'état passe-bande |
| Cramping près de Nyquist | ⚠️ | SVF pré-déformé (exact à la coupure). Les EQ (effet EQ et MIX IA) sont des biquads RBJ : cloche et shelf écrasés au-dessus de 10 kHz à 44,1 kHz |
| Modulation par sous-blocs | ✅ | Bloc de contrôle de 16 à 256 échantillons (réglage SAMPLES, 64 par défaut), enveloppes tous les 8 échantillons, rampes linéaires par échantillon |
| `sampleOffset` respecté | ✅ (nouveau) | Voir § 1 |
| Lissage anti-zipper | ✅ | 12 ms (moteur), 20 ms (effets), pôles simples suivis de rampes linéaires |
| Compresseur à détection log découplée, release dépendant du programme | ⚠️ | Détection crête, gain lissé en dB, genou doux de 6 dB, attaque adaptative (10 ms / 1 ms). Pas de filtre passe-haut dans la détection, release fixe |
| True Peak, lookahead, `getLatencySamples()` | ❌ | Limiteur MIX IA instantané, sans anticipation (il distord les transitoires). Latence déclarée : 0 |
| Délais fractionnaires sans perte d'aigus | ✅ / ⚠️ | Hermite 4 points (HYPER, flanger, chorus, délai). La réverbération lit ses lignes modulées en **linéaire** : perte d'aigus variable, bruit de modulation |
| FDN Householder, modulation, amortissement | ✅ | 8 lignes, Householder, modulation sinusoïdale de 0,25 ms, passe-bas d'un pôle par ligne |
| Compensation de gain automatique (K-weighting) | ⚠️ | Hors ligne seulement : les presets sont calibrés par `tools/fx_trim`. Rien en temps réel quand on tourne un bouton |
| Précision mixte | ⚠️ | Phases en double et coupe-continu final en double. Le reste est en float, dont le biquad MIX IA à 80 Hz (topologie DF-II transposée, sensible aux arrondis dans le grave) |

**Crénelage de la distorsion** (sinus de 5 kHz, drive à fond) :

| Mode | 1× | 2× | 4× |
|---|---|---|---|
| Tape | −28,2 dB | −55,1 dB | −73,7 dB |
| Tube | −25,6 dB | −41,2 dB | −56,1 dB |
| Fold | −17,0 dB | −57,1 dB | −57,2 dB |
| Hard clip | −20,7 dB | −34,6 dB | −51,6 dB |

### Pilier 2 : temps réel et performance

| Exigence | État | Constat |
|---|---|---|
| Zéro allocation dans `process()` | ✅ | Tout est alloué dans `prepare()` / `setupProcessing()`. Les événements et automations passent par des tableaux pré-alloués |
| Lock-free UI ↔ DSP | ✅ | Paramètres et matrice en atomiques. Wavetables par pointeur atomique avec époque, du RCU en pratique : le GUI attend que le bloc audio en cours soit fini avant de libérer l'ancienne table. Notes jouées au clavier de l'interface : file SPSC |
| Crossfade lors d'un changement de routage | ✅ | Changement d'ordre des effets et de mode de distorsion : creux de 10 ms. Marche / arrêt : fondu de 20 ms |
| Alignement et SoA | ❌ | Voix en AoS (`std::array<Voice, 8>`) |
| SIMD (SSE/AVX2) | ❌ | Code scalaire (`-O2 -ffast-math` sous GCC ; `/O2` sans `/fp:fast` sous MSVC) |
| Approximations rapides | ⚠️ | `tanh` de Padé (corrigé) ; mais `std::pow`, `log10`, `exp`, `tanh` et `atan` restent dans des boucles par échantillon (compresseurs, distorsion) |
| FTZ/DAZ | ✅ | MXCSR `0x8040` posé à l'entrée de `process()` et restauré à la sortie |

---

## 3. Analyse critique : ce qui manque pour jouer dans la cour des grands

Le cahier des charges est solide sur les effets. Il oublie pourtant ce qui fait **le son d'un synthé** :
l'oscillateur, la voix et ce qui se passe entre les notes. Voici les « armes secrètes », classées par
impact audible.

### 3.1 L'oscillateur (absent du cahier des charges, et c'est le plus important)

1. **Les basses manquent d'aigus.** Le niveau 0 du mipmap s'arrête à **127 harmoniques**, et il
   sert pour toutes les notes sous environ 200 Hz. Un DO1 (32,7 Hz) s'arrête donc à 4,2 kHz, un LA1
   (55 Hz) à 7 kHz. Serum et Vital montent à 1024 harmoniques.
   - **Correctif :** monter le niveau 0 à 512 harmoniques (4 échantillons par période de
     l'harmonique la plus haute dans une table de 2048).
   - Lire en **Hermite cubique** pour les niveaux de plus de 256 harmoniques. En linéaire, ces
     niveaux souffriraient d'une perte d'aigus et d'images.
   - Coût : 2 lectures de plus par oscillateur, seulement dans le grave.
2. **Le changement de niveau de mipmap est brutal.** Le niveau est choisi une fois par bloc de
   contrôle, sans fondu. Sur un pitch bend, un glide ou une modulation de pitch par LFO, le timbre
   saute d'une demi-octave d'harmoniques d'un coup : petit « clic de timbre ».
   - **Correctif :** fondu enchaîné entre deux niveaux voisins, avec un niveau fractionnaire calculé
     depuis `log2(incrément)`. On lit 2 niveaux au lieu d'un, c'est la technique standard.
3. **Le warp de phase crée des harmoniques hors budget.** La correction actuelle `(1 + 1,4·w)` est
   empirique.
   - **Correctif :** borner `w` par niveau, ou appliquer le warp à la table elle-même. C'est-à-dire
     rendre la table déformée puis la limiter en bande (FFT par frame, hors du thread audio, à
     chaque changement de warp lissé). Le crénelage du warp disparaît alors totalement.
4. **Dérive analogique, l'arme secrète la moins chère.** Chaque voix reçoit des décalages aléatoires
   fixés au départ de note : ±2 à 4 cents de pitch, ±3 % de coupure, ±0,3 dB de niveau. On peut y
   ajouter un bruit 1/f très lent (0,1 à 1 Hz) pour la dérive dans la durée. C'est ce qui sépare un
   accord « numérique » d'un accord « vivant ». Coût : négligeable.
5. **Vol de voix sans clic.** Quand les 8 voix sont prises, la voix volée change de note sur place :
   son niveau et sa phase sont gardés (donc pas de clic d'amplitude), mais la hauteur et le timbre
   sautent.
   - **Correctif :** une voix « fantôme » qui s'éteint en 3 ms pendant que la nouvelle démarre.
   - Ou passer à 16 voix et voler la voix la plus faible en relâche.

### 3.2 La voix : filtre et saturation

1. **Drive de voix sans crénelage.** Remplacer `tanh(x·d)` par son ADAA d'ordre 1, avec la primitive
   analytique F₁(x) = ln cosh x. On l'évalue de façon stable par F₁(x) = |x| + log1p(e^(−2|x|)) − ln 2.
   Pas besoin de suréchantillonner chaque voix.
2. **Saturation dans la boucle de résonance**, c'est elle qui fait l'auto-oscillation « musicale ».
   - SVF : saturer `v1` (sortie passe-bande) dans le calcul de la réinjection, en résolvant le
     système implicite par **une seule itération de Newton** depuis l'estimation linéaire (méthode
     de Simper). C'est stable, coûte environ ×1,6 et ne crénelle presque pas, car la saturation
     agit sur une sortie déjà filtrée.
   - Option **ladder 24 dB ZDF** (Zavalishin, *The Art of VA Filter Design*, chap. 5) : 4 étages
     TPT d'un pôle, `tanh` par étage, résolution de la boucle globale par la formule instantanée
     G·ξ + S. Ce sera le mode « grave qui mord » des presets basse.

### 3.3 Distorsion : ADAA sur table (éditeur de courbes)

C'est la pièce maîtresse demandée. Voici la méthode exacte, qui évite toute intégration numérique.

**Principe.** La courbe utilisateur (spline) est échantillonnée sur une table uniforme de
N + 1 nœuds sur [−A, A], par exemple N = 4096 et A = 4, puis interpolée linéairement :

  f(x) = fᵢ + sᵢ·t,  t = x − xᵢ,  sᵢ = (fᵢ₊₁ − fᵢ)/h

Pour une f **linéaire par morceaux**, les primitives sont **exactes et fermées** :

  F₁(x) = C1ᵢ + fᵢ·t + sᵢ·t²/2
  F₂(x) = C2ᵢ + C1ᵢ·t + fᵢ·t²/2 + sᵢ·t³/6
  C1ᵢ₊₁ = C1ᵢ + h·(fᵢ + fᵢ₊₁)/2   (le trapèze est exact pour une droite)
  C2ᵢ₊₁ = C2ᵢ + C1ᵢ·h + fᵢ·h²/2 + sᵢ·h³/6

On choisit les constantes pour que F₁(0) = F₂(0) = 0 : cela limite la croissance des valeurs, donc
les erreurs d'arrondi. Hors de la table, on prolonge le dernier segment ; F₁ et F₂ restent des
polynômes.

**ADAA ordre 1** (Parker, Zavalishin, Le Bivic, DAFx-16) :

  y[n] = (F₁(x[n]) − F₁(x[n−1])) / (x[n] − x[n−1])

Si |Δ| < ε, on prend y = f((x[n] + x[n−1])/2).

**ADAA ordre 2** (Bilbao, Esqueda, Parker, Välimäki, IEEE SPL 2017). Avec
D(a, b) = (F₂(a) − F₂(b))/(a − b), ou F₁((a+b)/2) si a ≈ b :

  y[n] = 2·(D(x[n], x[n−1]) − D(x[n−1], x[n−2])) / (x[n] − x[n−2])

Si x[n] ≈ x[n−2] : on pose x̄ = (x[n] + x[n−2])/2 et δ = x̄ − x[n−1], puis
y = (2/δ)·(F₁(x̄) + (F₂(x[n−1]) − F₂(x̄))/δ), ou f((x̄ + x[n−1])/2) si |δ| < ε.

**Précautions de commercial** :

- **Calcul de F₁, F₂ et des différences en double.** En float, F₁(x[n]) − F₁(x[n−1]) subit une
  annulation catastrophique dès que Δ est petit. Avec ε ≈ 10⁻⁶ en double, tout est propre.
- **Retards et léger passe-bas** : l'ADAA-1 retarde d'un demi-échantillon, l'ADAA-2 d'un
  échantillon.
  - Dans la zone linéaire, l'ADAA-1 revient à une moyenne de deux échantillons : |H| = cos(ω/2).
  - À l'intérieur d'un 2× (96 kHz), cela fait −0,5 dB à 10 kHz et −2 dB à 20 kHz. Il faut alors un
    shelf fixe de correction, ou passer en 4× (−0,5 dB à 20 kHz).
  - En chemin parallèle (mix sec / traité), il faut retarder le sec d'autant, sinon on obtient un
    filtre en peigne.
- **Ne jamais mettre l'ADAA dans une boucle de réinjection** (son retard rend la boucle implicite).
  Dans les boucles, utiliser une saturation douce.
- **Paramètres modulés** (drive, bias) : on applique `x·drive + bias` **avant** la table et
  `1/drive` après. La primitive reste celle d'une seule courbe, l'ADAA reste exact.
- **Changement de courbe pendant la lecture** : nouvelle table construite hors thread audio, publiée
  par RCU (pointeur atomique avec époque, comme les wavetables), puis **fondu de 5 à 10 ms** entre
  l'ancienne et la nouvelle sortie. Il faut deux ADAA en parallèle pendant le fondu.

**Combinaison gagnante** : ADAA + suréchantillonnage IIR (déjà là). L'ADAA-1 apporte typiquement
20 à 30 dB de mieux sur un clip dur. Cibles à vérifier par le test de crénelage (Hard clip, 5 kHz) :

| Configuration | Crénelage |
|---|---|
| Aujourd'hui, 2× | −34,6 dB |
| ADAA-1 + 2× | environ −60 dB |
| ADAA-1 + 4× | environ −75 dB |
| ADAA-2 + 4× (mode HQ et rendu) | −90 dB ou mieux |

**Asymétrie dynamique.** Le bias suit l'enveloppe du signal d'entrée (suiveur 5 ms / 80 ms) :
`bias = b₀ + k·env`. Les harmoniques paires « respirent » avec la dynamique au lieu d'être figées.
Retirer le continu ainsi créé **avant** le filtre de ton.

**Pré-accentuation / désaccentuation** (le secret des magnétophones) : un shelf de +6 dB au-dessus
de 3 kHz avant la non-linéarité, et le shelf inverse après. Le grave sature davantage, l'aigu moins,
et l'intermodulation agressive baisse. Deux SVF de shelf, coût minime.

**Hystérésis** : le vrai modèle de Jiles-Atherton (Chowdhury, DAFx-19) coûte cher et demande 4× ou
plus. Version « commerciale » : une non-linéarité à mémoire d'ordre 1, y = f(x + α·(y[n−1] − x[n−1])),
avec α de 0,1 à 0,3. Elle donne la légère traînée de phase dépendante du niveau typique du ruban.

### 3.4 Suréchantillonnage : une objection importante au cahier des charges

« IIR en temps réel, FIR à phase linéaire en rendu (`kOffline`) » : **à éviter tel quel.** Les deux
filtres n'ont pas la même latence (quelques échantillons de retard de groupe pour l'IIR, 32 à 64
pour un FIR). Si la latence change au rendu, la compensation de l'hôte (PDC) devient fausse et
l'export est décalé par rapport à la lecture. FL Studio ne relit pas toujours la latence entre les
deux modes.

**Recommandation :**

- **Mêmes filtres IIR polyphase en lecture et en rendu.** Le rendu monte seulement le facteur
  (2× → 8×) et l'ordre (rejection de 120 dB), ce qui ne change pas la latence déclarée (0) et ne
  coûte rien en lecture.
- **Suréchantillonnage par module, jamais global.** Un synthé passe 90 % de son temps dans des
  traitements linéaires (oscillateurs à table limitée en bande, filtres, délais) qui n'en ont pas
  besoin. Seuls les façonneurs (distorsion, drive, saturation de filtre) le méritent.
- La phase non linéaire des IIR (déphasage près de 20 kHz) est inaudible sur un effet de
  saturation. La phase linéaire ne sert qu'en mastering parallèle.

### 3.5 Filtres d'EQ

- **Remplacer les biquads RBJ par des SVF de Simper** pour les cloches et les shelves (le « linear
  trap optimised » : sortie m₀·v₀ + m₁·v₁ + m₂·v₂). Ils sont stables quand on tourne fréquence ou
  gain à toute vitesse : plus de recalcul de coefficients en DF-II, plus de transitoires. Ils sont
  aussi bien meilleurs numériquement dans le grave (le biquad à 80 Hz en float est bruité).
  - Cloche : g = tan(π·fc/fs), k = 1/(Q·A), m₁ = k·(A² − 1), avec A = 10^(dB/40).
- **Cramping :** pour les bandes fixes ou lentes (MIX IA, EQ des effets), utiliser les biquads
  « Matched Second Order » de Martin Vicanek (2016). Ils sont en forme fermée et reproduisent la
  forme analogique jusqu'à Nyquist sans suréchantillonner. Un shelf de 12 kHz à 44,1 kHz sonne
  alors comme dans un EQ analogique. Pour les bandes modulées, garder le SVF.

### 3.6 Dynamique

**Détecteur** : « smooth decoupled peak » en domaine log (Giannoulis, Massberg, Reiss,
*JAES* 2012) :

  y₁ = max(x_dB, α_R·y₁ + (1 − α_R)·x_dB)
  y_dB = α_A·y_dB + (1 − α_A)·y₁

**Release dépendant du programme** : deux détecteurs, un rapide (40 ms) et un lent (1,5 s). Le gain
appliqué suit le plus « réduit » des deux, à la manière de l'« auto release » d'un SSL : plus de
pompage sur un pad, et ça reste nerveux sur un pluck.

**Arme secrète n° 1 de la compression : un passe-haut dans le chemin de détection** (SVF de 80 à
150 Hz, 12 dB/oct). Le sub ne fait plus pomper tout le mix, et la distorsion d'intermodulation dans
le grave disparaît, puisque c'est le grave qui module le gain à sa propre fréquence. C'est
précisément le problème cité dans le cahier des charges, et c'est ainsi qu'on le règle.

**Limiteur True Peak avec anticipation** (MIX IA) :

1. Détection : suréchantillonnage 4× du chemin de détection seulement, par le FIR polyphasé de
   l'ITU-R BS.1770-4, annexe 2 (4 phases × 12 coefficients). On prend la crête des 4 phases.
2. Gain voulu par échantillon : g = min(1, plafond/crête).
3. **Minimum glissant** sur la fenêtre d'anticipation L (1,5 ms). Une file monotone (deque)
   pré-allouée sur un buffer circulaire le fait en O(1) amorti.
4. Lissage de l'attaque par **deux moyennes glissantes en cascade** de longueur L/2 (méthode de
   Geraint Luff / Signalsmith, 2022). Le gain atteint exactement le minimum au moment où la crête
   arrive, sans dépassement, et la forme est en cloche (pas de distorsion de « coin »). Release
   exponentiel dépendant du programme.
5. Retard de l'audio de L échantillons. **`getLatencySamples()` renvoie L en permanence**, que le
   limiteur soit actif ou non. Changer la latence en cours de route oblige à
   `restartComponent(kLatencyChanged)`, que les hôtes gèrent mal.

**Note pour un instrument** : 1,5 ms de latence en jeu live reste imperceptible, mais on peut
proposer un mode « 0 latence » pour le jeu et laisser le mode anticipé au rendu.

### 3.7 Délais, chorus, réverbération

- **Interpolation pour les délais modulés : garder Hermite 4 points** (ordre 3, continu en C¹,
  4 lectures). Comparaison :
  - **Lagrange d'ordre 4** coûte un peu plus pour un gain inaudible.
  - **Thiran (passe-tout)** est **à proscrire en modulation** : son état interne fait des
    transitoires quand la fraction de retard varie vite. Il est parfait pour un retard fractionnaire
    **fixe**, par exemple l'accord d'un Karplus-Strong ou les longueurs de lignes d'une réverbération.
- **Réverbération v2** (meilleur rapport qualité/coût connu, inspiré de l'exposé de Geraint Luff à
  l'ADC 2021, « Let's write a reverb ») :
  - **Diffuseur** : 4 étages, chacun = 8 canaux retardés + matrice de Hadamard + inversions de
    signe aléatoires. La densité d'écho double à chaque étage, sans passe-tout « métalliques ».
  - **FDN** : 16 lignes, matrice de Householder (O(N), déjà en place). Longueurs premières entre
    elles, réparties de façon exponentielle.
  - **Amortissement par ligne calé sur un RT60 en fréquence** (filtres absorbants de Jot) :
    shelf d'un pôle par ligne, dont les gains grave et aigu sont calculés depuis RT60_bas et
    RT60_haut et la longueur de la ligne. Le timbre de la queue est alors indépendant de la taille.
  - **Modulation par marche aléatoire lissée** (bruit filtré à 0,5–2 Hz) au lieu d'un sinus : plus
    de chorus audible sur les queues. Lecture **Hermite** au lieu de linéaire (corrige la perte
    d'aigus variable mesurée aujourd'hui).
  - Coût estimé : environ 1,5 % de CPU. On a la marge.
- **Délai** : passe-haut (80 Hz) dans la boucle, pour que le grave ne s'accumule pas. Synchro
  tempo (le tempo de l'hôte est déjà lu pour les LFO). Saturation douce dans la boucle (déjà là).

### 3.8 Gain, headroom, psychoacoustique

- **Compensation de gain automatique en temps réel** (page EFX) :
  - deux sonies à court terme, sec et traité, sur 400 ms avec pondération K (BS.1770 : shelf
    +4 dB à 1,5 kHz et passe-haut RLB à 38 Hz) ;
  - correction lente (constante de 1 à 2 s), bornée à ±12 dB, gelée sous −70 LUFS (silence) et
    pendant les attaques ;
  - un bouton « AUTO GAIN » comme sur Saturn ou Pro-L. C'est le seul moyen honnête de juger un
    effet.
- **Précision mixte, où elle compte vraiment** :
  - **double** pour : les phases (fait), les coupe-continu (fait), les filtres de moins de 100 Hz,
    les intégrateurs ADAA, les détecteurs lents des compresseurs ;
  - **float** partout ailleurs, pour le débit SIMD ;
  - pour un filtre grave, changer de topologie (SVF) apporte plus que passer un biquad DF-II en
    double.
- **Psychoacoustique** :
  - dérive analogique des voix (§ 3.1) ;
  - largeur stéréo par décorrélation (passe-tout différents à gauche et à droite) au lieu d'un
    simple retard, ce qui évite le filtrage en peigne en mono ;
  - vérification de compatibilité mono dans les tests (somme L + R sans trou de plus de 3 dB) ;
  - légère saturation de « colle » sur le bus des voix avant les effets (tanh ADAA à −18 dBFS) :
    ce qui sonne « analogique » est souvent ce qui ne sature jamais franchement mais jamais zéro
    non plus.

### 3.9 Ce qui distingue un plugin à 150 € : les tests

On ne vend pas « zéro artefact » sans le prouver. Banc de tests automatique à étendre (le début est
déjà dans `tests/`) :

- **Crénelage** : balayage sinus puis FFT, puis énergie hors harmoniques. Le seuil est un test :
  < −90 dB en HQ.
- **Clics** : dérivée seconde autour des arrêts et départs, blocs aléatoires (fait).
- **Invariance au découpage et au taux d'échantillonnage** (fait pour le découpage).
- **Détecteur d'allocations** : surcharger `operator new` dans le test et compter les appels pendant
  `process()`. Le test échoue si le compte n'est pas nul.
- **Null tests** : bypass = bit à bit identique ; mix 0 % = sec exact.
- **Banc CPU** à chaque build, pour voir une régression avant l'utilisateur.

---

## 4. Architecture cible du moteur (C++20)

```
          ┌──────────────────────── thread GUI ────────────────────────┐
          │ ParamStore (atomiques) · ModMatrix · tables (RCU + époque) │
          └───────────────┬────────────────────────────────────────────┘
   automations (offset) ─┤ SPSC : notes du clavier GUI, commandes (PANIC, courbes)
                         ▼
 ┌─────────────────────────────── thread audio : process() ───────────────────────────────┐
 │ FTZ/DAZ · découpage en micro-blocs de 32 éch. (frontières = notes / automations)        │
 │                                                                                          │
 │  1. Contrôle (par micro-bloc) : LFO, VIE, matrice → ModBus                              │
 │     ModBus = alignas(64) float val[kSources][32] (valeurs par échantillon, interpolées) │
 │  2. Voix (SoA, 8 voies AVX / 4 voies SSE)                                                │
 │     osc A/B (mip croisé, Hermite) → AC → drive ADAA-1 → SVF/ladder (Newton 1 it.)      │
 │     → enveloppes → pan → bus des voix                                                   │
 │  3. Bus : TEXTURE (grains) · ESPACE                                                     │
 │  4. Rack EFX : chaque module déclare son facteur de suréchantillonnage et sa latence ; │
 │     ADAA + OS par module, fondus de routage                                              │
 │  5. Master : DC (double) · EQ Vicanek/SVF · comp (SC HPF) · limiteur TP (L éch.)        │
 │  6. Fondu de sécurité (PANIC / activation) · sortie                                      │
 └──────────────────────────────────────────────────────────────────────────────────────────┘
```

**Points clés :**

1. **Micro-blocs de 32 échantillons**, au lieu des 64 actuels, et bornés par les événements. Les
   tampons intermédiaires d'un micro-bloc, 16 sources × 32 × 4 octets = 2 Ko par bus, tiennent tous
   dans le cache L1 (32 à 48 Ko).
2. **ModBus vectorisé.** Chaque source fournit 32 valeurs par micro-bloc, interpolées linéairement
   depuis le calcul de contrôle, ou calculées à chaque échantillon pour les sources audio (FM). Une
   destination = somme pondérée de lignes du bus, donc une boucle FMA vectorisable.
3. **Voix en SoA** : `struct VoiceLanes { alignas(32) float phase[8], inc[8], ic1[8], ic2[8], g[8], k[8], env[8]; }`.
   Le SVF, les enveloppes, le pan et l'ADAA se vectorisent parfaitement. Les lectures de table
   passent par des *gathers* (`_mm256_i32gather_ps`) ou par 8 lectures scalaires : plus lent, mais
   ça reste le seul point scalaire. Gain attendu : ×3 à ×5 sur la partie voix.
4. **Dispatch à l'exécution** : les noyaux AVX2+FMA sont compilés dans une unité séparée
   (`/arch:AVX2` sur ce seul fichier), choisis par `__cpuid` au chargement, avec un repli SSE2. **Ne
   jamais compiler tout le plugin en AVX2** : il planterait sur les vieux processeurs de certains
   clients.
5. **Latence** : chaque module déclare la sienne. Le total est figé à la préparation
   (`setupProcessing`) et renvoyé par `getLatencySamples()`. Seul le limiteur TP en ajoute.
6. **Approximations rapides** :
   - `exp2` et `log2` par polynôme minimax de degré 5 sur la mantisse, plus le décalage d'exposant :
     erreur < 10⁻⁷ relative, environ 8 instructions ;
   - `tanh` par Padé [7/6] bornée à ±5, ou `x/sqrt(1+x²)` pour un tape « doux » ;
   - `sin` par polynôme impair de degré 7 sur [−π/2, π/2] ;
   - **jamais de `std::pow` ou `log10` par échantillon** : les compresseurs actuels en font, c'est
     le prochain gain CPU facile.

---

## 5. Feuille de route, classée par impact

| Prio | Chantier | Impact audible | CPU | Effort |
|---|---|---|---|---|
| **P0** ✅ | Clics, DC, automation à l'échantillon, lissage, `tanh` C¹ | ★★★★★ (corrige des défauts audibles) | ≈ 0 | fait |
| **P1** ✅ | **Oscillateurs** : 512 harmoniques dans le grave, fondu entre niveaux de mipmap, Hermite, warp limité en bande | ★★★★★ (le son de base de chaque preset) | +30 % voix | 2 j |
| **P1** ✅ | **Distorsion ADAA** (table exacte ordres 1-2, double) + drive de voix ADAA-1 | ★★★★☆ (crénelage −35 → −75/−90 dB) | +10 % | 2 j |
| **P1** ✅ | Vol de voix avec fondu de 3 ms et dérive analogique par voix | ★★★★☆ | ≈ 0 | 0,5 j |
| **P2** ✅ | Limiteur True Peak avec anticipation, latence déclarée ; compresseurs (SC HPF, détecteur découplé, release auto) | ★★★★☆ (transitoires propres, fin du pompage) | +3 % | 2 j |
| **P2** ✅ | EQ : SVF de Simper (modulables) et Vicanek (fixes) | ★★★☆☆ (air, grave propre) | ≈ 0 | 1 j |
| **P3** ◐ | Réverbération v2 (diffuseur de Hadamard, FDN 16, RT60(f), modulation aléatoire, Hermite) ; délai avec HP et synchro | ★★★★☆ | +1,5 % | 2 j |
| **P4** | Filtre : saturation dans la boucle (Newton) + ladder 24 dB ZDF | ★★★★☆ (caractère, auto-oscillation) | +40 % filtre | 2 j |
| **P5** | SIMD : voix en SoA AVX2/SSE avec dispatch, approximations rapides, micro-blocs de 32 | ☆ (performance seulement) | −60 à −75 % | 3 j |
| **P6** | Auto-gain pondéré K (EFX), mode HQ de rendu (OS 8×, ADAA-2) | ★★★☆☆ (jugement honnête) | rendu seulement | 1 j |
| **P7** ◐ | Nouvelles fonctions du cahier des charges : éditeur de courbes (spline → table ADAA, partagé distorsion / modulation), suiveurs d'enveloppe, macros, glisser-déposer de modulation | ★★★★☆ (fonctions) | faible | 4–6 j |

✅ fait, ◐ en partie fait. Le détail et les mesures sont au § 5.1 ci-dessous.

### 5.1 Ce qui est fait (version 5) et ce qui a été mesuré

| Brique | Réalisation | Mesure |
|---|---|---|
| Oscillateurs | 18 niveaux de mipmap (511 harmoniques dans le grave), formes calculées rendues à 4096 points, Hermite sur les niveaux riches, fondu continu entre niveaux | tests de niveau et de continuité dans `engine_test` |
| Drive de voix | ADAA-1 de la tanh de Padé, forme fermée `log1p` **sans annulation** : en float, erreur max 2,7·10⁻⁷ face à la référence en précision étendue | CPU moteur −11 % |
| Distorsion (EFX) | ADAA-1 analytique des 4 formes (TAPE atan, TUBE tanh asymétrique, FOLD sinus, HARD), en double, **dans** le suréchantillonnage | énergie hors harmoniques : 1x −20 → **−32,5 dB**, 2x −35,7 → **−54,3 dB**, 4x −50,9 → **−76,0 dB** |
| Vol de voix | 4 voix « fantômes » fondues en 3 ms ; dérive analogique ±3 cents par oscillateur, ±0,055 octave sur le cutoff | — |
| Mastering (MIX IA) | 12 SVF cloche / plateau en double ; compresseur à détection log, passe-haut 100 Hz, genou 6 dB, relâche double ; limiteur True Peak 4x à anticipation 1,5 ms, latence déclarée à l'hôte | `master_test` : sonie BS.1770 exacte à 0,01 LU, plafond tenu à −0,98 dBTP |
| Compresseur (EFX) | détection passe-haut 90 Hz, relâche selon le programme (enveloppe rapide 80 ms + lente 900 ms) | réduction identique sur le médium, la basse ne pompe plus |
| EQ (EFX) | 3 cloches SVF de Simper, coefficients glissés dans le sous-bloc (automation sans transitoire) | +12,00 dB exacts au centre, 44,1 et 48 kHz |
| Réverbération | lecture Hermite des lignes modulées, modulation sinus + marche aléatoire filtrée | RT60 à ±3 % du nominal |
| Délai | passe-haut 60 Hz dans la boucle | — |
| Chargement des presets | FFT à tables précalculées, deux niveaux de mipmap par IFFT (signaux réels), harmoniques par récurrence de Tchebychev | 219 → **89 ms** en moyenne, 475 → **169 ms** au pire |
| Limiteur coupé | seul le retard reste (latence constante), la détection True Peak dort | — |
| Rendu reproductible | un preset chargé après un autre sonnait un peu différemment : l'âge des cellules de la Vie, son taux de naissances et la valeur ALÉA du son précédent débordaient, et les générations se déclenchaient par différence de dates absolues (arrondis dépendant de l'historique) | rendu identique au bit près (test `engine_test`) ; l'IA s'entraîne sur des écoutes exactes |
| P7 | glisser-déposer de modulation, routes libres, rack de 6 LFO, formes dessinées | — |

CPU du moteur complet (8 voix tenues, effets et mastering, 48 kHz, un cœur) : **8,1 → 6,9 %** en
moyenne sur les 72 presets, pire cas 10,2 %. Reste à faire : réverbération v2 complète (FDN 16,
RT60(f)), filtre de voix non linéaire (P4), SIMD (P5), auto-gain et rendu HQ (P6).

### 5.2 Versions 6 à 8 : jeu, couleur, instruments

| Brique | Réalisation | Vérification |
|---|---|---|
| Couche d'entrée | accords (10 formes, strum jusqu’à 80 ms entre deux notes), arpégiateur (5 ordres, 1/4 à 1/32 avec triolets, 1 à 4 octaves, gate, swing, LATCH) calé sur la position de lecture de l'hôte, ou sur une horloge interne à l'arrêt ; événements internes placés à l'échantillon près (le bloc est découpé) | `v6chk` : arpège montant sur 2 octaves en 1/16 calé sur l'hôte, accord MIN7 avec strum de 40 ms |
| Couche des voix | 16 voix ; POLY, MONO (pile de notes), LEGATO ; glide exponentiel `1,5 s × v²`, TOUJOURS ou entre notes LIÉES ; unison 1 à 8 copies (désaccord symétrique, panoramique étalé) | glide 131 → 262 Hz progressif ; unison 8 = 8 voix actives |
| 808 | punch de hauteur à l'attaque (jusqu'à +36 demi-tons, 5 à 400 ms) et sous-oscillateur sinus à l'octave ou une octave plus bas | rendu fini sur les 72 presets (tests) |
| Filtre | 12 dB, ou 24 dB en deux étages SVF (k = 1,848 puis 0,765 à résonance nulle : Butterworth d'ordre 4), drive `tanh` avant le filtre | — |
| VINTAGE / PUMP | bande (pleurage, scintillement, saturation, passe-bas), souffle et craquements qui suivent l'enveloppe du signal, réduction de fréquence et de résolution ; sidechain calé sur le tempo (`beat` et `beatPerSample` transmis au rack) | `fx_test` : 12 effets, chaînes au hasard, sorties finies |
| Instruments | lecteur cubique stéréo de zones SFZ (touches, vélocités, boucles, accord fin, gains additifs), publié par pointeur atomique et retiré par époques | `engine_test` : La5 à 880 Hz ± 3 Hz, boucle tenue ; les 23 instruments livrés chargés (3 à 220 ms) et joués |
| Export | rendu hors ligne du son joué (copie de l'état complet, extinction de la queue, latence du limiteur retirée), WAV 24 bits, glisser OLE vers la DAW | — |

**Ordre conseillé : P1 (oscillateurs), puis P1 (ADAA), puis P2, puis P3.** Après chaque brique :
les tests (crénelage, clics, allocations, CPU) et un recalibrage des niveaux des presets
(`raw2_fx_trim`), puisque chaque brique change un peu le son.

---

## 6. Références

- V. Zavalishin, *The Art of VA Filter Design*, rév. 2.1 (Native Instruments)
- A. Simper, *Linear Trapezoidal Integrated SVF* et *Solving the continuous SVF equations using
  trapezoidal integration*, Cytomic Technical Papers (2013–2021)
- J. Parker, V. Zavalishin, E. Le Bivic, « Reducing the aliasing of nonlinear waveshaping using
  continuous-time convolution », DAFx-16
- S. Bilbao, F. Esqueda, J. Parker, V. Välimäki, « Antiderivative antialiasing for memoryless
  nonlinearities », IEEE Signal Processing Letters 24(7), 2017
- J. Chowdhury, « Practical Considerations for Antiderivative Anti-Aliasing » (2020) et
  « Real-time physical modelling for analog tape machines », DAFx-19
- L. de Soras, *HIIR*, demi-bandes IIR polyphases (méthode de Valenzuela et Constantinides)
- M. Vicanek, « Matched Second Order Digital Filters » (2016)
- D. Giannoulis, M. Massberg, J. Reiss, « Digital Dynamic Range Compressor Design — A Tutorial and
  Analysis », JAES 60(6), 2012
- ITU-R BS.1770-4 (sonie et True Peak), annexes 1 et 2
- J.-M. Jot, A. Chaigne, « Digital delay networks for designing artificial reverberators », AES 90
  (1991)
- G. Luff, « Let's Write a Reverb », ADC 2021 ; « Designing a straightforward limiter »,
  Signalsmith Audio (2022)
