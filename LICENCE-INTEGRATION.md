# Intégration de la licence abrasion.dev dans Twisted

## Ce qui a changé

- **`src/license/`** (nouveau dossier) : le module de licence, porté depuis RAW — même
  serveur, même protocole, même clé publique Ed25519 (`manager.h`). Vérification de
  signature hors ligne, machine à états (activation par clé ou compte, renouvellement,
  limites 429/503), contrôleur de fond (thread séparé, jamais l'audio), détection de
  contournement (mode BRUT).
- **`src/core/PluginCore.h/.cpp`** : un `license::LicenseController` remplace l'ancien
  `std::atomic<bool> licensed`. Démarré au constructeur, partagé par CLAP et VST3 sans
  toucher à ces deux fichiers.
- **`src/ui/Editor.h/.cpp`** : écran d'activation (clé ou compte abrasion, avec onglets),
  une pastille « Activer »/« ● Licence » toujours visible dans l'en-tête (sauf en BRUT),
  et l'écran BRUT (fond noir, « TWISTED » en blocs géométriques — pas en police système,
  pour un rendu garanti identique partout — une ligne cliquable qui ramène à l'activation).
- **`src/platform/PlatformView_{win32,cocoa,x11}`** : saisie clavier et collage ajoutés
  pour les champs du panneau de licence (sans effet sur le reste, raccourcis du DAW inclus).
- **`CMakeLists.txt`** : compile les bonnes sources de licence selon la plateforme.

## Ce qui n'a PAS changé

Le son (`Engine::process`) n'est jamais gêné par la licence, à aucun moment : seules la
tour 3D et l'interface sont verrouillées, exactement comme dans RAW. Le délai de grâce
après suppression d'une licence (jusqu'à 3 jours pour une clé, jeton 90 jours pour un
compte) est un comportement voulu, pas un bug.

## Comment tester (une fois compilé)

1. `cmake -B build -DTWISTED_DEV_BUILD=ON` puis `cmake --build build --config Release`
   pour viser ton serveur local (`http://127.0.0.1:3000`) ; sans cette option, vise
   `https://abrasion.dev`.
2. Dans le plugin : sans licence, le son et les contrôles de base marchent (mode
   découverte), la tour reste vide. Cliquer la pastille « Activer » ou le message sous
   la tour ouvre le panneau.
3. Teste : activation par clé, par compte, déconnexion, réactivation.

## Ce que je n'ai PAS pu vérifier

- **Compilation réelle sur Windows et macOS** : pas possible ici (pas de SDK Windows, pas
  de Xcode). `PlatformView_win32.cpp` et `PlatformView_cocoa.mm` ont été relus avec le
  plus grand soin mais jamais compilés.
- **Rendu visuel** : aucune machine graphique ici (pas de GL réel). La disposition du
  panneau et de l'écran BRUT n'a jamais été vue à l'écran.
- **Le parcours réseau réel** contre abrasion.dev ou un serveur local.

## Ce que j'AI pu vérifier

- **Le module licence** : testé en C++17 réel (crypto Ed25519 contre la RFC 8032, machine
  à états, détection de contournement, anti-blocage sur deux échecs identiques).
- **`PluginCore.h/.cpp`, `Editor.h/.cpp`, `PlatformView_x11.cpp`** : compilés pour de vrai
  (pas juste relus) contre les véritables en-têtes du projet (`Engine.h`, `Analyzer.h`,
  `Params.h`, `Scene.h`, `Draw2D.h`, les vrais en-têtes X11 du système), avec un stub
  OpenGL/GLX limité aux symboles standard Khronos réellement utilisés. Zéro erreur, zéro
  avertissement avec `-Wall -Wextra`.
