# Twisted : le jeu · document de design

Statut : **proposition à valider** avant d'écrire le code.
Périmètre de ce document : la structure du projet, le système de sorts et de
fusions, et le plan de l'étape 1 (« la rue test »). La vision complète est
rappelée en une ligne par point dans [TODO.md](TODO.md).

---

## 1. Moteur : Godot 4 (confirmé)

Ta préférence est la bonne pour ce projet. Pourquoi :

| Critère | Godot 4 | Unreal 5 | Unity 6 |
|---|---|---|---|
| Installation | ~150 Mo, rien d'autre | ~100 Go + Visual Studio | ~15 Go + compte |
| Style low-poly soigné, brume, bloom | oui (rendu Forward+ : brouillard volumétrique, glow, ombres douces) | oui, mais taillé pour le photoréalisme | oui |
| Fichiers de scènes en texte | **oui** : je peux tout écrire et relire depuis le code | non (binaire) | partiellement |
| Coût, licence | gratuit, MIT, aucune redevance | 5 % au-delà d'1 M$ | abonnement selon revenus |
| Grande carte (5-8 km²) | pas de « World Partition » tout fait : chargement par zones à écrire nous-mêmes | outil intégré | outil partiel |

Le seul vrai point faible de Godot est le chargement progressif d'une grande
carte. Il est maîtrisable parce que la ville est construite en blocs
modulaires dès le départ (voir § 2.4). Pour une ville stylisée, Unreal
n'apporterait pas assez pour justifier sa lourdeur.

- Version : **Godot 4.6** (ou plus récent), édition « Standard » (pas .NET).
- Langage : **GDScript** (intégré, rien à installer, rechargé à chaud).
- Rendu : **Forward+** (PC uniquement, c'est celui qui a le brouillard
  volumétrique et le meilleur bloom).

## 2. Structure du projet

```
game/
├── project.godot            réglages du projet
├── DESIGN.md / TODO.md
├── data/                    ← TOUT l'équilibrage, en JSON lisible
│   ├── spells.json          sorts (coût, recharge, effets)
│   ├── fusions.json         sort A + sort B = sort C
│   ├── statuses.json        buffs / débuffs
│   ├── weapons.json         armes musicales
│   └── actors.json          joueur, cibles, monstres (vie, vitesse, attaques, jour/nuit)
├── core/                    ← LOGIQUE DE JEU PURE (aucun affichage)
│   ├── sim.gd               la « simulation » : reçoit des ordres, rend des événements
│   ├── entity_state.gd      vie, énergie, position, statuts d'une entité
│   ├── status_system.gd     buffs / débuffs génériques
│   ├── spell_system.gd      coûts, recharges, lancement
│   ├── fusion_resolver.gd   lit fusions.json, trouve la fusion de A + B
│   ├── effects.gd           briques d'effets (dégâts, téléport, dissipation…)
│   ├── projectile_state.gd  attaques en vol (avalables par le gater)
│   ├── world_clock.gd       heure du jour, jour/nuit
│   └── beat_clock.gd        horloge musicale (BPM, temps forts)
├── scenes/                  ← AFFICHAGE et contrôles (Godot)
│   ├── game/                la colle : branche la Sim sur les scènes
│   ├── actors/player/       le bonhomme d'abrasion
│   ├── actors/monsters/
│   ├── vehicles/skate/
│   ├── weapons/
│   ├── world/blocks/        blocs de ville modulaires
│   ├── world/sky/           ciel jour/nuit, lune, montagnes, oiseaux
│   └── levels/test_street.tscn
├── vfx/                     shaders : glitch, pixels, coupures, halos
├── audio/                   bus d'effets, sons générés
├── ui/                      HUD (vie, énergie, sorts, fusion)
├── reference/               ← images du bonhomme d'abrasion (à fournir)
└── tests/                   tests automatiques de la logique (core/)
```

### 2.1 Séparer la logique de l'affichage (pour le multijoueur plus tard)

```
 Clavier/souris ──► ORDRES ──► Sim (core/) ──► ÉVÉNEMENTS ──► Scènes (affichage, sons, effets)
                    « lancer gater »          « projectile 12 avalé »
                    « attaquer »              « statut Désynchro posé sur joueur »
```

- `core/` ne connaît ni les nœuds Godot, ni les sons, ni les shaders. Les
  entités y sont désignées par un numéro, pas par un objet affiché.
- La Sim avance par pas fixes (60 par seconde). Les déplacements et les
  collisions restent gérés par la physique de Godot ; leur position est
  recopiée dans la Sim à chaque pas.
