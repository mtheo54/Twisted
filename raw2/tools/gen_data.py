#!/usr/bin/env python3
"""Génère src/core/FactoryData.cpp à partir de resources/data/presets.json et themes.json.

Usage : python tools/gen_data.py   (depuis le dossier raw2)
Le fichier généré est versionné : il n'est utile de relancer ce script que si les JSON changent.
"""
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
presets = json.load(open(os.path.join(ROOT, 'resources/data/presets.json'), encoding='utf-8'))
themes = json.load(open(os.path.join(ROOT, 'resources/data/themes.json'), encoding='utf-8'))

# Jetons du thème : (nom C++, clé JSON, valeur de repli si absente)
# inputBg / div viennent de la maquette Presets.dc.html (écran de la banque de presets).
EXTRA = {
    'alu': {'inputBg': '#FFFFFF', 'div': '#CCCCCC'},
    'nothing': {'inputBg': '#0C0C0E', 'div': '#2A2A2E'},
    'titane': {'inputBg': '#C9CCCE', 'div': '#6E7173'},
    'crt': {'inputBg': '#040604', 'div': '#1E3A1E'},
    'ultraviolet': {'inputBg': '#FFFFFF', 'div': '#DFD9EB'},
    'onirique': {'inputBg': '#FFFFFF', 'div': '#E2CFD2'},
}
COLORS = ['chassis', 'chDot', 'card', 'line', 'rule', 'ink', 'muted', 'scr', 'scrLine', 'scrGrid', 'scrMid',
          'scrText', 'scrMuted', 'scrRule', 'acc', 'onAcc', 'hl', 'ledTab', 'ptr', 'tabBg', 'tabFg', 'logoBg',
          'btnBg', 'btnEdge', 'waveA', 'waveB', 'barHi', 'barLo', 'barPk', 'tickOff', 'knobFace', 'knobLine',
          'lifeBg', 'lifeLine', 'dead', 'railBg', 'keyNat', 'keyNatLine', 'keySharp', 'crs', 'sw', 'inputBg', 'div']
OPTIONAL = ['deadFill', 'cellEdge', 'tagAcc']


def col(s):
    s = s.strip()
    if s.startswith('#'):
        r, g, b = int(s[1:3], 16), int(s[3:5], 16), int(s[5:7], 16)
        return 'C(%d,%d,%d)' % (r, g, b)
    if s.startswith('rgba'):
        v = [x.strip() for x in s[s.index('(') + 1:s.index(')')].split(',')]
        return 'C(%s,%s,%s,%s)' % (v[0], v[1], v[2], fl(float(v[3])))
    raise ValueError(s)


def fl(x):
    s = '%g' % x
    if '.' not in s and 'e' not in s:
        s += '.0'
    return s + 'f'


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


out = []
out.append('// Fichier généré par tools/gen_data.py à partir de resources/data/*.json : ne pas éditer à la main.')
out.append('#include "core/FactoryData.h"\n')
out.append('namespace raw2 {\n')
out.append('namespace {')
out.append('constexpr Color C(int r, int g, int b, float a = 1.f) { return Color{r / 255.f, g / 255.f, b / 255.f, a}; }')
out.append('}\n')
out.append('const Theme kThemes[kNumThemes] = {')
for key, t in themes.items():
    t = dict(t)
    t.update(EXTRA[key])
    out.append('  { // ' + key)
    out.append('    %s, %s, %s,' % (cstr(key), cstr(t['name']), cstr(t['label'])))
    out.append('    ' + ', '.join(col(t[c]) for c in COLORS) + ',')
    out.append('    ' + ', '.join(col(t[c]) if c in t else 'Color{}' for c in OPTIONAL) + ',')
    out.append('    %s, %s, %s,' % ('true' if 'deadFill' in t else 'false', 'true' if 'cellEdge' in t else 'false',
                                    'true' if 'tagAcc' in t else 'false'))
    rgb = lambda a: '{%d,%d,%d}' % tuple(a)
    out.append('    %s, %s, %s, %s' % (rgb(t['accRGB']), rgb(t['old']), rgb(t['mid']) if 'mid' in t else '{0,0,0}',
                                      'true' if 'mid' in t else 'false'))
    out.append('  },')
out.append('};\n')

SH = {'sine': 'Shape::Sine', 'saw': 'Shape::Saw', 'square': 'Shape::Square', 'tri': 'Shape::Tri',
      'complex': 'Shape::Complex', 'noise': 'Shape::Noise', 'glass': 'Shape::Glass', 'metal': 'Shape::Metal'}
CAT = {'lead': 'Category::Lead', 'basse': 'Category::Bass', 'pad': 'Category::Pad', 'fx': 'Category::Fx',
       'pluck': 'Category::Pluck', 'arp': 'Category::Arp'}
out.append('const FactoryPreset kFactoryPresets[kNumFactoryPresets] = {')
for p in presets:
    a, b = p['oscA'], p['oscB']
    out.append('  {%s, %s, %s, {%s, %s, %s}, {%s, %s, %s}, %s, %s, %s, %s, %d, %d, %d, %d, %s, %s, %s, %d, %d, %d, %s, %s},' % (
        cstr(p['name']), CAT[p['category']], cstr(p['style']),
        SH[a['shape']], fl(a['morphStart']), fl(a['morphEnd']), SH[b['shape']], fl(b['morphStart']), fl(b['morphEnd']),
        fl(p['aPos']), fl(p['bPos']), fl(p['aLevel']), fl(p['bLevel']), p['aOct'], p['aSemi'], p['bOct'], p['bSemi'],
        fl(p['lifeSpeed']), fl(p['lifeBirth']), fl(p['lifeQty']), p['depthHarm'], p['depthSpace'], p['depthTexture'],
        'true' if p['lifeRule'] == 'B36/S23' else 'false', fl(p['master'])))
out.append('};\n')
out.append('} // namespace raw2')
assert len(presets) == 72 and len(themes) == 6
open(os.path.join(ROOT, 'src/core/FactoryData.cpp'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print('ok: %d presets, %d thèmes' % (len(presets), len(themes)))
