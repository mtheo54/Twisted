#!/usr/bin/env bash
# Prépare les éléments des vidéos promo dans build/promo-assets :
#   - captures de l'interface en 2560 x 1920 (raw2_snap, RAW2_SNAP_SCALE=2)
#   - musiques jouées par le moteur RAW 2 (raw2_promo_audio + partitions de ce dossier)
#   - forme d'onde d'un vrai échantillon de Wurlitzer (plan « instruments »)
# Usage : promo/make-assets.sh <dossier de la banque d'instruments>
#   (le dossier qui contient « Wurlitzer EP200 », « Alto Sax »…, voir docs/INSTRUMENTS.md)
# Nécessite un build Linux avec -DRAW2_BUILD_TRAINER=ON (raw2_snap, raw2_inst_pack, raw2_promo_audio).
set -euo pipefail
export LC_ALL=C.UTF-8
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BANK="$(cd "${1:?dossier de la banque d\'instruments}" && pwd)"
BUILD="$ROOT/build"
OUT="$BUILD/promo-assets"
mkdir -p "$OUT"

# un HOME jetable où la banque est « installée » (Documents/Raw 2/Instruments)
FAKEHOME="$BUILD/promo-home"
rm -rf "$FAKEHOME" && mkdir -p "$FAKEHOME/Documents/Raw 2"
ln -s "$BANK" "$FAKEHOME/Documents/Raw 2/Instruments"
export HOME="$FAKEHOME"

# pack de presets d'instruments (les partitions y prennent leurs sons)
rm -rf "$BUILD/promo-pack" && mkdir -p "$BUILD/promo-pack"
"$BUILD/raw2_inst_pack" "$BUILD/promo-pack" > /dev/null
PACK="$BUILD/promo-pack/RAW 2 · INSTRUMENTS"

snap() {  # snap <sortie> <onglet> <thème> [env…]
    local out=$1 tab=$2 th=$3; shift 3
    env RAW2_SNAP_SCALE=2 RAW2_SNAP_PRESET=5 "$@" "$BUILD/raw2_snap" "$OUT/$out" "$tab" "$th" > /dev/null
}
# onglets : 0 SON, 1 VIE, 2 MODULES, 6 WAVETABLE, 7 JEU, 8 SYNTH ; thèmes 0 à 5
snap son.png 0 0 &
snap vie.png 1 0 &
snap wavetable.png 6 0 &
snap synth.png 8 0 &
for th in 0 1 2 3 4 5; do snap "theme$th.png" 0 "$th" & done
wait
snap inst.png 0 0 RAW2_SNAP_PRESETFILE="$PACK/25 Fusion Wurli Sub.raw" RAW2_SNAP_FRAMES=200

# musiques (les partitions désignent le pack par ../build/promo-pack)
(cd "$HERE" && "$BUILD/raw2_promo_audio" lancement.txt "$OUT/lancement.wav")
(cd "$HERE" && "$BUILD/raw2_promo_audio" le-son.txt "$OUT/le-son.wav")
# analyse image par image (niveau, graves, aigus, attaques, spectre) : la scène réagit au son
for a in lancement le-son; do python3 "$HERE/audio_features.py" "$OUT/$a.wav" "$OUT/$a-audio.json"; done

# forme d'onde réelle : Wurlitzer, une note de la 4e octave jouée forte
python3 - "$BANK/Wurlitzer EP200/samples" "$OUT/wurli.json" <<'EOF'
import json, os, sys, wave
import numpy as np
d, out = sys.argv[1], sys.argv[2]
names = sorted(os.listdir(d))
import re
pick = next((n for n in names if re.fullmatch(r'[a-g]b?4f\.wav', n.lower())), None) or names[len(names) // 2]
w = wave.open(os.path.join(d, pick))
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
if w.getnchannels() == 2:
    x = x.reshape(-1, 2).mean(axis=1)
sr = w.getframerate()
x = x[: int(sr * 2.4)]
env = [float(np.abs(c).max()) for c in np.array_split(x, 160)]
m = max(env) or 1.0
cyc = x[int(sr * 0.05): int(sr * 0.05) + 1024]
cm = float(np.abs(cyc).max()) or 1.0
json.dump({'name': 'Wurlitzer EP200', 'sample': pick, 'env': [round(v / m, 4) for v in env],
           'wave': [round(float(v) / cm, 4) for v in cyc[::2]]}, open(out, 'w'))
print('forme d\'onde :', pick)
EOF
echo "éléments prêts dans $OUT"