- Plus tard, en multijoueur, le serveur fera tourner la Sim et les joueurs
  n'enverront que leurs ordres : rien à réécrire dans les règles.

### 2.2 Données plutôt que code

Les nombres (dégâts, coûts, durées, vitesses) sont dans `data/`. Tu peux les
modifier avec n'importe quel éditeur de texte, relancer le jeu, et sentir la
différence sans toucher au code.

### 2.3 Audio : effets simulés aujourd'hui, rythme demain

- Chaque famille de sons passe par un **bus audio** (joueur, ennemis, monde,
  musique). Les sorts appliquent un effet sur le bus visé :
  - bit crush → effet *Distortion* en mode *Lo-fi* de Godot (réduction de bits) ;
  - gater → volume haché par le code (porte rythmique), puis coupure nette ;
  - chorus, reverb → effets *Chorus* et *Reverb* intégrés à Godot.
- `beat_clock.gd` donne le tempo (100 BPM au départ) et signale chaque temps.
  Les armes et les effets s'y abonnent. Plus tard, on la calera sur la
  position réelle de la musique : seul ce fichier changera.
- Les premiers sons sont générés par code (bips, souffles, basses). Tu pourras
  les remplacer par les tiens en déposant des fichiers `.wav` / `.ogg`.

### 2.4 Ville modulaire

- Grille de **tuiles de 32 m × 32 m** : rue droite, carrefour, virage,
  impasse, place, bloc d'immeubles, bloc résidentiel. Chaque tuile est une
  scène qu'on assemble comme des briques.
- Les tuiles sont regroupées en **zones de 256 m** (8 × 8 tuiles). Une carte
  de 2,5 km de côté (~6 km²) ≈ 10 × 10 zones.
- Vitesses prévues : marche 5 m/s, course 8 m/s, skate 13 m/s, vélo 17 m/s.
  Le futur chargement progressif calculera son rayon d'après la vitesse
  (à vélo, on charge plus loin devant soi). Pas codé à l'étape 1.
- La tour est un repère unique au centre, visible de partout (version
  simplifiée au loin).

## 3. Sorts, statuts et fusions

### 3.1 Les ressources du joueur

- **Énergie** : 100 max, se recharge de 10/s. Chaque sort en coûte.
- **Recharge** (cooldown) par sort.
- Une fusion coûte plus cher qu'un sort seul, **met ses deux sorts en
  recharge** et impose une **contrepartie** (un débuff sur soi).

### 3.2 Statuts (buffs / débuffs) génériques

Joueur et monstres ont la même liste de statuts. Un statut a : un nom, un
type (buff ou débuff), une durée, un nombre de cumuls max, des
**modificateurs** et des **étiquettes**.

```json
{
  "saturation": {
    "name": "Saturation", "kind": "buff", "duration": 8.0, "max_stacks": 1,
    "dispellable": true,
    "modifiers": { "damage_dealt_mult": 1.5, "damage_taken_mult": 0.7 },
    "tags": ["audio"]
  },
  "desync": {
    "name": "Désynchro", "kind": "debuff", "duration": 4.0, "max_stacks": 1,
    "dispellable": true,
    "modifiers": { "move_speed_mult": 0.7, "damage_taken_mult": 1.2, "energy_regen_mult": 0.5 },
    "tags": ["glitch"]
  },
  "intangible": {
    "name": "Intangible", "kind": "buff", "duration": 0.35, "max_stacks": 1,
    "dispellable": false,
    "modifiers": { "damage_taken_mult": 0.0 },
    "tags": ["glitch"]
  }
}
```

Modificateurs connus du code : `move_speed_mult`, `damage_dealt_mult`,
`damage_taken_mult`, `energy_regen_mult`, `cooldown_mult`, `can_cast`
(silence). « Annuler les buffs adverses » = retirer tous les statuts
`kind: buff` et `dispellable: true` de la cible.

### 3.3 Sorts = liste de briques d'effets

Un sort ne contient pas de code : c'est une liste d'**effets** pris dans une
boîte à outils. Les briques de l'étape 1 :

