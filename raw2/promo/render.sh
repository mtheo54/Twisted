#!/usr/bin/env bash
# Refait les vidéos promo de RAW 2 : éléments (captures, musiques) puis les 4 vidéos.
# Usage : promo/render.sh <dossier de la banque d'instruments> [musique]
# Sortie : build/promo-videos/RAW2-pub-<lancement|le-son>-<9x16|1x1>.mp4
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
"$HERE/make-assets.sh" "${1:?dossier de la banque d\'instruments}" "${2:-}"
OUT="$ROOT/build/promo-videos"
mkdir -p "$OUT"
cd "$ROOT"
node promo/render.cjs lancement 1080 1920 "$OUT/RAW2-pub-lancement-9x16.mp4" &
node promo/render.cjs le-son 1080 1920 "$OUT/RAW2-pub-le-son-9x16.mp4" &
wait
node promo/render.cjs lancement 1080 1080 "$OUT/RAW2-pub-lancement-1x1.mp4" &
node promo/render.cjs le-son 1080 1080 "$OUT/RAW2-pub-le-son-1x1.mp4" &
wait
echo "vidéos dans $OUT"
