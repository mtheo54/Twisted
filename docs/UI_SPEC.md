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

| Événement | Rendu | Source dans le plugin |
|---|---|---|
| Coup de basse (kick, 808, note de basse) | Onde qui part du sommet de l'antenne : anneau horizontal qui s'élargit, second anneau qui descend, coque filaire qui se dilate, flash de la pointe | Détecteur de transitoires sur la bande 20-150 Hz (thread d'analyse) |
| Force du coup | Opacité de l'onde | Énergie du transitoire au-dessus de la moyenne glissante |
| Knob **Stéréo** | Diamètre de l'onde | Largeur stéréo |
| Knobs **Saturation** / **Enhance** | Intensité de l'onde | Quantité de traitement |
| Knob **Dry / Wet** | Heure du jour : 0 % = jour, 40 % = après-midi, 60 % = crépuscule, 100 % = nuit | Mix |
| Nuit | Fenêtres, lanternes, enseignes et plateformes s'allument, les braises ressortent | Mix > 35 % |
| Bouton **Bypass** | Travelling avant sur la tour jusqu'à l'antenne, image désaturée, bandeau « BYPASS · 素通し », knobs grisés | Bypass |

### Prise de vue retenue

Plan large en trois-quarts, depuis les toits de l'autre côté du quartier,
légèrement en contre-plongée. La tour reste au centre, la ville se lit au
premier plan et la lune se place derrière l'antenne. La caméra dérive
lentement et suit la souris en léger parallaxe. Les immeubles hauts sont
interdits dans l'axe caméra → tour pour que rien ne cache la tour.

En bypass, la caméra avance jusqu'à cadrer la plateforme haute et l'antenne
(transition d'environ 1 s). Les animations sont réduites si le système
demande `prefers-reduced-motion`.

## 3. Disposition

```
┌──────────────────────────────────────────────────────────────────┐
│ TWISTED 物見櫓          Preset [Bus batterie ▾]  ◎ Analyser  ● Bypass │
├──────────────────────────────────────────────────────────────────┤
│ ┌Reconnaissance──┐                                 ┌Niveaux─────┐ │
│ │ Bus batterie 74%│          (scène 3D 16:8)        │ IN  ▮▮▮▮▯  │ │
│ │ barres / classe │                                 │ OUT ▮▮▮▯▯  │ │
│ └─────────────────┘                                 │ LUFS        │ │
│ Wet 80 % · nuit                              ▶ Démo  Charger un son│
├──────────────────────────────────────────────────────────────────┤
│ Entrée Saturation Stéréo Enhance │ (DRY/WET) │ Coupe-bas Coupe-haut Sortie │
│ Suréchantillonnage [1× 2× 4× 8× 16×]  Caractère [▾]  Topologie [▾]  ◉ Gain auto │
├──────────────────────────────────────────────────────────────────┤
│ Latence 0 échantillon · OS 4× · SIMD AVX2 · Threads … · CPU 3 %     │
└──────────────────────────────────────────────────────────────────┘
```

Taille par défaut de la fenêtre : 1180 × 800 px, redimensionnable ; en
dessous de 900 px de large, les knobs passent sur une grille et le Dry/Wet
monte en tête.

## 4. Contrôles

| Contrôle | Plage | Défaut | Rôle |
|---|---|---|---|
| Entrée | −24 … +24 dB | 0 dB | Trim d'entrée, piloté par le gain staging auto |
| Saturation | 0 … 100 % | 25 % | Drive de l'étage non linéaire |
| Stéréo | 0 … 200 % | 100 % | Largeur Mid/Side (0 = mono, 200 = très large) |
| Enhance | 0 … 100 % | 20 % | Excitation harmonique des aigus + punch transitoire |
| Dry / Wet | 0 … 100 % | 80 % | Mix parallèle (et heure du jour) |
| Coupe-bas | 20 … 500 Hz (log) | 30 Hz | Passe-haut |
| Coupe-haut | 2 … 20 kHz (log) | 20 kHz | Passe-bas |
| Sortie | −24 … +24 dB | 0 dB | Gain de sortie |
| Suréchantillonnage | 1×, 2×, 4×, 8×, 16× | 4× | Facteur autour de l'étage non linéaire |
| Caractère | Bande, Lampe triode, Transistor, Wavefolder | Bande | Courbe de saturation |
| Topologie filtre | SVF TPT (ZDF), Ladder ZDF 4 pôles, Biquad TDF-II, Linkwitz-Riley 24 | SVF TPT | Structure numérique des filtres |
| Gain staging auto | on / off | on | Vise −18 dBFS RMS en entrée, compense en sortie |
| Analyser | bouton | | Écoute 2 s, reconnaît la source, applique le preset |
| Bypass | bouton | off | Bypass sans clic (crossfade court) |

Interaction des knobs : glisser verticalement (Maj = réglage fin), molette,
flèches du clavier, Début/Fin, double-clic = valeur par défaut. Les knobs de
gain sont bipolaires (l'arc part du centre).

## 5. Reconnaissance audio et presets

Classes proposées : **Kick / 808**, **Basse**, **Voix**, **Bus batterie**,
**Guitare / synthé**, **Master**. Le panneau affiche la confiance de chaque
classe ; la meilleure choisit le preset, et les knobs glissent vers leurs
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
3. Ajouter un mode « vue libre » (orbite à la souris) en plus du plan fixe ?
4. Liste des classes de reconnaissance et des presets à compléter.