| Brique | Ce qu'elle fait |
|---|---|
| `apply_status` | pose un statut (sur soi ou sur la cible) |
| `dispel` | retire les buffs (ou débuffs) dissipables de la cible |
| `damage` | dégâts, sur la cible ou en zone autour d'un point |
| `dash` | déplacement rapide dans la direction voulue |
| `teleport_to_target` | apparaît près de la cible (portée max, distance d'arrivée) |
| `swallow_projectiles` | avale les attaques ennemies avalables dans un cône devant soi |
| `silence` | empêche la cible de lancer un sort pendant un temps |

Chaque sort indique aussi ses effets **visuels** et **sonores** par un nom
(`"vfx": "glitch_pixel"`, `"sfx": "bitcrush_dodge"`), branchés côté affichage.

```json
{
  "bitcrush": {
    "name": "Bit crush", "energy": 20, "cooldown": 3.0,
    "effects": [
      { "type": "dash", "distance": 4.0, "duration": 0.18 },
      { "type": "apply_status", "status": "intangible", "on": "self" }
    ],
    "vfx": "glitch_pixel", "sfx": "bitcrush_dodge", "audio_fx": "bitcrush"
  },
  "gater": {
    "name": "Gater", "energy": 25, "cooldown": 6.0,
    "effects": [
      { "type": "swallow_projectiles", "range": 5.0, "angle": 90, "window": 0.6, "refund_energy": 10 },
      { "type": "silence", "on": "swallowed_owner", "duration": 1.5 }
    ],
    "vfx": "gate_slices", "sfx": "gate_chop", "audio_fx": "gate"
  }
}
```

### 3.4 Les deux premiers sorts

**Bit crush : esquive glitch.** Petit dash de 4 m ; pendant 0,35 s le
bonhomme est intangible. À l'écran : il se pixélise (moins de résolution sur
son corps), décalage rouge/bleu, traînée de blocs carrés. Au son : le monde
entier passe 0,35 s en basse résolution (8 bits → 4 bits), puis revient.

**Gater : avaler le sort adverse.** Pendant 0,6 s, un cône devant le joueur
« hache » tout projectile ennemi avalable : le projectile clignote en
tranches, son son est découpé en rythme (la porte s'ouvre et se ferme sur la
double croche du tempo), puis coupé net. Le projectile disparaît, son lanceur
est réduit au silence 1,5 s et le joueur récupère 10 d'énergie. À l'écran :
des lamelles verticales noires balaient le cône.

Le projectile ennemi est un **vrai objet de la Sim** (`projectile_state.gd`) :
position, vitesse, dégâts, lanceur, étiquette `swallowable`. Le gater
interroge la Sim, il ne « devine » pas d'après l'image.

### 3.5 Fusions

`fusions.json` liste les paires. L'ordre A + B ou B + A ne compte pas.

```json
[
  {
    "id": "glitch_jump",
    "name": "Saut glitch",
    "inputs": ["bitcrush", "gater"],
    "energy": 40, "cooldown": 12.0,
    "puts_inputs_on_cooldown": true,
    "effects": [
      { "type": "teleport_to_target", "max_range": 12.0, "stop_distance": 1.8 },
      { "type": "dispel", "on": "target", "kind": "buff" },
      { "type": "damage", "amount": 8, "radius": 2.5, "at": "self" }
    ],
    "drawback": [
      { "type": "apply_status", "status": "desync", "on": "self" }
    ],
    "vfx": "glitch_teleport", "sfx": "glitch_jump"
  }
]
```

**Saut glitch (Bit crush + Gater)** : le bonhomme se décompose en pixels,
réapparaît à 1,8 m de la cible (12 m max), annule ses buffs, fait 8 dégâts
autour de lui. Contrepartie : **Désynchro** 4 s (−30 % vitesse, +20 % dégâts
subis, énergie deux fois plus lente).

Ajouter une fusion = ajouter une entrée dans `fusions.json` avec les
briques existantes. **Aucun code à écrire.** Seule une brique d'effet
totalement nouvelle (par exemple « gèle le temps ») demande du code, une fois,
et devient ensuite réutilisable par tous les sorts.

### 3.6 Comment déclencher une fusion : ma proposition

**Maintenir le clic droit (« mode fusion »), puis appuyer sur les deux sorts.**

- Clic droit maintenu : le temps ralentit légèrement (×0,8, en solo
  seulement), deux emplacements vides apparaissent au centre du HUD.
- Appui sur A puis E (ou E puis A) : les deux icônes se remplissent ;
  si la fusion existe, elle part au second appui. Sinon, rien n'est dépensé
  et le HUD l'indique (« pas de fusion connue »).

Pourquoi pas « deux sorts en succession rapide » : bit crush est une esquive
qu'on enchaînera souvent avec gater en panique. La fusion partirait par
accident, et à l'inverse on la raterait par manque de rapidité. Une touche
dédiée rend la fusion **voulue**, découvrable (on essaie des paires) et
lisible pour les futures fusions à 4, 6, 10 sorts. Le clic droit reste
naturel au milieu d'un combat à la souris.

## 4. Armes musicales (2-3 pour l'étape 1)

