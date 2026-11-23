# RAW 2 v1.0.0 — passation à ONIRIQUE

RAW 2 est un synthé d'ABRASION, distribué par ONIRIQUE (https://onirique.store). Ce document
explique ce qui est livré, ce qui a été vérifié et ce qu'il reste à faire avant la mise en vente.

## Ce qui est livré

| Fichier | Pour qui | Contenu |
|---|---|---|
| `RAW2-Setup-v1.0.0.exe` | clients Windows | installateur complet : plugin VST3, RAW 2 sans DAW, 23 instruments, pack de 34 presets « RAW 2 · INSTRUMENTS », désinstallation propre |
| `RAW2-macOS-1.0.0.dmg` | clients Mac | `Installer RAW 2.pkg` (VST3 + Audio Unit), copie manuelle, notice |
| `RAW2-instruments-v1.0.0.zip` | clients Mac | les 23 instruments et le pack de presets, à décompresser dans `~/Documents/Raw 2` (sous Windows, l'installateur s'en charge) |
| `RAW2-sources-v1.0.0.zip` | développeur | code source complet, pour recompiler et signer |
| `ONIRIQUE.md` | vous | ce document |

## Fiche produit (texte de base pour la page de vente)

**RAW 2 — synthé wavetable, instruments réels et vie cellulaire, par ABRASION.**

- 2 oscillateurs wavetable : tables jusqu'à 256 images, import de tables Serum / Vital, de
  n'importe quel son découpé en cycles ou resynthétisé, glisser-déposer d'un fichier audio.
- 12 modes de warp par oscillateur, dont FM, AM et RM croisées entre A et B.
- Filtre multimode à 10 types (LP, HP, BP, notch, peak, phaser, comb ±, formant, morph) avec
  drive, en 12 ou 24 dB.
- 23 instruments réels multi-échantillonnés (Wurlitzer, CP80, Pianet, piano à queue, string
  machines, cuivres et nappes OB, basses et sifflet G-funk, sax, vibraphone), jouables sur
  chaque oscillateur et mélangeables avec le synthé.
- Unison jusqu'à 16 voix, 32 voix de polyphonie, POLY / MONO / LEGATO, glide, mode 808.
- 4 enveloppes, LFO dessinables, routes de modulation par note avec courbe, 4 macros.
- Aftertouch et MPE (Audio Unit et VST3 Note Expression).
- 12 effets : hyper/dim, distorsion, flanger, phaser, chorus, delay, réverbe, compresseur, EQ,
  filtre, vintage (bande, vinyle, lo-fi), pump calé sur le tempo.
- Arpégiateur calé sur l'hôte, accords en un doigt, export du son en WAV par glisser-déposer.
- 72 presets d'usine + 34 presets instruments, packs de presets partageables (`.raw2pack`).
- 6 thèmes, zoom de 75 à 150 %.

**Configuration requise**
- Windows 10 ou 11, 64 bits, hôte VST3 (FL Studio, Ableton Live, Bitwig, Reaper, Cubase…).
- macOS 10.15 ou plus récent, Apple Silicon et Intel, VST3 ou Audio Unit (Ableton Live,
  Logic Pro, GarageBand…).
- Environ 280 Mo d'espace disque avec les instruments.

## Ce qui a été vérifié

- 7 suites de tests automatiques (moteur, synthèse, presets, fichiers, interface).
- Audit de robustesse (voir `docs/AUDIT.md`) : mémoire, concurrence entre fils,
  zéro allocation dans le fil audio, paramètres extrêmes de 22 à 192 kHz, fichiers corrompus.
- Windows : l'installateur est testé (installation, chargement du plugin dans un hôte VST3 de
  test, désinstallation).
- Mac : compilation universelle, tests et validation Audio Unit (`auval`) sur un Mac de
  GitHub à chaque modification.
- Compatibilité des presets : un preset `.raw` enregistré en v1.0.0 s'ouvrira dans toutes les
  versions suivantes.

## À faire avant la mise en vente

1. **Essai dans de vrais logiciels de musique.** RAW 2 n'a été testé que par des outils
   automatiques, jamais par une personne dans Ableton, FL Studio ou Logic. Une heure sur
   Windows et une heure sur Mac : recharger un projet, automation, plusieurs RAW 2 ouverts,
   changer de preset pendant la lecture, glisser un fichier audio, jouer les instruments.
2. **Signature Mac (bloquant).** Le plugin n'a qu'une signature locale : chez les clients,
   macOS le bloque (« impossible de vérifier le développeur ») et il faut passer par
   Réglages Système. Il faut un compte Apple Developer (99 $ par an), un certificat
   « Developer ID Application » et « Developer ID Installer », puis la notarisation Apple.
   La compilation automatique peut le faire si on lui donne ces certificats.
3. **Signature Windows (conseillée).** Sans signature, Windows affiche « Windows a protégé
   votre ordinateur » ; on peut passer outre, mais ça inspire moins confiance. Un certificat
   de signature de code (par exemple Azure Trusted Signing) supprime ce message.
4. **Protection / licence.** RAW 2 ne contient aucune vérification de licence : toute personne
   qui a le fichier peut l'utiliser. Si ONIRIQUE Central doit activer le plugin (clé, compte),
   il faut décrire son fonctionnement (serveur, format de clé) pour l'intégrer au plugin.
   Le son ne doit jamais être coupé par la licence pendant un projet.
5. **Crédits obligatoires (CC BY 3.0)** : le texte suivant doit figurer sur la page de vente
   ou dans le manuel (il est déjà dans la fenêtre À PROPOS du plugin) :

   > Wurlitzer EP200, Yamaha CP80 and Hohner Pianet T samples by Greg Sullivan
   > (http://www.sullivang.net/), SFZ mapping by kinwie
   > (https://github.com/sfzinstruments/GregSullivan.E-Pianos), licensed under CC BY 3.0
   > (https://creativecommons.org/licenses/by/3.0/). Modified: trimmed, converted, re-mapped
   > for RAW 2.

6. **Piano à queue : licence à confirmer.** Les échantillons du « Grand Piano » sont présentés
   comme placés dans le domaine public par Akai en 2000, mais leur dépôt d'origine ne contient
   aucun fichier de licence. Pour une vente sans aucun risque, on peut le retirer : il est
   dans une section à part de l'installateur et un seul preset (« Piano Trap ») l'utilise.
   Tous les autres instruments sont en CC0 (libres, sans condition) ou CC BY 3.0 (crédit
   ci-dessus).

## Support client

En cas de problème, demander au client le fichier `Documents/Raw 2/RAW2-journal.txt`
(Windows et Mac). Il contient la version, les presets chargés et les erreurs rencontrées.

## Recompiler

Voir `README.md` dans les sources : Windows avec Visual Studio 2026 et CMake, Mac avec Xcode et
CMake (binaire universel), ou le workflow GitHub Actions `raw2-macos` qui fabrique le `.dmg`.
L'installateur Windows se construit avec NSIS (`packaging/windows/README.md`), la banque
d'instruments avec `tools/build_instruments.py` (`docs/INSTRUMENTS.md`).
