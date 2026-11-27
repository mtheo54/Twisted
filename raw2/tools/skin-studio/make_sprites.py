#!/usr/bin/env python3
"""Vignettes de l'interface pour ABRASION SKIN STUDIO.

Chaque page de RAW 2 est rendue par raw2_snap (Linux, Cairo) sur une plaque noire puis blanche
(cartes transparentes), ce qui donne l'image exacte avec sa transparence ; un troisième rendu avec un
autre accent donne le masque des pixels d'accent (recoloriés par le studio). Deux bases : claire (ALU)
et sombre (NOTHING).

    python3 tools/skin-studio/make_sprites.py build/raw2_snap tools/skin-studio/sprites
"""
import json, os, subprocess, sys, tempfile
from PIL import Image, ImageChops

SNAP, OUT = sys.argv[1], sys.argv[2]
SCALE = 0.75
ACC = ["acc", "hl", "ledTab", "ptr", "waveB", "barPk", "tagAcc"]

def skin(dir_, base, plate, acc=None):
    os.makedirs(dir_, exist_ok=True)
    colors = {"chassis": plate, "card": "#00000000"}
    if acc:
        for k in ACC: colors[k] = acc
    json.dump({"format": "raw2skin", "version": 1, "name": os.path.basename(dir_), "base": base, "colors": colors,
               "background": {"file": "", "voile": 0, "grille": False}, "glass": {"on": False}, "cards": {"opacite": 1}},
              open(os.path.join(dir_, "skin.json"), "w"))

def main():
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp()
    env = dict(os.environ, HOME=tmp, RAW2_SNAP_PRESET="5")
    for base in ("alu", "nothing"):
        dirs = [os.path.join(tmp, base + s) for s in ("-noir", "-blanc", "-acc")]
        skin(dirs[0], base, "#000000")
        skin(dirs[1], base, "#FFFFFF")
        skin(dirs[2], base, "#000000", "#00FF00")
        for tab in range(9):
            out = os.path.join(tmp, f"{base}{tab}.png")
            subprocess.run([SNAP, out, str(tab), "0"], env=dict(env, RAW2_SNAP_SPRITES=";".join(dirs)), check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            b, w, g = (Image.open(f"{out}.{k}.png").convert("RGB") for k in range(3))
            # alpha = 1 - (blanc - noir) ; couleur = noir / alpha
            bw = ImageChops.subtract(w, b).convert("L")
            alpha = bw.point(lambda v: 255 - v)
            rgba = Image.new("RGBA", b.size)
            bp, ap = b.load(), alpha.load()
            rp = rgba.load()
            for y in range(b.size[1]):
                for x in range(b.size[0]):
                    a = ap[x, y]
                    if a == 0: continue
                    r, gg, bb = bp[x, y]
                    rp[x, y] = (min(255, r * 255 // a), min(255, gg * 255 // a), min(255, bb * 255 // a), a)
            mask = ImageChops.difference(g, b).convert("L").point(lambda v: min(255, v * 2))
            size = (int(b.size[0] * SCALE), int(b.size[1] * SCALE))
            rgba.resize(size, Image.LANCZOS).save(os.path.join(OUT, f"{base}-{tab}.webp"), "WEBP", quality=82, method=6)
            mask.resize(size, Image.LANCZOS).save(os.path.join(OUT, f"{base}-{tab}-acc.webp"), "WEBP", quality=70, method=6)
            print(base, tab, flush=True)

main()