Toutes réagissent au tempo de `beat_clock` : elles **pulsent sur chaque
temps** (lumière, échelle) et un coup donné **dans le temps** (± 80 ms) est un
« coup juste » : dégâts ×1,5, son plus riche, petit flash.

| Arme | Objet 3D | Jeu |
|---|---|---|
| **Métronome** | gros métronome pyramidal tenu comme une masse, balancier lumineux | corps à corps lent et lourd, combo de 3 coups |
| **Caisson** | enceinte de basse portée à l'épaule, membrane qui pompe | onde de basse en cône court, repousse les ennemis |
| **Vinyle** | disque lumineux lancé comme un frisbee, revient à la main | attaque à distance, rebondit une fois |

## 5. Le skate (véhicule de l'étape 1)

**Choix : le skate.** Plus simple à rendre agréable que le vélo : pas
d'animation de pédalage, une planche et une pose suffisent, et la glisse
(virages larges, petit saut « ollie ») est fun tout de suite.

- **F** près du skate : monter / descendre. **Z** pousser (accélère par
  impulsions), **S** freiner, **Q/D** tourner (virage plus large à vitesse
  élevée), **Espace** ollie.
- Vitesse max 13 m/s (marche 5, course 8).

**Attaquer en véhicule ? Ma proposition :**
- **Sorts : oui**, tous. Bit crush sur le skate = un écart glitch latéral,
  pratique pour éviter un projectile.
- **Armes : non**, sauf une seule action : **descente d'attaque** (clic
  gauche) : le bonhomme saute du skate et frappe en arrivant au sol, avec un
  bonus de dégâts lié à la vitesse. Simple à coder, et ça garde le skate
  comme outil de déplacement plutôt que de combat.

## 6. Monstres de l'étape 1

| Monstre | Quand | IA | Attaques |
|---|---|---|---|
| **Grésillon** (boule de parasites statiques, petites antennes) | jour et nuit | erre → repère le joueur à 15 m → approche → tire à 8 m → fuit un peu s'il a peu de vie | **Boule de larsen** : projectile lent, *avalable* par le gater. **Saturation** : se donne un buff (+50 % dégâts, −30 % dégâts subis), annulable par la fusion |
| **Ombre sub** (silhouette sombre, cœur violet qui bat sur le tempo) | **nuit seulement** | plus rapide, plus résistante, charge le joueur | **Onde sub** : projectile *avalable* ; coup au corps à corps *non avalable* (il faut esquiver) |

L'IA est une petite machine à états (errer / poursuivre / attaquer / fuir)
dans la Sim. Les valeurs sont dans `actors.json`.

## 7. Style visuel : comment on l'obtient

