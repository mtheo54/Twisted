# Twisted : liste des sons à créer

Tous les sons du jeu, avec le **nom de fichier** à utiliser. Quand tes sons
sont prêts, mets-les dans `game/web-demo/sounds/` (ou envoie-les-moi) : je
les branche à la place des sons synthétisés actuels.

**Format conseillé** : `.ogg` (ou `.wav`), mono, 44,1 kHz, sans silence au
début (le son doit partir tout de suite). Volume crête autour de −3 dB.
Pour les sons très répétés (pas, coups, impacts), fais **2 ou 3 variantes**
: `nom_1.ogg`, `nom_2.ogg`, `nom_3.ogg`. Le jeu en tirera une au hasard.

Légende durée : **très court** < 0,15 s · **court** 0,15–0,5 s · **moyen** 0,5–1,5 s · **long** > 1,5 s.

---

## 1. Armes (10)

Chaque arme a un combo : des coups normaux, puis un **coup final** (le gros).
Il faut pour chaque arme : le **swing** (le coup dans le vide), l'**impact**
(quand ça touche) et le **coup final**. Les variantes sont les bienvenues.

### Twisted Sword (arme de départ, lame torsadée)
| Fichier | Quand | Durée | Idée |
|---|---|---|---|
| `twisted_sword_swing_1..3` | taillades 1 et 2, estoc | très court | sifflement de lame, aigu, un peu métallique |
| `twisted_sword_hit_1..3` | la lame touche | très court | tranchant + chair |
| `twisted_sword_twist` | coup final « TWIST! » (tourne 2 fois, 4 coups) | moyen | lame qui vrille, 4 accents rapprochés |

### Poings
| `fists_swing_1..3` | jab gauche, jab droit | très court | souffle d'air |
| `fists_hit_1..3` | le poing touche | très court | coup sourd |
| `fists_drop` | coup final « DROP! » (projette) | court | gros coup + drop de basse |

### Baguettes (de batterie)
| `sticks_swing_1..3` | 3 frappes rapides | très court | clic de baguette, sec |
| `sticks_hit_1..3` | touche | très court | caisse claire / rimshot |
| `sticks_roll` | coup final « ROULEMENT! » (5 frappes en 0,3 s) | court | roulement de caisse claire |

### Pied de micro (bâton)
| `mic_stand_swing_1..2` | balayage, estoc | court | « whoosh » grave, métal creux |
| `mic_stand_hit_1..2` | touche | très court | tube de métal qui cogne |
| `mic_stand_spin` | coup final « MOULINET! » (tourne sur lui-même) | moyen | whoosh qui tourne + larsen de micro |

### Guitare-hache
| `guitar_swing_1..2` | 2 grands coups lents | court | whoosh lourd + corde qui frotte |
| `guitar_hit_1..2` | touche | court | coup lourd + cordes qui vibrent |
| `guitar_power_chord` | coup final « POWER CHORD! » (frappe le sol) | moyen | accord saturé énorme + impact |

