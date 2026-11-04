#!/usr/bin/env python3
# Construit la banque d'instruments réels de RAW 2 (couche « ROMpler ») à partir de bibliothèques
# libres déjà téléchargées (CC0, domaine public, CC-BY). Chaque instrument sort dans son dossier :
#   <Nom>/<Nom>.sfz   sous-ensemble SFZ lu par le plugin (zones, vélocités, boucles)
#   <Nom>/samples/    WAV PCM 16 bits, 44,1 ou 48 kHz, mono ou stéréo
#   <Nom>/LICENSE.txt licence, source, attribution
# Usage : python3 tools/build_instruments.py <dossier des sources> <dossier de sortie>
# Nécessite ffmpeg / ffprobe (décodage FLAC, rééchantillonnage) et numpy. Déterministe.
import json, math, os, re, shutil, struct, subprocess, sys, wave
import numpy as np

SRC = OUT = ''

# ---------------------------------------------------------------- lecture SFZ (sources)
NOTE = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}

def key_of(v):
    v = str(v).strip().lower()
    if re.fullmatch(r'-?\d+', v):
        return int(v)
    m = re.fullmatch(r'([a-g])([#b]?)(-?\d+)', v)
    if not m:
        raise ValueError('note ' + v)
    n = NOTE[m.group(1)] + (1 if m.group(2) == '#' else -1 if m.group(2) == 'b' else 0)
    return n + 12 * (int(m.group(3)) + 1)  # c4 = 60

def read_sfz(path, defines=None):
    """Texte SFZ aplati : #include résolus, #define substitués, commentaires retirés."""
    defines = {} if defines is None else defines
    base = os.path.dirname(path)
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    for line in text.splitlines():
        line = line.split('//')[0].strip()
        if not line:
            continue
        m = re.match(r'#define\s+(\$\w+)\s+(.*)', line)
        if m:
            defines[m.group(1)] = m.group(2).strip()
            continue
        for k in sorted(defines, key=len, reverse=True):
            line = line.replace(k, defines[k])
        m = re.match(r'#include\s+"([^"]+)"', line)
        if m:
            out.append(read_sfz(os.path.join(base, m.group(1).replace('\\', '/')), defines))
            continue
        out.append(line)
    return '\n'.join(out)

def parse_sfz(path):
    """Liste de régions (opcodes hérités global > master > group > region), chemins résolus."""
    text = read_sfz(path)
    tokens = re.split(r'(<\w+>)', text)
    levels = {'global': {}, 'master': {}, 'group': {}}
    default_path, header, cur, regions, gidx = '', None, {}, [], -1

    def ops(chunk):
        res = {}
        keys = [m for m in re.finditer(r'(?:(?<=\s)|^)([A-Za-z0-9_]+)=', chunk)]
        for i, m in enumerate(keys):
            end = keys[i + 1].start() if i + 1 < len(keys) else len(chunk)
            res[m.group(1)] = chunk[m.end():end].strip()
        return res

    def flush():
        if header == 'region':
            r = dict(levels['global']); r.update(levels['master']); r.update(levels['group']); r.update(cur)
            r['_group'] = gidx
            regions.append(r)

    for t in tokens:
        if re.fullmatch(r'<\w+>', t):
            flush()
            header, cur = t[1:-1], {}
            if header == 'global':
                levels = {'global': {}, 'master': {}, 'group': {}}
            elif header == 'master':
                levels['master'], levels['group'] = {}, {}
            elif header == 'group':
                levels['group'] = {}
                gidx += 1
            continue
        o = ops(t)
        if header == 'control':
            default_path = o.get('default_path', default_path).replace('\\', '/')
        elif header in levels:
            levels[header].update(o)
        elif header == 'region':
            cur.update(o)
    flush()
    base = os.path.dirname(path)
    for r in regions:
        r['_file'] = os.path.normpath(os.path.join(base, default_path + r['sample'].replace('\\', '/')))
    return regions

# ---------------------------------------------------------------- audio (ffmpeg)
def probe(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries', 'stream=sample_rate,channels',
                          '-of', 'json', path], capture_output=True, text=True, check=True).stdout
    s = json.loads(out)['streams'][0]
    return int(s['sample_rate']), int(s['channels'])

