# Packs de presets RAW 2

RAW 2 lit ses packs dans le dossier **`Documents\Raw 2`** de l'utilisateur. Le plugin crée ce
dossier tout seul au premier lancement. Il relit le dossier à chaque ouverture de la fenêtre
**CHOIX DU PRESET** (onglet **USER**), donc un pack ajouté apparaît sans redémarrer le DAW.

```
Documents\Raw 2\
  Mes presets\                  MES PRESETS : bouton ENREGISTRER et « + SAUVER LE SON ACTUEL »
  NUIT FROIDE\                  un pack = un dossier de fichiers .raw
    pack.json                   (facultatif) nom, auteur, version du pack
    01 Verglas.raw
    02 Souffle noir.raw
    basses\                     (facultatif) un niveau de sous-dossiers pour ranger
      Mur noir.raw
  RAGE LOURDE.raw2pack          un pack = un seul fichier .raw2pack (format de la maquette)
  un preset isolé.raw           les .raw posés à la racine sont regroupés dans DIVERS
  Wavetables\                   exports WAV de la page WAVETABLE (ce n'est pas un pack)
  Exports\                      sons rendus par « → GLISSER EN .WAV » (ce n'est pas un pack)
  Instruments\                  instruments multi-échantillonnés SFZ (voir docs/INSTRUMENTS.md)
    Wurlitzer EP200\
      Wurlitzer EP200.sfz
      samples\…
      LICENSE.txt
```

## Installer un pack

- Copier le dossier du pack (ou le fichier `.raw2pack`) dans `Documents\Raw 2`.
- Ou **glisser-déposer** le dossier, le `.raw2pack` ou des `.raw` sur la fenêtre CHOIX DU
  PRESET : le plugin les copie au bon endroit. Un `.raw` isolé est rangé dans MES PRESETS.
- Le bouton **[ INSTALLER UN PACK… ]** ouvre le dossier `Documents\Raw 2` dans l'explorateur.
- **[ RETIRER ]** envoie le pack à la corbeille de Windows (deuxième clic pour confirmer).
- **[ PACKS DÉMO ]** installe deux packs d'exemple, un de chaque format.

## Format `.raw` (un preset)

Les fichiers `.raw` sont ceux que le plugin écrit avec ENREGISTRER : un JSON
`{"format": "RAW2-preset", "version": 5, "name", "category", "style", "params", "fxOrder", "oscA", "oscB", "matrix"}`,
plus `"instA"` / `"instB"` (`{"name": "Wurlitzer EP200"}`) quand un oscillateur joue un
instrument de `Documents\Raw 2\Instruments`. Un preset ne contient que le **nom** de
l'instrument : le pack qui s'en sert doit être distribué avec la banque, ou dire où la trouver.
`category` vaut `basse`, `lead`, `pad`, `pluck`, `arp` ou `fx` (filtres de la fenêtre), et
`style` est le genre affiché (menu GENRE). Pour fabriquer un pack, le plus simple est de régler
les sons dans le plugin, de les enregistrer puis de déplacer les fichiers de `Mes presets` vers le
dossier du pack.

`pack.json` (facultatif) : `{"name": "NUIT FROIDE", "author": "ABRASION", "version": "1.0"}`.
Sans ce fichier, le nom du dossier sert de nom de pack.

## Format `.raw2pack` (un pack en un fichier)

C'est le format de la maquette : chaque preset part d'un preset d'usine (`base`, de 0 à 71) et
change quelques réglages.

```json
{
  "format": "raw2-pack",
  "name": "RAGE LOURDE",
  "author": "ABRASION",
  "version": "1.0",
  "presets": [
    { "name": "Mur noir", "tags": "basse", "base": 7, "p": { "aLvl": 0.9 } },
    { "name": "Béton chaud", "tags": "basse", "base": 8, "p": { "m1": 60, "eA_a": 0.1, "l1_r": 0.6 } }
  ]
}
```

- `tags` : le premier mot, s'il s'agit d'une catégorie (`basse`, `lead`…), sert de catégorie.
- `p` : réglages, avec les clés de la maquette ou les clés des fichiers `.raw` :
  `aPos`, `aLvl`, `bPos`, `bLvl`, `aWarp`, `bWarp`, `aPan`, `bPan`, `speed`, `birth`, `qty`, `master`,
  `m1`, `m2`, `m3` (de -100 à 100), `smooth`, `fxWet`, enveloppes `eA_a` `eA_d` `eA_s` `eA_r` `eA_c`
  et `eB_…`, LFO `l1_r` `l1_p` `l1_f` `l1_s` et `l2_…`, effets `x_<effet>_0|1|2` (réglages),
  `x_<effet>_m` (MIX), `x_<effet>_md` (modulation) avec `<effet>` parmi `hyper dist flang phase chor dly rev comp eq filt`.
  Toutes les valeurs vont de 0 à 1, sauf indication contraire.
- Facultatif : `oct` et `st` (`{"a": -1, "b": 0}`), `mode` (`{"a": 0..4, "b": 0..4}`),
  `rule` (`"life"` ou `"hl"`), `fx` (`{"on": {"rev": true, "dist": false}}`),
  `wt` (`[["saw", 0, 1], ["glass", 0.2, 0.8]]` : forme, début et fin du morphing, pour les deux oscillateurs).
- Une entrée peut aussi être un preset complet (le contenu d'un fichier `.raw`).

Limites : 500 presets par pack, fichier de 4 Mo au plus. Les textes sont nettoyés (40 caractères
pour un nom).
