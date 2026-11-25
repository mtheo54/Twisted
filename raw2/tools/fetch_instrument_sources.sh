#!/usr/bin/env bash
# Télécharge les bibliothèques libres (CC0, CC BY 3.0, domaine public) dont la banque d'instruments
# de RAW 2 est tirée, en ne gardant que les dossiers utiles (clones partiels).
#   raw2/tools/fetch_instrument_sources.sh <dossier des sources>
# Ensuite : python3 raw2/tools/build_instruments.py <dossier des sources> <dossier de sortie>
set -euo pipefail
OUT="${1:?dossier des sources}"
mkdir -p "$OUT"
cd "$OUT"
clone() {  # clone <dépôt> [dossiers…] : complet si aucun dossier n'est donné, partiel sinon
    local repo=$1; shift
    local name; name=$(basename "$repo")
    [ -d "$name/.git" ] && return 0
    if [ $# -eq 0 ]; then
        git clone -q --depth 1 "https://github.com/$repo.git"
    else
        git clone -q --depth 1 --filter=blob:none --sparse "https://github.com/$repo.git"
        (cd "$name" && git sparse-checkout set "$@")
    fi
}
clone sfzinstruments/GregSullivan.E-Pianos
clone sfzinstruments/SplendidGrandPiano
clone sfzinstruments/karoryfer.string-cyborgs
clone sfzinstruments/karoryfer.weresax
clone sparklecatta-lang/XSXB-Band-Samples
clone sgossner/VCSL Electrophones "Idiophones/Struck Idiophones/Vibraphone"
clone User1362/cc0-obxf-multisamples Strings/String_Machine Pads/Warmth Pads/Vintage_Swell Brass/J60_Brass \
    Brass/Brass_Stabs Leads/Funk_Lead Leads/Super_Saw Basses/OBX_Bass_1 Basses/Slap_Bass Organs/Rotary_Organ \
    Organs/Tonewheels Voices/Choir_Aah_1
du -sh ./* 2>/dev/null || true
