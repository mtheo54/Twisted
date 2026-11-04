# IA de RAW 2 : rapport d'entraînement

Produit par `raw2_train` le 2026-11-02.

Validation : les réseaux sont d'abord entraînés **sans** 15 presets d'usine mis de côté, puis mesurés sur ces presets inconnus. Les poids livrés sont ensuite ré-entraînés sur les 72 presets avec la même recette.

## RoleNet (carte PROPOSITION)

- 3456 écoutes (72 presets x 48 variantes), réseau 120 -> 64 -> 32 -> 5.
- Rôle reconnu sur des presets inconnus : **72.5 %** (hasard : 20 %).

Matrice de confusion (lignes : rôle réel, colonnes : rôle prédit) :

| | BASSE | LEAD | PAD | ARP / SEQ | TEXTURE | 
|---|---|---|---|---|---|
| BASSE | 144 | 0 | 0 | 0 | 0 | 
| LEAD | 4 | 137 | 2 | 49 | 0 | 
| PAD | 1 | 2 | 86 | 1 | 54 | 
| ARP / SEQ | 0 | 51 | 0 | 89 | 4 | 
| TEXTURE | 6 | 0 | 24 | 0 | 66 | 

## MixNet (page MIX IA)

- 20000 boucles de 3 s avec défauts ajoutés au hasard, réseau 40 -> 64 -> 64 -> 10.
- Décisions correctes sur des presets inconnus : **82.3 % (anciennes règles : 65.1 %)**.
- Erreur moyenne sur le dosage des EQ : **1.22 dB**.

| Correction | Réseau : bonnes décisions | précision | rappel | Règles |
|---|---|---|---|---|
| EQ 250 Hz | 70.4 % | 52 % | 42 % | 55.4 % |
| COMP | 85.1 % | 73 % | 51 % | 71.4 % |
| SHELF 8 kHz | 79.7 % | 51 % | 21 % | 46.0 % |
| EQ 80 Hz | 74.5 % | 57 % | 16 % | 41.7 % |
| EQ 3.5 kHz | 84.3 % | 75 % | 54 % | 75.7 % |
| LIMITEUR | 100.0 % | 100 % | 100 % | 100.0 % |

Les « règles » sont celles de la version précédente, recalculées sur les mêmes bandes (approximation).
