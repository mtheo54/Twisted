# Twisted : le jeu · TODO

Légende : [x] fait · [ ] à faire · [~] en cours

## Préparation

- [x] Document de design (DESIGN.md)
- [x] Image du bonhomme d'abrasion (`reference/bonhomme_abrasion.png`)
- [ ] Autres vues du bonhomme (profil, dos), optionnel

## Démo web (en cours)

- [x] Démo navigateur v1 : rue, bonhomme chrome pixélisé, coups, esquive, enceinte
- [x] Démo v2 : carrefour (rue commerçante, rue résidentielle, place et parc) — **à tester par toi**
- [x] Sols texturés variés, bâtiments typés (konbini, ramen, sushi, izakaya, café, boutique, karaoké, grand magasin, boulangerie, appartements, maisons)
- [x] Objets de rue (enseignes, menus, barrières, arbres, distributeurs, poteaux électriques et fils, feux, torii, vélos…)
- [x] Sensation de vitesse (champ de vision, lignes de vitesse, poussière, accélération, secousse, bruits de pas selon le sol)
- [x] Double saut, wall jump (un seul, enchaînable sur les murs tagués de la ruelle), glissade le long des murs
- [x] Jambes du bonhomme, pose allongée après 30 s
- [x] Premier secret : vinyle doré sur le toit de la boulangerie
- [x] Démo v3 : combos (3e coup qui projette), frappe plongeante, contre-glitch — **à tester par toi**
- [x] Esquive en onde pixélisée, directionnelle, aussi en l'air, images fantômes
- [x] Impact frames façon animé + plan ciné au ralenti (DROP et frappe plongeante)
- [x] Bonhomme articulé et souple (coudes, genoux, mains, ressorts, écrasement / étirement)
- [ ] Reporter les choix validés dans le projet Godot

## Étape 1 : la rue test

- [x] 0. Projet Godot créé (à installer chez toi : voir README.md)
- [~] 1. Prototype minimal : bonhomme chrome pixélisé, caméra 3e personne, rue de 128 m, esquive, coups de poing, enceinte d'entraînement, ville lointaine, montagnes, tour (**à tester par toi**)
- [ ] 2. Skate : monter/descendre, pousser, tourner, ollie, descente d'attaque, sorts en roulant
- [ ] 3. Armes musicales : Métronome, Caisson, Vinyle + horloge musicale (BPM) et coups justes
- [ ] 4. Monstre Grésillon : IA (errer, poursuivre, attaquer, fuir), boule de larsen avalable, buff Saturation
- [ ] 5. Sort Bit crush (esquive glitch)
- [ ] 6. Sort Gater (avaler un projectile, silence du lanceur)
- [ ] 7. Fusion Saut glitch (Bit crush + Gater) + mode fusion au clic droit
- [ ] 8. Porte interactive + petit bâtiment
- [ ] 9. Jour / nuit : ciel, lune, fenêtres allumées, monstre de nuit Ombre sub
- [ ] 10. Finition visuelle : bloom, brume, tour au loin, oiseaux

## Plus tard (ne pas faire maintenant)

### Sorts et fusions
- [ ] Chorus (sort + effet audio Chorus)
- [ ] Reverb (sort + effet audio Reverb)
- [ ] Fusions avec chorus et reverb
- [ ] Autres effets audio (delay, phaser, filtre, pitch, saturation…)
- [ ] Synchronisation réelle sur le rythme de la musique (BPM)

### Monde
- [ ] Blocs de ville supplémentaires (carrefour, virage, place, quartier résidentiel)
- [ ] La tour centrale complète (treillis, anneaux d'énergie, sphère de lignes)
- [ ] Chargement progressif des zones (rayon selon la vitesse du véhicule)
- [ ] Carte complète 5-8 km², ville sans port + quartiers résidentiels
- [ ] Secrets cachés à découvrir le jour
- [ ] Fin secrète

### Ennemis
- [ ] Bestiaire de nuit (monstres forts)
- [ ] Boss final : le dragon vert-turquoise serpentin aux détails dorés

### Véhicules
- [ ] Vélo
- [ ] Autres véhicules légers

### Technique
- [ ] Lissage des mouvements sur écrans 120/144 Hz (interpolation physique)
- [ ] Tests automatiques de la logique (core/)
- [ ] Manette
- [ ] Menu, réglages de qualité graphique, sauvegarde
- [ ] Multijoueur (la Sim tourne sur le serveur, les joueurs envoient des ordres)