def decode(path, sr_out):
    sr, ch = probe(path)
    ch = min(ch, 2)
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-ac', str(ch), '-ar', str(sr_out), '-f', 'f32le', '-acodec', 'pcm_f32le', '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype='<f4').reshape(-1, ch).copy(), sr

def wav_loop(path):
    """Boucle du bloc 'smpl' d'un WAV source (début, fin) ou None."""
    if not path.lower().endswith('.wav'):
        return None
    with open(path, 'rb') as f:
        d = f.read()
    p = 12
    while p + 8 <= len(d):
        cid, sz = d[p:p + 4], struct.unpack('<I', d[p + 4:p + 8])[0]
        if cid == b'smpl' and sz >= 36 + 24:
            n = struct.unpack('<I', d[p + 8 + 28:p + 8 + 32])[0]
            if n:
                st, en = struct.unpack('<II', d[p + 8 + 36 + 8:p + 8 + 36 + 16])
                return st, en
        p += 8 + sz + (sz & 1)
    return None

def write_wav16(path, x, sr):
    y = np.clip(np.round(x * 32767.0), -32767, 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(y.tobytes())

def f0(x, sr, lo=25.0, hi=2500.0):
    """Fondamentale estimée (autocorrélation) sur 0,25 s après l'attaque."""
    m = x.mean(axis=1)
    s0 = int(0.08 * sr)
    seg = m[s0:s0 + int(0.25 * sr)]
    if len(seg) < 2048:
        seg = m[:int(0.25 * sr)]
    seg = seg - seg.mean()
    n = len(seg)
    ac = np.fft.irfft(np.abs(np.fft.rfft(seg, 2 * n)) ** 2)[:n]
    a, b = int(sr / hi), min(int(sr / lo), n - 1)
    if b <= a + 2:
        return 0.0
    k = a + int(np.argmax(ac[a:b]))
    # premier pic assez haut (évite l'octave du dessous)
    peak = ac[k]
    for j in range(a + 1, k):
        if ac[j] > 0.85 * peak and ac[j] >= ac[j - 1] and ac[j] >= ac[j + 1]:
            k = j
            break
    if 0 < k < n - 1:  # interpolation parabolique
        y0, y1, y2 = ac[k - 1], ac[k], ac[k + 1]
        den = y0 - 2 * y1 + y2
        k = k + 0.5 * (y0 - y2) / den if den else k
    return sr / k

# ---------------------------------------------------------------- recettes
def tile(regs, max_layers, extend=12):
    """Garde au plus max_layers couches de vélocité, les recolle sur 1..127 et comble les trous de touches."""
    layers = sorted({(r['lovel'], r['hivel']) for r in regs})
    if len(layers) > max_layers:
        idx = [round(i * (len(layers) - 1) / (max_layers - 1)) for i in range(max_layers)] if max_layers > 1 else [len(layers) - 1]
        keep = [layers[i] for i in idx]
        regs = [r for r in regs if (r['lovel'], r['hivel']) in keep]
        layers = keep
    n = len(layers)
    bounds = [1] + [round(1 + 127 * (i + 1) / n) for i in range(n - 1)] + [128]
    remap = {layers[i]: (bounds[i], bounds[i + 1] - 1) for i in range(n)}
    for r in regs:
        r['lovel'], r['hivel'] = remap[(r['lovel'], r['hivel'])]
    for lay in set(remap.values()):
        rs = sorted([r for r in regs if (r['lovel'], r['hivel']) == lay], key=lambda r: (r['lokey'], r['root']))
        for a, b in zip(rs, rs[1:]):  # trous entre zones : la zone basse monte
            if b['lokey'] > a['hikey'] + 1:
                a['hikey'] = b['lokey'] - 1
            elif b['lokey'] <= a['hikey']:
                b['lokey'] = a['hikey'] + 1
        rs[0]['lokey'] = max(0, min(rs[0]['lokey'], rs[0]['root'] - extend))
        rs[-1]['hikey'] = min(127, max(rs[-1]['hikey'], rs[-1]['root'] + extend))
    return [r for r in regs if r['lokey'] <= r['hikey']]

def from_sfz(rel, group_filter=None, region_filter=None, root_shift=0):
    regs = []
    for r in parse_sfz(os.path.join(SRC, rel)):
        if r.get('trigger', 'attack') != 'attack':
            continue
        if any(k.startswith(('on_locc', 'on_hicc', 'start_locc', 'start_hicc')) for k in r):
            continue  # zones déclenchées par un contrôleur (résonance de cordes, pédale) : ignorées
        if r.get('seq_position', '1') != '1':
            continue
        if group_filter is not None and r['_group'] not in group_filter:
            continue
        if region_filter and not region_filter(r):
            continue
        key = r.get('key')
        root = key_of(r.get('pitch_keycenter', key if key else 60))
        regs.append({
            'file': r['_file'],
            'lokey': key_of(r.get('lokey', key if key else 0)), 'hikey': key_of(r.get('hikey', key if key else 127)),
            'root': root + root_shift, 'lovel': int(r.get('lovel', 1)), 'hivel': int(r.get('hivel', 127)),
            'tune': int(float(r.get('tune', 0))), 'volume': float(r.get('volume', 0)),
            'loop': r.get('loop_mode', r.get('loopmode', 'no_loop')) in ('loop_continuous', 'loop_sustain'),
            'loop_start': int(r['loop_start']) if 'loop_start' in r else None, 'loop_end': int(r['loop_end']) if 'loop_end' in r else None,
            'offset': int(r.get('offset', 0)),
        })
    return regs

def from_names(rel, pattern, layer_of=lambda m: 1, note_shift=0):
    """Zones depuis les noms de fichiers (note dans le nom, ex. FMPiano_C3_vl2.wav)."""
    d = os.path.join(SRC, rel)
    regs = []
    for f in sorted(os.listdir(d)):
        m = re.fullmatch(pattern, f)
        if not m:
            continue
        root = key_of(m.group('note')) + note_shift
        lay = layer_of(m)
        regs.append({'file': os.path.join(d, f), 'lokey': root, 'hikey': root, 'root': root, 'lovel': lay, 'hivel': lay,
                     'tune': 0, 'volume': 0.0, 'loop': False, 'loop_start': None, 'loop_end': None, 'offset': 0})
    # zones de touches : chaque note couvre jusqu'à la suivante (même couche)
    for lay in {r['lovel'] for r in regs}:
        rs = sorted([r for r in regs if r['lovel'] == lay], key=lambda r: r['root'])
        for a, b in zip(rs, rs[1:]):
            mid = (a['root'] + b['root']) // 2
            a['hikey'], b['lokey'] = mid, mid + 1
    return regs

CC_BY_EP = ('Creative Commons Attribution 3.0 Unported (CC BY 3.0) - https://creativecommons.org/licenses/by/3.0/',
            'https://github.com/sfzinstruments/GregSullivan.E-Pianos')
CC0 = 'Creative Commons Zero 1.0 Universal (CC0 1.0) - https://creativecommons.org/publicdomain/zero/1.0/'
OBXF = 'https://github.com/User1362/cc0-obxf-multisamples (rendus de patchs OB-Xf sous CC0)'

def obxf(folder, name, cat, layers=1):
    return dict(name=name, cat=cat, src=lambda: from_sfz(f'cc0-obxf-multisamples/{folder}/{os.path.basename(folder)}.sfz'),
                lic=CC0, url=OBXF, author='rendus OB-Xf (patchs CC0)', layers=layers, maxlen=6.0)

RECIPES = [
    dict(name='Wurlitzer EP200', cat='KEYS', src=lambda: from_sfz('GregSullivan.E-Pianos/Wurlitzer EP200/Wurlitzer EP200.sfz'),
         lic=CC_BY_EP[0], url=CC_BY_EP[1], author='Wurlitzer EP200 samples by Greg Sullivan (sullivang.net), SFZ by kinwie', layers=3, maxlen=8.0, by=True),
    dict(name='CP80 Electric Grand', cat='KEYS', src=lambda: from_sfz('GregSullivan.E-Pianos/CP80/CP80.sfz'),
         lic=CC_BY_EP[0], url=CC_BY_EP[1], author='Yamaha CP80 samples by Greg Sullivan (sullivang.net), SFZ by kinwie', layers=2, maxlen=8.0, by=True),
    dict(name='Pianet T', cat='KEYS', src=lambda: from_sfz('GregSullivan.E-Pianos/Pianet T/Pianet T.sfz'),
         lic=CC_BY_EP[0], url=CC_BY_EP[1], author='Hohner Pianet T samples by Greg Sullivan (sullivang.net), SFZ by kinwie', layers=2, maxlen=6.0, by=True),
    dict(name='FM Piano', cat='KEYS', src=lambda: from_names('VCSL/Electrophones/' + _vcsl_elec() + '/FM Piano', r'FMPiano_(?P<note>[A-G]#?-?\d)_vl(?P<v>\d)\.wav',
                                                             lambda m: int(m.group('v'))),
         lic=CC0, url='https://github.com/sgossner/VCSL', author='Versilian Community Sample Library (VCSL), Yamaha TX81Z', layers=2, maxlen=6.0, check_octave=True),
    dict(name='Clavisynth', cat='KEYS', src=lambda: from_names('VCSL/Electrophones/' + _vcsl_elec() + '/Clavisynth', r'Clavisynth_(?P<note>[A-G]#?-?\d)_vl(?P<v>\d)\.wav',
                                                               lambda m: int(m.group('v')), note_shift=24),  # le patch sonne 2 octaves au-dessus des noms
         lic=CC0, url='https://github.com/sgossner/VCSL', author='Versilian Community Sample Library (VCSL), Yamaha TX81Z', layers=2, maxlen=4.0, check_octave=True),
    dict(name='Grand Piano', cat='PIANO', src=lambda: from_sfz('SplendidGrandPiano/Splendid Grand Piano.sfz'),
         lic='Domaine public (publié par Akai en 2000, d’après le dépôt)', url='https://github.com/sfzinstruments/SplendidGrandPiano',
         author='Splendid Grand Piano (Akai), SFZ sfzinstruments', layers=2, maxlen=6.0),
    dict(name='String Machine', cat='PAD', src=lambda: from_sfz('karoryfer.string-cyborgs/Programs/zinc_one_voice.sfz', root_shift=-12),  # sonne une octave sous sa référence
         lic=CC0, url='https://github.com/sfzinstruments/karoryfer.string-cyborgs', author='Karoryfer Samples, String Cyborgs', layers=1, maxlen=8.0),
    obxf('Strings/String_Machine', 'OB String Machine', 'STRINGS'),
    obxf('Pads/Warmth', 'OB Pad Warmth', 'PAD'),
    obxf('Pads/Vintage_Swell', 'OB Pad Vintage Swell', 'PAD'),
    obxf('Brass/J60_Brass', 'OB Brass J60', 'BRASS'),
    obxf('Brass/Brass_Stabs', 'OB Brass Stabs', 'BRASS'),
    obxf('Leads/Funk_Lead', 'OB Funk Lead', 'LEAD'),
    obxf('Leads/Super_Saw', 'OB Super Saw', 'LEAD'),
    obxf('Basses/OBX_Bass_1', 'OB Bass', 'BASS'),
    obxf('Basses/Slap_Bass', 'OB Slap Bass', 'BASS'),
    obxf('Organs/Rotary_Organ', 'OB Rotary Organ', 'ORGAN'),
    obxf('Organs/Tonewheels', 'OB Tonewheels', 'ORGAN'),
    obxf('Voices/Choir_Aah_1', 'OB Choir Aah', 'VOX'),
    dict(name='G-Funk Bass', cat='BASS', src=lambda: from_sfz('XSXB-Band-Samples/gfunk-bass.sfz', group_filter={0}),
         lic=CC0, url='https://github.com/sparklecatta-lang/XSXB-Band-Samples', author='sparklecatta-lang (XSXB Band Samples)', layers=1, maxlen=6.0),
    dict(name='G-Funk Whistle', cat='LEAD', src=lambda: from_sfz('XSXB-Band-Samples/gfunk-lead.sfz', group_filter={0}),
         lic=CC0, url='https://github.com/sparklecatta-lang/XSXB-Band-Samples', author='sparklecatta-lang (XSXB Band Samples)', layers=1, maxlen=6.0),
    dict(name='Alto Sax', cat='WINDS', src=lambda: from_sfz('karoryfer.weresax/Programs/alto_map_forte_dynamic.sfz'),
         lic=CC0, url='https://github.com/sfzinstruments/karoryfer.weresax', author='Karoryfer Samples, Weresax', layers=1, maxlen=6.0),
    dict(name='Vibraphone', cat='MALLETS', src=lambda: from_names('VCSL/Idiophones/Struck Idiophones/Vibraphone/Hard Mallets',
                                                                   r'Vibes_hard_(?P<note>[A-G]#?\d)_v(?P<v>\d)_rr1_Main\.wav', lambda m: int(m.group('v')),
                                                                   note_shift=12),  # noms une octave en dessous du son
         lic=CC0, url='https://github.com/sgossner/VCSL', author='Versilian Community Sample Library (VCSL)', layers=2, maxlen=6.0, check_octave=True),
]

def _vcsl_elec():
    d = os.path.join(SRC, 'VCSL/Electrophones')
    for e in sorted(os.listdir(d)):
        if os.path.isdir(os.path.join(d, e, 'FM Piano')):
            return e
    raise FileNotFoundError('VCSL FM Piano')

# ---------------------------------------------------------------- construction
def build(rec):
    regs = rec['src']()
    if not regs:
        raise RuntimeError('aucune zone')
    regs = tile(regs, rec['layers'])
    name = rec['name']
    folder = os.path.join(OUT, name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(os.path.join(folder, 'samples'))
    files = sorted({r['file'] for r in regs})
    srs = [probe(f)[0] for f in files]
    sr_out = srs[0] if srs[0] in (44100, 48000) else 44100
    audio, info = {}, {}
    for f in files:
        x, sr_in = decode(f, sr_out)
        ratio = sr_out / sr_in
        lp = wav_loop(f)
        # début : 2 ms avant le premier passage au-dessus de -50 dB du pic du fichier
        pk = float(np.max(np.abs(x))) if len(x) else 0.0
        on = np.nonzero(np.max(np.abs(x), axis=1) > pk * 10 ** (-50 / 20))[0]
        start = max(0, int(on[0]) - int(0.002 * sr_out)) if len(on) else 0
        audio[f] = (x, start, ratio, lp)
    # normalisation de l'instrument entier : le plus fort à -1 dBFS
    peak = max(float(np.max(np.abs(a[0]))) for a in audio.values()) or 1.0
    gain = 10 ** (-1 / 20) / peak
    out_regions, written = [], {}
    for r in sorted(regs, key=lambda r: (r['lovel'], r['lokey'])):
        x, start, ratio, lp = audio[r['file']]
        ls, le = r['loop_start'], r['loop_end']
        if r['loop'] and (ls is None or le is None) and lp:
            ls, le = lp
        loop = r['loop'] and ls is not None and le is not None and le > ls + 32
        off = int(round(r['offset'] * ratio))
        if loop:
            ls2, le2 = int(round(ls * ratio)) - start, int(round(le * ratio)) - start
            st = start if ls2 >= 0 else start + ls2  # la boucle reste dans le fichier
            ls2, le2 = int(round(ls * ratio)) - st, int(round(le * ratio)) - st
            end = min(len(x), st + le2 + int(0.05 * sr_out))
            y = x[st:end] * gain
        else:
            st = start
            end = min(len(x), st + int(rec['maxlen'] * sr_out))
            y = x[st:end] * gain
            fade = min(len(y), int(0.05 * sr_out))
            if fade and end < len(x):
                y[-fade:] *= np.linspace(1, 0, fade)[:, None]
        key = (r['file'], loop)
        if key not in written:
            base = re.sub(r'[^A-Za-z0-9_.-]', '_', os.path.splitext(os.path.basename(r['file']))[0])
            fn = f'{base}.wav'
            k = 2
            while fn in written.values():
                fn, k = f'{base}_{k}.wav', k + 1
            write_wav16(os.path.join(folder, 'samples', fn), y, sr_out)
            written[key] = fn
        reg = dict(sample=written[key], lokey=r['lokey'], hikey=r['hikey'], pitch_keycenter=r['root'], lovel=r['lovel'], hivel=r['hivel'])
        if r['tune']:
            reg['tune'] = r['tune']
        if abs(r['volume']) > 0.01:
            reg['volume'] = round(r['volume'], 2)
        if loop:
            reg.update(loop_mode='loop_continuous', loop_start=max(0, ls2), loop_end=min(len(y) - 1, le2))
        else:
            reg['loop_mode'] = 'no_loop'
        if off:
            reg['offset'] = max(0, off - st)
        out_regions.append(reg)
    with open(os.path.join(folder, name + '.sfz'), 'w', encoding='utf-8') as f:
        f.write(f'// name: {name}\n// category: {rec["cat"]}\n// license: {rec["lic"]}\n// author: {rec["author"]}\n')
        f.write('// construit par tools/build_instruments.py (RAW 2)\n<control> default_path=samples/\n')
        for reg in out_regions:
            f.write('<region> ' + ' '.join(f'{k}={v}' for k, v in reg.items()) + '\n')
    with open(os.path.join(folder, 'LICENSE.txt'), 'w', encoding='utf-8') as f:
        f.write(f'{name} - instrument de la banque RAW 2\n\nLicence : {rec["lic"]}\nSource : {rec["url"]}\nAuteur / attribution : {rec["author"]}\n')
        if rec.get('by'):
            f.write('Modified: trimmed, converted, re-mapped for RAW 2.\n')
        else:
            f.write('Modifié pour RAW 2 : coupé, converti, remappé.\n')
    return out_regions, folder, sr_out

# Compensation de niveau : les fichiers sont normalisés en crête (-1 dBFS), si bien qu'un piano
# ou un vibraphone (attaque forte, puis décroissance) sonne bien plus bas qu'un oscillateur. On
# mesure la zone du do central (vélocité 100) sur sa première seconde et on écrit un gain
# <global> global_volume=… (additif, SFZ v2) pour viser MAKEUP_RMS, crête de la zone sous MAKEUP_PEAK
# (au-delà de 0 dBFS : le gain est appliqué en flottant par le moteur, qui garde sa marge).
MAKEUP_RMS, MAKEUP_PEAK, MAKEUP_MAX = -8.0, 9.0, 20.0

def apply_makeup(folder, name):
    sfz = os.path.join(folder, name + '.sfz')
    with open(sfz, encoding='utf-8') as f:
        lines = [l for l in f.read().splitlines() if not l.startswith('<global>')]
    regs = [dict(kv.split('=', 1) for kv in l[len('<region> '):].split()) for l in lines if l.startswith('<region>')]
    def score(r):
        lk, hk, lv, hv = int(r['lokey']), int(r['hikey']), int(r['lovel']), int(r['hivel'])
        dk = 0 if lk <= 60 <= hk else min(abs(60 - lk), abs(60 - hk))
        dv = 0 if lv <= 100 <= hv else min(abs(100 - lv), abs(100 - hv))
        return (dk, dv)
    r = min(regs, key=score)
    with wave.open(os.path.join(folder, 'samples', r['sample']), 'rb') as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64).reshape(-1, ch) / 32768.0
    x = x.mean(axis=1) * 10 ** (float(r.get('volume', 0)) / 20)
    if r.get('loop_mode') == 'loop_continuous':
        ls, le = int(r['loop_start']), int(r['loop_end'])
        while len(x) < sr:
            x = np.concatenate([x[:le + 1], x[ls:le + 1]])
    x = x[:sr]
    rms = 10 * np.log10(np.mean(x ** 2) + 1e-12)
    pk = 20 * np.log10(np.max(np.abs(x)) + 1e-9)
    db = float(np.clip(min(MAKEUP_RMS - rms, MAKEUP_PEAK - pk), 0.0, MAKEUP_MAX))
    out, done = [], False
    for l in lines:
        if not done and l.startswith('<region>'):
            out.append(f'<global> global_volume={db:.1f}')
            done = True
        out.append(l)
    with open(sfz, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    return db

def verify(name, regions, folder):
    """Contrôle des fichiers, boucles, pavage des touches, et hauteur de 2 zones (cents)."""
    errs, cents = [], []
    for r in regions:
        p = os.path.join(folder, 'samples', r['sample'])
        with wave.open(p, 'rb') as w:
            n, sr, bits = w.getnframes(), w.getframerate(), w.getsampwidth() * 8
        if bits != 16 or sr not in (44100, 48000):
            errs.append(f'{r["sample"]} format')
        if r.get('loop_mode') == 'loop_continuous' and not (0 <= r['loop_start'] < r['loop_end'] < n):
            errs.append(f'{r["sample"]} boucle')
    for lay in {(r['lovel'], r['hivel']) for r in regions}:
        keys = sorted((r['lokey'], r['hikey']) for r in regions if (r['lovel'], r['hivel']) == lay)
        for a, b in zip(keys, keys[1:]):
            if b[0] != a[1] + 1:
                errs.append(f'trou de touches {a} {b}')
    picks = sorted(regions, key=lambda r: r['pitch_keycenter'])
    for r in [picks[len(picks) // 3], picks[2 * len(picks) // 3]]:
        p = os.path.join(folder, 'samples', r['sample'])
        with wave.open(p, 'rb') as w:
            sr, ch = w.getframerate(), w.getnchannels()
            x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, ch) / 32768.0
        est = f0(x, sr)
        want = 440.0 * 2 ** ((r['pitch_keycenter'] - 69 - r.get('tune', 0) / 100.0) / 12)
        cents.append(round(1200 * math.log2(est / want)) if est > 0 else None)
    return errs, cents

def main():
    global SRC, OUT
    if len(sys.argv) != 3:
        print(__doc__ or 'usage : build_instruments.py <sources> <sortie>')
        sys.exit(1)
    SRC, OUT = sys.argv[1], sys.argv[2]
    os.makedirs(OUT, exist_ok=True)
    rows = []
    only = os.environ.get('RAW2_ONLY')
    if os.environ.get('RAW2_MAKEUP_ONLY'):  # recalcule seulement les gains d'instruments déjà construits
        for rec in RECIPES:
            folder = os.path.join(OUT, rec['name'])
            if os.path.isdir(folder):
                print(f'{rec["name"]}: +{apply_makeup(folder, rec["name"]):.1f} dB', flush=True)
        return
    for rec in RECIPES:
        if only and rec['name'] != only:
            continue
        try:
            regions, folder, sr = build(rec)
            errs, cents = verify(rec['name'], regions, folder)
            makeup = apply_makeup(folder, rec['name'])
            size = sum(os.path.getsize(os.path.join(folder, 'samples', f)) for f in os.listdir(os.path.join(folder, 'samples'))) / 1e6
            lay = len({(r['lovel'], r['hivel']) for r in regions})
            loops = any(r.get('loop_mode') == 'loop_continuous' for r in regions)
            rows.append(f'| {rec["name"]} | {rec["cat"]} | {len(regions)} | {lay} | {"oui" if loops else "non"} | {size:.1f} | {sr} | {cents} | +{makeup:.1f} | {"; ".join(errs[:3]) or "ok"} |')
        except Exception as e:  # une recette en échec n'arrête pas les autres
            rows.append(f'| {rec["name"]} | {rec["cat"]} | - | - | - | - | - | - | - | ÉCHEC : {e} |')
        print(rows[-1], flush=True)
    if only:  # reconstruction d'un seul instrument : le rapport complet reste intact
        return
    with open(os.path.join(OUT, 'REPORT.md'), 'w', encoding='utf-8') as f:
        f.write('| Instrument | Cat. | Zones | Couches | Boucles | Mo | Hz | Écart de hauteur (cents) | Gain | Contrôles |\n|---|---|---|---|---|---|---|---|---|---|\n')
        f.write('\n'.join(rows) + '\n')

if __name__ == '__main__':
    main()