### Micro-fléau (micro au bout d'un câble)
| `flail_swing_1..2` | 2 coups de câble | court | câble qui fouette l'air |
| `flail_hit_1..2` | touche | très court | micro qui cogne (bruit dans le micro) |
| `flail_hook` | coup final « LARSEN-CROCHET! » (ramène l'ennemi) | moyen | larsen strident qui tire |

### Boom Box (autel du parc)
| `boombox_swing_1..2` | 2 grands coups | court | whoosh lourd, plastique |
| `boombox_hit_1..2` | touche | court | gros choc + grésillement de haut-parleur |
| `boombox_drop_the_bass` | coup final « DROP THE BASS! » (onde qui part devant) | moyen | drop de basse énorme, sub |

### Faux à cordes (autel de la ruelle)
| `scythe_swing_1..2` | 2 balayages larges | court | lame qui fend l'air + corde pincée |
| `scythe_hit_1..2` | touche | très court | tranchant |
| `scythe_harvest` | coup final « MOISSON! » (2 tours, ramène les ennemis) | moyen | deux balayages + cordes graves |

### Diapason (lance, autel sur le toit du karaoké)
| `fork_thrust_1..2` | 2 estocs | très court | sifflement aigu |
| `fork_hit_1..2` | touche | très court | « ting » métallique |
| `fork_resonance` | coup final « RÉSONANCE! » (étourdit) | moyen | diapason qui vibre (La 440), long et pur |

### Poings sub (gants-enceintes, autel au nord)
| `sub_fists_swing_1..2` | 2 coups lourds | très court | souffle grave |
| `sub_fists_hit_1..2` | touche | court | coup + basse |
| `sub_fists_uppercut` | coup final « INFRA-UPPERCUT! » (envoie en l'air) | moyen | montée de sub + boom |

### Communs à toutes les armes
| Fichier | Quand | Durée |
|---|---|---|
| `weapon_switch` | changer d'arme (X, molette) | très court |
| `weapon_unlock` | ramasser une arme sur un autel | moyen (petite fanfare sombre) |
| `counter_glitch` | contre-glitch (attaque juste après un dash) | court |
| `roll_attack` | roulade (attaque pendant une glissade) | court |
| `slam_impact` | écrasement au sol (C en l'air), cratère | moyen, très grave |

---

## 2. Sorts (7), touches A et E

| Fichier | Sort | Durée | Idée |
|---|---|---|---|
| `spell_bitcrush` | **Bitcrush** = le dash (Maj) | très court | son qui se « pixelise » (bitcrusher) |
| `spell_gater` | **Gater** (contre) : lancement | très court | son haché on/off |
| `spell_gater_success` | Gater réussi (coup bloqué) | court | son coupé net, en rafale |
| `spell_gater_perfect` | Gater parfait (ennemi étourdi) | court | idem + accent brillant |
| `spell_reverb` | **Reverb** | moyen | queue de réverbération qui s'étire |
| `spell_delay` | **Delay** (double fantôme qui rejoue tes coups) | moyen | écho qui se répète 3 fois |
| `spell_delay_fail` | Delay raté | court | écho qui avorte |
| `spell_distortion` | **Distortion** | moyen | son qui sature, grave |
| `spell_chorus` | **Chorus** (soin) | moyen | voix / nappe doublée, apaisante |
| `spell_chorus_tick` | soin en cours (toutes les 0,25 s) | très court | petite note douce |
| `spell_saturation` | **Saturation** (vitesse folle) | court | montée saturée |
| `spell_tired` | essoufflement après Saturation | court | souffle, son qui retombe |
| `spell_not_ready` | sort pas encore rechargé | très court | « bip » discret |

## 3. Ondes de visée (clic droit + clic)

| Fichier | Onde | Durée | Idée |
|---|---|---|---|
| `wave_sine_fire` | **Sinus** (fusil à pompe, 5 ondes) | court | onde sinus douce qui s'ouvre |
| `wave_square_fire` | **Carré** (sniper, rayon) | court | onde carrée agressive, claquante |
| `wave_triangle_fire` | **Triangle** (tir rapide, suit la cible) | très court | petit tir pointu |
| `wave_select` | choisir une onde (1, 2, 3) | très court | clic |
| `wave_projectile_hit` | une onde touche | très court | |

## 4. Fusions (18), touches R et T

Une fusion = deux sorts combinés : le son peut mélanger les deux.

| Fichier | Fusion | Mélange de |
|---|---|---|
| `fusion_tp` | Saut glitch (téléportation derrière l'ennemi) | Bitcrush + Gater |
| `fusion_oignon` | Peau d'oignon (intouchable, traverse) | Bitcrush + Reverb |
| `fusion_triple` | Triple glitch (3 dashs) | Bitcrush + Delay |
| `fusion_dist_x` | Distorsion glitch | Bitcrush + Distortion |
| `fusion_dephasage` | Déphasage (étourdit autour) | Bitcrush + Chorus |
| `fusion_surchauffe` | Surchauffe glitch | Bitcrush + Saturation |
| `fusion_echo_infini` | Écho infini (dégâts sans fin) | Reverb + Delay |
| `fusion_dist_soft` | Distorsion douce | Reverb + Distortion |
| `fusion_choeur` | Chœur lointain (soin long) | Reverb + Chorus |
| `fusion_sat_soft` | Saturation douce | Reverb + Saturation |
| `fusion_crescendo` | Crescendo (chaque coup plus fort) | Delay + Distortion |
| `fusion_super_saut` | Super saut | Delay + Chorus |
| `fusion_tempo` | Tempo | Delay + Saturation |
| `fusion_renvoi` | Renvoi (renvoie les dégâts) | Distortion + Chorus |
| `fusion_all_in` | Quitte ou double | Distortion + Saturation |
| `fusion_all_in_won` / `fusion_all_in_lost` | Quitte ou double gagné / perdu | |
| `fusion_pwm` | PWM (rayon large qui traverse) | Sinus + Carré |
| `fusion_onde_douce` | Onde douce (4 orbes chercheurs) | Sinus + Triangle |
| `fusion_dent_scie` | Dent de scie (rafale de 8 tirs) | Carré + Triangle |

---

## 5. Déplacements

| Fichier | Quand | Durée |
|---|---|---|
| `step_asphalt_1..3`, `step_tiles_1..3`, `step_cobble_1..3`, `step_plaza_1..3`, `step_grass_1..3`, `step_gravel_1..3`, `step_metal_1..3`, `step_wood_1..3` | pas selon le sol | très court |
| `jump`, `double_jump` (salto), `wall_jump` | sauts | très court |
| `land_heavy` | atterrissage après une grande chute | court |
| `dash_jump` | dash-saut | court |
| `slide_start` + `slide_loop` (en boucle) | glissade | court + boucle |
| `slam_fall` | chute de l'écrasement (avant l'impact) | court |
| `slam_bounce` | rebond après écrasement | court |
| `body_liquid_1..3` | le corps liquide s'étire / gouttes de chrome | très court, discret |

## 6. Monstres (la nuit)

Chaque attaque a une **annonce** (avant le coup, pour prévenir le joueur)
puis le **coup**. Les annonces sont très importantes pour le gameplay.

| Fichier | Monstre | Quand |
|---|---|---|
| `gresillon_idle`, `gresillon_step_1..3` | Grésillon (petit parasite à pattes de cuivre) | grésillement, pas de pattes |
| `gresillon_bite_windup` / `gresillon_bite` | | morsure |
| `gresillon_pounce_windup` / `gresillon_pounce` | | bond |
| `cable_idle` | Câblé (humanoïde de câbles, tête de jack) | présence (buzz électrique) |
| `cable_whip_windup` / `cable_whip` | | fouet de câble |
| `cable_claw_windup` / `cable_claw` | | griffe |
| `cable_spark_windup` / `cable_spark` | | étincelle lancée |
| `gueule_idle`, `gueule_step_1..3` | Gueule-enceinte (enceinte affamée sur pattes) | respiration, pas lourds |
| `gueule_bite_windup` / `gueule_bite` | | morsure en bond (projette au sol) |
| `gueule_claws_windup` / `gueule_claws` | | griffes |
| `gueule_scream_windup` / `gueule_scream` | | cri en cône (larsen monstrueux) |
| `ombre_idle` | Ombre sub (basse qui flotte) | bourdonnement sub |
| `ombre_pulse_windup` / `ombre_pulse` | | pulsation autour d'elle |
| `ombre_infrabass_windup` / `ombre_infrabass` | | projectile d'infrabasse |
| `ombre_fade` / `ombre_appear` | | disparaît / réapparaît |
| `monster_spawn` | tous | un monstre apparaît |
| `monster_hit_1..3` | tous | un monstre est touché (gore) |
| `monster_explode_1..3` | tous | un monstre explose en morceaux |
| `gib_bounce_1..3` | tous | morceaux qui retombent |
| `monster_interrupted` | tous | on a interrompu son attaque |

Enceinte d'entraînement (place du parc) : `dummy_larsen_windup` / `dummy_larsen`, `dummy_boom_windup` / `dummy_boom`, `dummy_hit_1..3`.

## 7. Joueur

| Fichier | Quand |
|---|---|
| `player_hurt_1..3` | il est touché |
| `player_knockdown` | il est projeté au sol |
| `player_heal` | il se soigne (sang, ramen) |
| `player_death` | K.O. |
| `player_wave` / `player_yawn` | il salue / s'allonge après 30 s sans bouger |

## 8. Monde, ambiance et interface

| Fichier | Quand | Durée |
|---|---|---|
| `ambience_day` (boucle) | ville le jour, brumeuse | long |
| `ambience_night` (boucle) | nuit, inquiétant | long |
| `night_falls` | la nuit tombe (touche N) | moyen |
| `day_rises` | le jour se lève | moyen |
| `dragon_roar` | le dragon de jade se réveille au loin | long |
| `lantern_fire` (boucle) | flammes des lanternes et des autels, proche | long |
| `secret_found` | vinyle doré trouvé | moyen |
| `npc_talk_1..3` | parler à un personnage (F) / ligne suivante | très court |
| `quest_update`, `quest_done` | quête avancée / terminée | court |
| `style_rank_up`, `style_rank_down` | la jauge de style monte / descend | court |
| `ui_open`, `ui_close`, `ui_select` | fenêtre Tab | très court |
| `banner` | message au centre de l'écran | très court |

## 9. Musique (plus tard, facultatif)

`music_day`, `music_night`, `music_combat` (boucles) : si tu en fais, garde
le même tempo pour pouvoir passer de l'une à l'autre en rythme.
