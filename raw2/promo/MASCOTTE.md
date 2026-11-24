# Le bonhomme d'ABRASION : la mascotte

Le bonhomme d'ABRASION est le personnage de la marque. On le voit dans les vidéos, sur les pages de
vente et dans les visuels. Il présente les plugins et parle au public avec ses expressions.

## D'où il vient, et pourquoi il a changé

Il est parti de l'image `game/reference/bonhomme_abrasion.png` : un bonhomme de chrome qui fait coucou.
Cette silhouette ressemble trop à des icônes déjà connues, comme le « buddy » de MSN Messenger. Pour
qu'ABRASION puisse en faire sa mascotte, il a reçu quatre signes distinctifs, à garder sur chaque visuel.

## Signes distinctifs (obligatoires)

1. **Visière orange** : un bandeau sombre sur l'avant de la tête, où ses yeux s'affichent en
   pixels. Il n'a pas de bouche : tout passe par les yeux.
2. **Logo ABRASION sur la poitrine** : un carré noir avec un carré orange au centre, qui pulse
   sur le rythme.
3. **Anneau lumineux** : un anneau orange dans l'espace entre la tête, qui flotte, et le corps.
4. **Queue en câble jack** : un câble part du bas du dos, traîne derrière lui et finit par une
   fiche orange. On le reconnaît même de dos.

## Silhouette

Les mesures sont données pour une hauteur totale d'environ 2,6 :
- **Tête** : une sphère de chrome (rayon 0,39), séparée du corps par un espace d'environ 0,09.
- **Torse** : une pilule large (rayon 0,36, aplatie à 0,86 en profondeur), au chrome plus sombre que la tête.
- **Bras et jambes** : des tubes épais (rayon 0,15), souples comme ceux d'un personnage animé, terminés par des boules.
- **Chrome** : un matcap gris, avec un reflet blanc en haut à gauche et un léger reflet orange en bas.

## Expressions de la visière

| Nom | Quand |
|---|---|
| neutre | au repos (il cligne régulièrement) |
| cligne | clignement |
| content | coucou, réussite, présentation |
| surpris | chute, découverte |
| clin | complicité, au revoir |
| concentre | effort, appui sur un bouton, réglage |
| aie | choc, impact |
| malin | teaser (« Écoute ça… ») |
| etoiles | émerveillement, drop |

Quand il parle, ses yeux s'aplatissent et rebondissent au rythme des syllabes.

## Couleurs

- chrome : matcap de `#8C8F97` à `#EEF0F5` ;
- visière : `#060606` ;
- yeux : `#FF5A10` ;
- logo et anneau : orange ABRASION `#FF4D00`, lumineux.

## À éviter

- Lui dessiner une bouche ou des yeux en dehors de la visière.
- Le montrer sans au moins deux de ses signes distinctifs.
- Changer l'orange ou rendre le chrome coloré.

## Fichiers

- `bonhomme.js` : le modèle 3D, ses expressions (`FACES`) et ses poses (`POSE` : coucou, marche,
  saut, frappe, surf, pousse, assis).
- La planche de référence (vues, expressions, couleurs) est générée à partir du modèle.

## Nom

Il n'a pas encore de nom : pour l'instant, c'est « le bonhomme d'ABRASION ».
