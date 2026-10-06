# Twisted : le jeu

Jeu de combat 3D dans l'univers du plugin Twisted (abrasion), fait avec
**Godot 4.6**. Voir [DESIGN.md](DESIGN.md) pour la conception et
[TODO.md](TODO.md) pour l'avancement.

## Installer Godot (une seule fois)

1. Va sur <https://godotengine.org/download/windows/>.
2. Télécharge **Godot Engine 4.6** (le bouton principal, pas la version .NET).
3. Décompresse le fichier `.zip` où tu veux, par exemple dans `Documents\Godot`.
4. Lance `Godot_v4.6...-stable_win64.exe`. Il n'y a rien d'autre à installer.

## Récupérer la dernière version du jeu

Dans PowerShell, depuis le dossier où tu as cloné Twisted :

```powershell
cd $HOME\Twisted
git fetch origin
git checkout claude/affectionate-lamport-twaiwl
git pull
```

## Ouvrir et lancer le jeu

1. Dans la fenêtre d'accueil de Godot, clique **Importer**.
2. Choisis le fichier `Twisted\game\project.godot`, puis **Importer et modifier**
   (la première ouverture prend quelques secondes).
3. Appuie sur **F5** (ou le bouton ▶ en haut à droite) pour jouer.

Si quelque chose ne marche pas, copie-moi le texte rouge de l'onglet
**Sortie** ou **Débogueur** en bas de l'éditeur.

## Organisation

| Dossier | Contenu |
|---|---|
| `data/` | tous les réglages du jeu (vie, dégâts, vitesses) en JSON, modifiables à la main |
| `core/` | la logique de jeu pure, sans affichage (prête pour le multijoueur) |
| `scenes/` | personnages, ville, caméra, niveau (l'affichage) |
| `vfx/` | shaders (façades, rendu pixélisé) |
| `ui/` | interface à l'écran |
| `reference/` | images de référence (le bonhomme d'abrasion) |
