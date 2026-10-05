# Twisted · 物見櫓

Plugin de mixage pour DAW qui reconnaît la source audio (kick, basse, voix,
bus batterie, guitare/synthé, master) et choisit le réglage adapté :
saturation, largeur stéréo, enhance, suréchantillonnage, filtres, gain
staging automatique. Objectifs : latence nulle, DSP SIMD, threads sans
verrou, build CMake uniquement.

Étape en cours : **valider l'interface graphique**.

- Maquette interactive : ouvrir `ui-prototype/index.html` dans un navigateur.
- Spécification de l'interface : [`docs/UI_SPEC.md`](docs/UI_SPEC.md).

La source de la maquette est `ui-prototype/tower.body.html` ;
`ui-prototype/build.sh` régénère `index.html`.
