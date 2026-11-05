# Audit de RAW 2

Bilan de l'audit de robustesse (v8.3) : ce qui a été cherché, ce qui a été trouvé et corrigé, et
comment refaire les mêmes contrôles avant chaque version.

## Méthode

| Contrôle | Outil | Résultat après correctifs |
|---|---|---|
| Mémoire (débordements, use-after-free) et comportements indéfinis | GCC ASan + UBSan sur tous les tests | propre |
| Courses entre le fil audio et l'interface | GCC TSan, `raw2_race` (presets d'usine, packs, états complets, paramètres, INIT pendant que le son tourne) | 0 alerte, sortie saine |
| Temps réel : allocations dans `process()` | `raw2_torture` (compteur `operator new` limité au fil audio, 74 presets, arpège, accords, unisson, polyphonie) | 0 allocation |
| Paramètres extrêmes, 22 à 192 kHz, blocs de 1 à 4096 | `raw2_torture` | aucun NaN/Inf, crête ≤ 1,0 |
| Dénormaux dans les queues de réverbe | `raw2_torture` (coût d'un bloc après 10 s de silence) | pas d'emballement (x86 DAZ/FTZ, ARM FPCR.FZ) |
| Fichiers corrompus ou piégés | `raw2_fuzz` (mutations de vrais fichiers) : JSON, état de projet, preset `.raw`, `.raw2pack`, WAV 24 bits/float, SFZ | aucun plantage, sortie finie |
| File MIDI de l'Audio Unit | 4 fils producteurs, 800 000 notes | aucune perte hors file pleine, aucun doublon |
| Relecture statique | clang-tidy (bugprone, clang-analyzer, cert, concurrency) | faux positifs uniquement (voir plus bas) |

## Problèmes trouvés et corrigés

1. **Plantage possible du moteur (grave).** Avec certains réglages extrêmes (hauteur très haute +
   fréquence d'échantillonnage basse, ou vitesse de lecture d'un sample énorme), l'incrément de
   phase dépassait un cycle par échantillon : la lecture de table sortait du tableau
   (`renderVoice`). Incréments maintenant bornés (sous Nyquist), phase repliée par `floor`, index
   de table et positions de sample contrôlés.
2. **Course de données sur les LFO dessinés (moyen).** L'interface écrivait la table d'un LFO
   pendant que l'audio la lisait. Les cases sont désormais atomiques (`Modulation::tableAt`).
3. **Paramètre NaN (moyen).** Un hôte ou un fichier pouvait envoyer NaN, qui contaminait tout le
   son jusqu'au rechargement. `ParamStore::set` ignore NaN.
4. **Écriture de fichier non atomique (moyen).** Un disque plein ou un plantage pendant la
   sauvegarde tronquait le preset ou le pack. Écriture dans `fichier.tmp~` puis remplacement d'un
   coup (`MoveFileExW` / `rename`) : l'ancien fichier reste intact en cas d'échec.
5. **MIDI de l'Audio Unit hors fil de rendu (moyen, macOS).** Certains hôtes envoient les notes
   depuis un autre fil que le rendu : l'ancien tableau pouvait perdre ou dupliquer des notes.
   Remplacé par une file sans verrou multi-producteurs bornée (Vyukov).
6. **Tampons nuls en VST3 (faible).** Certains hôtes appellent `process()` sans tampon de sortie
   (vidage des paramètres). Ces appels sont maintenant gérés sans accès au pointeur nul.
7. **WAV float hors norme (faible).** Un WAV 32 bits float avec des valeurs énormes (1e30) passait
   tel quel dans le moteur. Échantillons bornés à ±4 à la lecture.

## Avertissements de clang-tidy laissés tels quels

- « missing comma » : concaténations voulues de chaînes UTF-8 (`"AL\xC3\x89" "A"`).
- `getenv` / `readdir` « not thread safe » : appelés seulement depuis le fil de l'interface.
- `atoi` / `atof` : valeurs bornées juste après (SFZ, noms de fichiers numérotés).
- divisions entières et compteurs flottants dans l'interface : calculs de grille voulus.
- accès à `optional` non vérifié : chaque accès est précédé d'un test.

## Refaire l'audit

```sh
# sanitizers mémoire + UB sur les tests
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DRAW2_BUILD_AUDIT=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j && (cd build-asan && ctest)

# courses entre fils (20 s de changements de presets pendant le son)
cmake -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRAW2_BUILD_AUDIT=ON -DCMAKE_CXX_FLAGS="-fsanitize=thread"
cmake --build build-tsan --target raw2_race && build-tsan/raw2_race "$(mktemp -d)" 20

# temps réel, paramètres extrêmes, dénormaux
build-asan/raw2_torture 150

# fuzzing : raw2_fuzz <json|state|preset|pack|wav|sfz> <fichier graine> <itérations> [HOME]
build-asan/raw2_fuzz preset "mon preset.raw" 2000 "$(mktemp -d)"
```
