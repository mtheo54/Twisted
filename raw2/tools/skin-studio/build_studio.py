#!/usr/bin/env python3
"""Assemble ABRASION SKIN STUDIO en un seul fichier HTML, utilisable hors ligne.

Le modèle studio.html reçoit : les thèmes (resources/data/themes.json), la table des blocs (lue dans
src/ui/Blocks.cpp, la même que le plugin), les vignettes de l'interface (sprites/, voir
make_sprites.py) et les polices de RAW 2 (réduites aux caractères utiles).

    python3 tools/skin-studio/build_studio.py dist/ABRASION-Skin-Studio.html
"""
import base64, io, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def c_string(lits):
    """Concatène des littéraux C ("\\xC3\\x89" "DITEUR") et décode l'UTF-8."""
    raw = b""
    for lit in lits:
        i = 0
        while i < len(lit):
            if lit[i] == "\\" and lit[i + 1] == "x":
                raw += bytes([int(lit[i + 2:i + 4], 16)])
                i += 4
            elif lit[i] == "\\":
                raw += lit[i + 1].encode()
                i += 2
            else:
                raw += lit[i].encode()
                i += 1
    return raw.decode("utf-8")


def blocks():
    src = open(os.path.join(ROOT, "src/ui/Blocks.cpp"), encoding="utf-8").read()
    out = []
    modes = {"kRsScale": 0, "kRsWidth": 1, "kRsFree": 2, "kRsStack": 3}
    for m in re.finditer(r'\{"([a-z0-9.]+[A-Za-z0-9]*)", (\d), ((?:"(?:[^"\\]|\\.)*"\s*)+), \{([^}]*)\}(?:, (kRs\w+), ([^}]*))?\}', src):
        name = c_string(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(3)))
        r = [float(v.strip().rstrip("f")) for v in m.group(4).split(",")]
        b = {"key": m.group(1), "tab": int(m.group(2)), "name": name, "r": r, "rs": modes[m.group(5)] if m.group(5) else 0}
        if m.group(6):  # bornes de taille d'un widget : minW, minH, maxW, maxH
            b["min"] = [float(v.strip().rstrip("f")) for v in m.group(6).split(",")]
        out.append(b)
    assert len(out) >= 37, len(out)
    return out


def sprites():
    d = os.path.join(HERE, "sprites")
    res = {}
    for f in sorted(os.listdir(d)):
        if f.endswith(".webp"):
            res[f[:-5]] = "data:image/webp;base64," + base64.b64encode(open(os.path.join(d, f), "rb").read()).decode()
    return res


def fonts():
    from fontTools import subset
    from fontTools.ttLib import TTFont
    text = "".join(chr(c) for c in range(0x20, 0x7F)) + "ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸàâäçéèêëîïôöùûüÿœŒ«»·…—–’‘“”•→←↑↓↔⇄▾▸◂▴●○✕✓×÷±°%€★☆▶■□▲▼"
    res = {}
    for key, fn in (("mono", "JetBrainsMono-Regular.ttf"), ("monob", "JetBrainsMono-Bold.ttf"), ("pixel", "Silkscreen-Regular.ttf")):
        f = TTFont(os.path.join(ROOT, "resources/fonts", fn))
        opt = subset.Options()
        opt.flavor = "woff2"
        opt.layout_features = ["*"]
        s = subset.Subsetter(opt)
        s.populate(text=text)
        s.subset(f)
        buf = io.BytesIO()
        f.flavor = "woff2"
        f.save(buf)
        res[key] = "data:font/woff2;base64," + base64.b64encode(buf.getvalue()).decode()
    return res


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = args[0] if args else os.path.join(ROOT, "dist", "ABRASION-Skin-Studio.html")
    themes = json.load(open(os.path.join(ROOT, "resources/data/themes.json"), encoding="utf-8"))
    version = re.search(r"project\(RAW2 VERSION ([0-9.]+)", open(os.path.join(ROOT, "CMakeLists.txt")).read()).group(1)
    data = {"themes": themes, "blocks": blocks(), "sprites": sprites(), "version": version}
    f = fonts()
    html = open(os.path.join(HERE, "studio.html"), encoding="utf-8").read()
    html = html.replace("/*__DATA__*/null", json.dumps(data, ensure_ascii=False, separators=(",", ":")))
    for k, v in f.items():
        html = html.replace("__FONT_%s__" % k.upper(), v)
    if "--page" in sys.argv:  # page publiée : le squelette (doctype, head, body) est ajouté à la publication
        for tag in ('<!doctype html>\n', '<html lang="fr">\n', '<head>\n', '<meta charset="utf-8">\n',
                    '<meta name="viewport" content="width=device-width, initial-scale=1">\n', '</head>\n', '<body>\n', '</body>\n', '</html>\n'):
            assert tag in html, tag
            html = html.replace(tag, "", 1)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "w", encoding="utf-8").write(html)
    print(out, "%.1f Mo" % (len(html.encode()) / 1e6))


main()
