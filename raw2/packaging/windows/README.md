# Installateur Windows (RAW2-Setup-vX.exe)

`raw2.nsi` (NSIS 3, cible amd64-unicode) installe :

- `RAW2.vst3` dans `C:\Program Files\Common Files\VST3\` (remplacé en entier) ;
- `RAW2Preview.exe` et `LISEZ-MOI.txt` dans `C:\Program Files\ABRASION\RAW 2\`, avec des
  raccourcis dans le menu Démarrer et une entrée « Applications » pour désinstaller ;
- en option, la banque d'instruments et le pack « RAW 2 · INSTRUMENTS » dans `Documents\Raw 2`
  (le Grand Piano à part). `Mes presets` n'est jamais touché, ni à l'installation ni à la
  désinstallation.

Préparer le dossier `stage/` à côté du script, puis `makensis raw2.nsi` :

```
stage/vst3/RAW2.vst3/...            build/VST3/RAW2.vst3
stage/app/RAW2Preview.exe           build/bin/RAW2Preview.exe
stage/LISEZ-MOI.txt                 ce dossier
stage/docs/Instruments/...          banque d'instruments (sans le Grand Piano)
stage/docs/RAW 2 · INSTRUMENTS/...  raw2_inst_pack <sortie>
stage/docs_piano/Instruments/Grand Piano/...
```

Sous Linux, lancer `makensis` (et Wine pour un essai) avec `LC_ALL=C.UTF-8` : sinon les noms
contenant « · » sont mal traités.