- **Bâtiments** générés par code : volumes à toits plats, rebords, antennes,
  climatiseurs, fenêtres (une texture générée qui s'allume la nuit).
- **Ciel** : un shader de ciel maison. Jour : dégradé pêche → gris-bleu,
  horizon lumineux, brume. Nuit : violet sombre, étoiles, grande lune avec
  halo derrière une montagne, lueur rose à l'horizon.
- **Montagnes** en triangles à l'horizon, plusieurs plans dans la brume.
- **La tour** : treillis rouge et blanc, anneaux d'énergie et sphère de
  lignes fines au sommet (on reprend l'esprit de l'interface du plugin).
- **Post-traitement** : brouillard volumétrique léger, bloom (glow) doux,
  occlusion ambiante, étalonnage par moment de la journée.
- **Effets de sorts** : shaders dédiés (pixélisation, décalage RVB,
  lamelles de porte), c'est la signature du jeu, on les soigne.
- **Performances** : ombres limitées à la zone proche, distance d'affichage
  par objet, un réglage « qualité » basse/haute dans le menu.

**Le bonhomme d'abrasion** (référence : `reference/bonhomme_abrasion.png`) :
- volumes simples comme sur l'icône : tête ronde séparée du corps, corps en
  gélule **sans jambes** (il flotte à 16 cm du sol, avec une ombre ronde
  dessous), gros bras arrondis, finition **chrome gris foncé** qui reflète le
  ciel ;
- **rendu pixélisé** comme l'image : une petite caméra le dessine en basse
  résolution (88 pixels de haut), puis l'image est posée dans la scène à sa
  place, avec de gros pixels nets et une silhouette en escalier. Le reste de
  la ville garde son rendu net, le bonhomme ressort comme sur l'icône ;
- **Bit crush** utilisera ce même réglage : sa résolution chute (88 → 12
  pixels) pendant l'esquive, puis remonte. Le sort « sonne » comme l'effet ;
- animations par code : flottement, penché en avant en course, bras qui
  balancent, coups de poing alternés, toupie à l'esquive, et il **fait
  coucou** comme sur l'icône quand on le laisse immobile ;
- F2 bascule entre le rendu pixélisé et le modèle lisse (comparaison, ou
  secours en cas de souci d'affichage).

## 8. Commandes (clavier AZERTY)

Les touches sont lues par **position physique** : sur un clavier QWERTY,
ZQSD devient automatiquement WASD.

| Action | Touche |
|---|---|
| Se déplacer | Z Q S D |
| Caméra | souris |
| Courir | Maj gauche (maintenir) |
| Sauter | Espace |
| Esquive (toupie rapide) | Ctrl gauche |
| Attaquer | clic gauche |
| Changer d'arme | 1, 2, 3 ou molette |
| Sort Bit crush | A |
| Sort Gater | E |
| Mode fusion | clic droit (maintenir) + A et E |
| Interagir (porte, skate) | F |
| Debug : jour ↔ nuit | N |
| Aide à l'écran | F1 |
| Bonhomme pixélisé / lisse | F2 |
| Libérer / reprendre la souris | Échap / clic |

## 9. Ce que tu dois fournir ou faire toi-même

1. **Installer Godot 4** (je te donnerai le lien et les étapes exactes au
   jalon 0, c'est un simple fichier à décompresser, ~150 Mo).
2. ~~Les images du bonhomme d'abrasion~~ : reçue, rangée dans
   `game/reference/`. D'autres vues (profil, dos) restent bienvenues.
3. Plus tard, en option : tes propres sons (`.wav`) et musiques, et le logo
   abrasion pour l'écran titre.

Tout le reste (modèles, textures, sons de départ) est généré par code ou
sous licence libre (CC0), et je le signalerai dans `game/CREDITS.md`.

## 10. Plan de l'étape 1 : la rue test

Un jalon = une fonctionnalité. Après chacun, je te dis comment le lancer et
le tester, et j'attends ton retour.

| # | Jalon | Ce que tu pourras tester |
|---|---|---|
| 0 | Installer Godot, ouvrir le projet | le projet s'ouvre, une scène vide tourne |
| 1 | **Prototype minimal** : bonhomme provisoire, caméra 3e personne, rue droite avec immeubles, roulade, une attaque de base sur un mannequin | bouger, sauter, esquiver, frapper le mannequin et voir sa vie baisser |
| 2 | **Skate** | monter, pousser, tourner, ollie, descendre, descente d'attaque |
| 3 | **3 armes** + horloge musicale | pulsation sur le tempo, coups justes |
| 4 | **Grésillon** (IA, boule de larsen, buff Saturation) | se faire attaquer, le tuer |
| 5 | **Bit crush** | esquiver une boule de larsen en glitch |
| 6 | **Gater** | avaler une boule de larsen, voir le Grésillon réduit au silence |
| 7 | **Fusion Saut glitch** | téléport sur un Grésillon saturé, buff annulé, Désynchro sur soi |
| 8 | **Porte** + petit bâtiment | ouvrir, fermer, entrer |
| 9 | **Jour / nuit** + ciel, fenêtres, lune, **Ombre sub** | passage de l'un à l'autre, monstre de nuit |
| 10 | Passe de finition visuelle | bloom, brume, tour au loin, oiseaux |

J'ai placé le monstre (jalon 4) **avant** les sorts : sans attaque ennemie,
le gater n'aurait rien à avaler et le bit crush rien à esquiver. C'est le
seul changement par rapport à l'ordre que tu as donné.

Ensuite viendront chorus et reverb (voir [TODO.md](TODO.md)).

## 11. Démo web et décisions validées (après la première démo)

**Démo sur internet.** On avance d'abord sur une démo jouable dans le
navigateur (Three.js, partageable par un simple lien), pour itérer vite.
Son code est dans `web-demo/`. Le projet Godot reste la version PC à long
terme : les choix validés dans la démo y seront reportés.

**Carte de la démo : un carrefour.**
- Une **rue commerçante et de restaurants** : konbini, ramen, sushi, izakaya,
  café, boutique de vêtements. Vitrines, auvents, rideaux noren, lanternes
  rouges, enseignes verticales en kanji, menus posés sur le trottoir.
- Une **rue résidentielle** : immeubles d'appartements (balcons, linge,
  climatiseurs), petites maisons à toit en pente, clôtures, plantes.
- Une **petite place avec un parc** : herbe, gravier, arbres, bancs.
- Objets de rue : lampadaires, poteaux électriques avec leurs fils,
  distributeurs de boissons, barrières, cônes, bancs, poubelles, vélos,
  arbres (cerisiers, ginkgos), feux de circulation.
- Aucune vraie marque : enseignes inventées.

**Sols variés et texturés** (style dessiné, pas photo) : asphalte fissuré
avec plaques d'égout et passages piétons, dalles de trottoir, pavés de la
rue commerçante, carrelage de la place, herbe, gravier, plaques de métal,
terrasse en bois.

**Déplacements.**
- **Sensation de vitesse** : champ de vision qui s'élargit en course,
  caméra plus basse, lignes de vitesse, poussière, accélération
  progressive, secousse à l'atterrissage, bruits de pas.
- **Double saut** (une fois par saut, petit effet pixel).
- **Wall jump** : **un seul** par saut. Exception : certains murs
  marqués (tags, grillages) permettent d'enchaîner, pour atteindre des
  toits et des secrets.

**Le bonhomme.**
- **Petites jambes chromées et arrondies**, dans le style des bras.
- Coucou après 5 s d'attente ; **après 30 s il s'allonge** au sol, mains
  derrière la tête, et se relève d'un bond dès qu'on bouge.

## 12. Animation et combat (validé)

**Combos** (décrits dans les données, `moves` : « next » = coup suivant) :
- **Clic, clic, clic** : gauche, droite, puis **DROP**, le 3e coup, gros
  coup qui **projette l'ennemi au loin** (il rebondit sur le sol et les murs).
- **Saut + clic** : **frappe plongeante**, onde de choc à l'arrivée.
- **Esquive + clic** : **contre-glitch**, on traverse l'ennemi en frappant.
- Jusqu'à deux clics sont mémorisés pendant un coup, pour enchaîner sans
  rater ; chaque coup avance vers l'ennemi proche (visée douce).

**Esquive** : onde de pixels au sol, propulsion dans la direction choisie
(ZQSD), images fantômes pixélisées, le bonhomme se « bit-crushe »
(résolution 88 → 30 pixels). Une fois en l'air par saut. Recharge 0,7 s.

**Impact frames** (seulement sur le DROP et la frappe plongeante) :
pause d'impact, image inversée noir et blanc puis très contrastée avec
lignes de concentration, puis **plan ciné** en contre-plongée sur le côté,
au ralenti, avant de revenir à la caméra normale. Les coups normaux ont
une micro-pause, une petite secousse et des étincelles.

**Bonhomme souple** : coudes, genoux, mains rondes, buste séparé du
bassin. Chaque articulation suit sa cible avec un ressort (léger
dépassement), la tête compense la torsion du buste et traîne un peu,
le corps s'écrase et s'étire (sauts, réceptions, coups), il anticipe
ses coups et respire à l'arrêt.

## 13. Sorts, ondes, buffs / débuffs et fusions (démo web)

Tout est décrit dans les données de la démo (`spells`, `waves`, `statuses`,
`fusions`). Ajouter une fusion = ajouter une ligne. Les statuts sont
génériques : joueur et ennemis utilisent les mêmes.

**Caméra** : plus aucun mouvement de caméra pendant les impact frames et
les combos (plan ciné, ralenti et secousses retirés). La pause d'impact et
les impact frames restent.

**Touches** : C Bitcrush (esquive de base) · **A E : 2 sorts de base** ·
**R T : 2 sorts déjà fusionnés** (choisis parmi les 18 fusions, lancés
d'une seule touche, visés au centre de l'écran) · clic droit maintenu :
viser, clic : tirer l'onde choisie (1 sinus, 2 carré, 3 triangle) · Tab :
fenêtre « Mes sorts » (sauvegardée dans le navigateur).

*Validé ensuite* : plus de fusion en direct (« F + deux touches »). Une
fusion équipée a sa propre recharge et ne bloque pas ses sorts d'origine.

| Sort | Effet | Recharge |
|---|---|---|
| Bitcrush | esquive de base | 0,7 s |
| Gater | contre : parfait ≤ 0,15 s avant le coup (annulé + ennemi étourdi 1,2 s, recharge ramenée à 1 s), bon ≤ 0,35 s (−70 %) | 4 s |
| Reverb | 10 s : chaque coup pose un écho de 3 dégâts/s pendant 4 s | 14 s |
| Delay | dans les 2 s après un combo : double fantôme qui rejoue les 3 derniers coups à 60 % ; raté = recharge divisée par 2 | 12 s |
| Distortion | 8 s : dégâts infligés ×1,5, reçus ×1,5 | 15 s |
| Chorus | 10 PV/s pendant 6 s, attaquer coupe le soin | 18 s |
| Saturation | 6 s : vitesse ×1,6, esquive quasi sans recharge, 3 sauts ; puis 5 s d'essoufflement (lent, sans esquive) | 20 s |

| Onde | Effet | Recharge |
|---|---|---|
| Sinus | fusil à pompe : 5 ondes, 12 dégâts de près → 3 à 14 m | 1 s |
| Carré | sniper : rayon instantané 80 m, 34 dégâts | 3,5 s |
| Triangle | 9 dégâts à toute distance, suit légèrement la cible | 0,45 s |

Fusions (sorts fusionnés, recharge propre) :
Saut glitch 10 s · Peau d'oignon 12 s · Triple glitch 9 s · Distorsion
glitch 20 s · Déphasage 14 s · Surchauffe glitch 24 s · Écho infini 40 s ·
Distorsion douce 16 s · Chœur lointain 22 s · Saturation douce 18 s ·
Crescendo 20 s · Super saut 15 s · Tempo 18 s · Renvoi 22 s · Quitte ou
double 45 s · ondes : PWM 8 s (rayon large qui traverse), Onde douce 7 s
(4 orbes à tête chercheuse), Dent de scie 9 s (rafale de 8 tirs).
Le Gater ne fusionne qu'avec le Bitcrush (sinon trop fort).

**Mode combat** : l'enceinte d'entraînement riposte une fois frappée
(12 s sans échange pour se calmer) : « onde de larsen » (projectile,
signalée 0,6 s avant) ou « boom » au corps à corps (anneau rouge au sol,
0,55 s avant). Ses attaques sont de vrais objets de la Sim : le Gater les
annule.

## 14. Monde vivant : PNJ, armes, bestiaire, dragon (démo web)

**Taille humaine** : le bonhomme mesure environ 1,85 m (longues jambes,
taille, buste, bras en deux parties). Le buste suit le bassin avec un temps
de retard (taille 55 %, buste 45 %), ce qui donne des coups plus « en
torsion ». En combat (ennemi actif à moins de 12 m), il se met en **garde**
selon son arme, rebondit sur ses appuis et reste tourné vers l'ennemi
(on tourne autour, la foulée suit la vraie direction). Quand il est
touché, il encaisse (le buste part en arrière) ; les gros coups le
**projettent au sol**, il se relève d'un bond (invincible pendant ce temps).

**Armes** (aucune arme à feu ; X ou molette pour changer ; données dans
`weapons` et `moves`) :

| Arme | Combo | Particularité | Où la trouver |
|---|---|---|---|
| Poings | jab, jab, drop | le drop projette | dès le départ |
| Baguettes | 3 frappes + roulement de 5 coups | très rapides | Kenji, au carrefour |
| Pied de micro | balayage, estoc, moulinet (tout autour) | grande allonge | DJ Taro : vaincre 3 monstres |
| Guitare-hache | 2 grands coups + power chord | lente, onde de choc, fait vaciller | Rin : lui rapporter le vinyle doré |
| Micro-fléau | 2 coups de câble + crochet | portée 5 m, ramène l'ennemi et l'étourdit | Yuna (la nuit) : vaincre une Gueule-enceinte |

**Bestiaire** (données dans `bestiary`). Chaque attaque est annoncée : une
étoile **jaune** au-dessus du monstre = on peut l'interrompre en le
frappant ; **rouge** = il est lancé (« blindé »), il faut esquiver (C) ou
placer un Gater. Les monstres vaincus lâchent une note verte qui soigne.

| Monstre | Quand | Attaques |
|---|---|---|
| Grésillon | jour et nuit | morsure, bond (souvent par deux) |
| Câblé (humanoïde de câbles, tête de jack) | jour et nuit | fouet de câble, griffe, étincelle à distance |
| Gueule-enceinte (enceinte sur 4 pattes de câble, membrane-gueule) | nuit | morsure en bond (projette au sol), griffes, cri en cône |
| Ombre sub (basse qui flotte, traîne ses câbles) | nuit | pulsation autour d'elle, infrabasse à tête chercheuse, fondu (téléportation) |

Apparitions : 3 monstres max le jour, 6 la nuit, hors de vue (22 à 60 m),
dans les rues. À l'aube, les monstres de nuit se dissolvent.

**PNJ** : 12 passants (moins la nuit) qui marchent sur les trottoirs,
s'arrêtent sur leur téléphone et fuient les monstres. Six personnages
importants (losange ◆ quand ils ont du nouveau, **F** pour parler) :
Kenji (batteur, carrefour), DJ Taro (place du parc), Rin (luthière, rue
commerçante), Yuna (chanteuse, devant le karaoké, la nuit), Vieux Sato
(au bout de la rue, il regarde la tour) et Mamie Kiko (ramen : soigne
une fois toutes les 45 s). La progression (armes, monstres vaincus,
quêtes) est sauvegardée dans le navigateur.

**Dragon de jade** (boss final, préparé) : long dragon serpentin turquoise
(écailles dessinées), ventre doré, épines, crinière, cornes et moustaches
d'or, quatre pattes. La nuit, il monte de derrière la tour et tourne
autour, à 250 m de haut. Pas encore combattable.

**Réalisme** : voitures garées (kei cars, taxis) aux phares allumés la
nuit, nuages qui dérivent et changent de teinte avec l'heure.

## 15. Virage dark fantasy, façon Ultrakill (démo web v7)

**Le bonhomme** (images de référence : grosse tête ronde de chrome clair,
corps plein et arrondi aux membres épais, façon icône chromée). Il est **semi-liquide** : ses membres
s'allongent quand il frappe (le bras qui cogne s'étire jusqu'à ×1,9), ses
jambes s'étirent à chaque foulée quand il fonce, ses bras traînent
derrière lui, il s'étale dans le sens de la vitesse et s'étire en chute.
Sa tête est montée sur un ressort et traîne derrière lui. Il laisse des
gouttes de chrome quand il va très vite. Les passants et le Câblé gardent
des proportions humaines (deux silhouettes pour le même squelette).

**Déplacements** (toujours en 3e personne) :

| Touche | Effet |
|---|---|
| ZQSD | course rapide permanente (11 m/s) |
| Maj | dash (Bitcrush) : 3 charges qui se rechargent (0,9 s chacune), invincible, aussi en l'air |
| Maj puis Espace | dash-saut : on garde 80 % de la vitesse du dash |
| C au sol | glissade (17 m/s, on peut tourner un peu) ; sauter garde l'élan |
| C en l'air | écrasement : chute à 55 m/s, cratère, dégâts autour |
| Espace juste après un écrasement | rebond, d'autant plus haut que la chute était longue |
| Espace contre un mur | 3 wall jumps par saut (à l'infini sur les murs tagués) |

En l'air, l'élan est conservé : on peut seulement l'orienter. Au sol,
l'excès de vitesse retombe doucement. Les clics en l'air donnent des coups
normaux (on reste suspendu un instant).

**Armes** : 10 en tout. La **Twisted Sword** est l'arme de départ (deux
taillades, un estoc qui fonce, puis la torsion qui tourne deux fois et
projette). Nouvelles armes posées sur des **autels** (pierre noire, bougies) :

| Arme | Combo | Autel |
|---|---|---|
| Boom Box | 2 grands coups + « Drop the bass » : une ligne d'impacts fend le sol devant | au fond du parc |
| Faux à cordes | 2 balayages à 240° + moisson (2 tours, ramène les ennemis) | ruelle taguée |
| Diapason | 2 estocs rapides + résonance (étourdit 1,6 s) | toit du karaoké |
| Poings sub | 2 coups lourds + infra-uppercut (envoie en l'air) | bout de la rue, au nord |

**Ultra-violence** : les coups font gicler du sang qui tache le sol ; les
monstres tués explosent en morceaux qui volent, rebondissent et saignent.
Comme dans Ultrakill, **frapper de près soigne** (35 % des dégâts infligés
à moins de 4,5 m). **Jauge de style** : D Détraqué, C Cruel, B Brutal,
A Atroce, S Sanguinaire, SS Sadique, SSS Sans pitié, puis TWISTED ; bonus
pour les morts aériennes, le jonglage, l'écrasement, le changement d'arme ;
elle baisse quand on est touché ou qu'on traîne.

**Impacts au sol** (sans bouger la caméra) : fissures, débris qui
rebondissent, onde de choc, poussière, courte pause d'impact.

**Monstres uniquement la nuit**, jusqu'à 9 à la fois, plus rapides et plus
agressifs (ils viennent de loin). Au lever du jour, ils se dissolvent. Le
jour sert à explorer, parler et trouver les autels.

**Ambiance dark fantasy** : jour couvert et brumeux, crépuscule couleur
sang, nuit d'encre sous une **lune rouge** ; pierre sombre, flèches
gothiques sur les toits, cerisiers rouge sang, lanternes à flamme qui
vacillent, braises et cendres dans l'air, corbeaux, tour en fer noir aux
anneaux rouges. Étalonnage de l'image (moins saturée, plus contrastée),
vignette et grain de film.
