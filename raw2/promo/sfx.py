#!/usr/bin/env python3
# Bruitages de la pub HISTOIRE, synthétisés et calés sur histoire.json, mixés sur la musique.
#   python3 promo/sfx.py <musique> <histoire.json> <sortie.wav> <meta.json>
# Sons ponctuels (liste « bruitages ») + sons répétés déduits du montage : pas pendant la marche,
# bips des dialogues (un par lettre), cliquet du potard, sauts, anneaux de LED, thèmes.
import json, subprocess, sys, wave
import numpy as np

SR = 48000
rng = np.random.default_rng(20251124)


def t_(d):
    return np.arange(int(d * SR)) / SR


def env(n, a=0.002, r=None, curve=6.0):
    t = np.arange(n) / SR
    e = np.minimum(1, t / max(a, 1e-4))
    if r is not None:
        e *= np.exp(-t * curve / r)
    return e


def sine_glide(f0, f1, d, shape='exp'):
    t = t_(d)
    f = f0 * (f1 / f0) ** (t / d) if shape == 'exp' else f0 + (f1 - f0) * t / d
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def lp(x, fc):
    a = np.exp(-2 * np.pi * fc / SR)
    y = np.empty_like(x); s = 0.0
    for i, v in enumerate(x):
        s = (1 - a) * v + a * s; y[i] = s
    return y


def bp_noise(d, f0, f1, q=6.0):
    """bruit filtré passe-bande dont la fréquence glisse de f0 à f1 (filtre d'état variable)"""
    n = int(d * SR); x = rng.standard_normal(n); y = np.zeros(n)
    low = band = 0.0
    for i in range(n):
        fc = f0 * (f1 / f0) ** (i / n)
        f = 2 * np.sin(np.pi * min(fc, SR / 6) / SR)
        high = x[i] - low - band / q
        band += f * high; low += f * band
        y[i] = band
    return y / (np.abs(y).max() + 1e-9)


def click(freq=2500, d=0.012):
    t = t_(d)
    return np.sin(2 * np.pi * freq * t) * np.exp(-t * 600) + rng.standard_normal(len(t)) * np.exp(-t * 900) * 0.5


# ---------------------------------------------------------------- sons
def s_boing(h=1.0):
    d = 0.55; t = t_(d); f0 = 190 * h
    f = f0 * (1 + 1.1 * np.exp(-t * 16)) * (1 + 0.06 * np.sin(2 * np.pi * 11 * t) * np.exp(-t * 4))
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 5.5)
    x[:len(t_(0.05))] += np.sin(2 * np.pi * 70 * t_(0.05)) * np.exp(-t_(0.05) * 60) * 0.8
    return x


def s_pop(h=1.0):
    d = 0.09; t = t_(d)
    x = sine_glide(900 * h, 300 * h, d) * np.exp(-t * 45)
    x[:len(t_(0.012))] += click(3200, 0.012) * 0.4
    return x


def s_chute():
    d = 0.45; return sine_glide(1700, 520, d) * env(int(d * SR), 0.02) * np.hanning(int(d * SR)) * 0.6


def s_souffle():
    d = 0.7; return bp_noise(d, 300, 2600, 2.5) * np.hanning(int(d * SR)) ** 1.5


def s_clac():
    x = np.zeros(int(0.25 * SR)); c = click(2200, 0.02); x[:len(c)] += c
    t = t_(0.12); x[:len(t)] += np.sin(2 * np.pi * 95 * t) * np.exp(-t * 35) * 0.9
    return x


def s_allumage():
    d = 1.7; t = t_(d)
    f = 70 * (650 / 70) ** ((t / d) ** 0.7)
    ph = np.cumsum(f) / SR
    saw = 2 * (ph % 1) - 1
    x = lp(saw, 1800) * np.minimum(1, t / 0.25) * np.exp(-np.maximum(0, t - 1.3) * 6)
    x += np.sin(2 * np.pi * 9200 * t) * 0.05 * np.minimum(1, t / 0.6)
    return x


def s_gresille():
    d = 0.35; n = int(d * SR); x = np.zeros(n)
    for _ in range(90):
        i = rng.integers(0, n - 200); x[i:i + 60] += rng.standard_normal(60) * np.exp(-np.arange(60) / 12)
    x += np.sin(2 * np.pi * 50 * t_(d)) * 0.15
    return x * np.hanning(n) ** 0.3


def s_cascade():
    notes = [0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27, 29, 31, 34, 36]
    x = np.zeros(int(1.9 * SR))
    for k, n in enumerate(notes):
        f = 440 * 2 ** ((n + 3) / 12); d = 0.06; t = t_(d)
        b = np.sign(np.sin(2 * np.pi * f * t)) * np.exp(-t * 50) * 0.5
        i = int(k * 0.1 * SR); x[i:i + len(b)] += b
    return lp(x, 6000)


def s_envol():
    d = 0.35; return sine_glide(280, 950, d) * np.hanning(int(d * SR)) + bp_noise(d, 600, 3000, 2) * 0.25 * np.hanning(int(d * SR))


def s_atterrit():
    t = t_(0.3); x = np.sin(2 * np.pi * 62 * t) * np.exp(-t * 14)
    x[:len(t_(0.03))] += rng.standard_normal(len(t_(0.03))) * np.exp(-t_(0.03) * 120) * 0.5
    return x


def s_montee():
    d = 0.8; return bp_noise(d, 400, 7000, 3) * np.linspace(0, 1, int(d * SR)) ** 2


def s_touche():
    x = np.zeros(int(0.2 * SR)); c = click(3000, 0.015); x[:len(c)] += c
    t = t_(0.1); x[:len(t)] += np.sin(2 * np.pi * 130 * t) * np.exp(-t * 40)
    return x


def s_impact():
    d = 1.6; t = t_(d)
    sub = np.sin(2 * np.pi * np.cumsum(38 + 60 * np.exp(-t * 18)) / SR) * np.exp(-t * 2.6)
    crash = rng.standard_normal(len(t)); crash = crash - lp(crash, 2500)
    crash *= np.exp(-t * 3.2) * 0.35
    return sub + crash


def s_glitch():
    d = 0.14; n = int(d * SR); x = rng.standard_normal(n)
    x = np.round(x * 3) / 3  # quantifié
    for k in range(0, n, 900):  # bégaiement
        if (k // 900) % 2: x[k:k + 900] = x[max(0, k - 900):k][:len(x[k:k + 900])]
    return x * np.hanning(n) ** 0.5


def s_carillon():
    d = 1.4; t = t_(d); x = np.zeros(len(t))
    for f, a in ((880, 1.0), (1320, 0.6), (1760, 0.3)):
        x += a * np.sin(2 * np.pi * f * t + 2.2 * np.sin(2 * np.pi * f * 1.4 * t) * np.exp(-t * 6)) * np.exp(-t * 3)
    return x / 2


def s_pas():
    d = 0.08; t = t_(d)
    x = lp(rng.standard_normal(len(t)), 900) * np.exp(-t * 60) * 3 + np.sin(2 * np.pi * 110 * t) * np.exp(-t * 50)
    x[:len(t_(0.006))] += click(4200, 0.006) * 0.25  # petit tintement de chrome
    return x


def s_bip():
    d = 0.024; f = 1250 + rng.uniform(-150, 150); t = t_(d)
    return np.sign(np.sin(2 * np.pi * f * t)) * np.exp(-t * 90) * 0.5


def s_cliquet():
    return click(3400 + rng.uniform(-300, 300), 0.008)


def s_blip(h=1.0):
    d = 0.05; t = t_(d); return np.sin(2 * np.pi * 1800 * h * t) * np.exp(-t * 70)


SOUNDS = {'boing': s_boing, 'pop': s_pop, 'chute': s_chute, 'souffle': s_souffle, 'clac': s_clac, 'allumage': s_allumage,
          'grésille': s_gresille, 'cascade': s_cascade, 'envol': s_envol, 'atterrit': s_atterrit, 'montée': s_montee,
          'touche': s_touche, 'impact': s_impact, 'glitch': s_glitch, 'carillon': s_carillon}


def reverb(x, wet=0.16, d=0.9):
    n = int(d * SR); ir = rng.standard_normal(n) * np.exp(-np.arange(n) / SR * 7)
    ir = lp(ir, 5000); ir /= np.sqrt(np.sum(ir ** 2))
    L = len(x) + n; F = 1 << (L - 1).bit_length()
    y = np.fft.irfft(np.fft.rfft(x, F) * np.fft.rfft(ir, F), F)[:len(x)]
    return x + wet * y


def main():
    music, story, out, meta = sys.argv[1:5]
    H = json.load(open(story))
    M, T = H['musique'], H['temps']
    total = T['total']
    N = int(total * SR)
    fx = np.zeros(N)

    def put(t, x, gain_db):
        i = int(t * SR)
        if i >= N: return
        x = x[: N - i] * 10 ** (gain_db / 20) / (np.abs(x).max() + 1e-9)
        fx[i: i + len(x)] += x

    for c in H['bruitages']:
        f = SOUNDS[c['son']]
        x = f(c['hauteur']) if 'hauteur' in c else f()
        put(c['t'], x, c['gain'])
    # pas pendant la marche (même cadence que la scène : 2,2 cycles/s, un pas par demi-cycle)
    a, b = T['marche']
    k = 0
    while a + 0.12 + k / 4.4 < b:
        put(a + 0.12 + k / 4.4, s_pas(), -15 - (k % 2) * 2); k += 1
    # bips des dialogues : un par lettre (pas les espaces), à la vitesse de frappe
    for dlg in H['dialogues']:
        for i, ch in enumerate(dlg['texte']):
            if ch.strip() and ch not in '…':
                put(dlg['t'] + i / H['cps'], s_bip(), -27)
    beat = 60 / M['bpm']
    p = T['plans']
    # après le drop : le bonhomme saute sur chaque temps
    tt = T['drop'] + beat
    while tt < p[0] - 0.05:
        put(tt, s_boing(1.7), -21); tt += beat
    # plan VIE : un anneau de LED par temps
    for j in range(4):
        put(p[1] + j * beat, s_blip(1 + 0.12 * j), -20)
    # plan potard : cliquet du cran tous les 1/10 s
    tt = p[2] + 0.1
    while tt < p[3] - 0.1:
        put(tt, s_cliquet(), -21); tt += 0.1
    # plan Wurlitzer : un petit saut par temps
    for j in range(4):
        put(p[3] + 0.05 + j * beat, s_pop(1.6 + 0.1 * j), -19)
    # plan thèmes : un tintement par thème
    for j in range(6):
        put(p[4] + 0.05 + j * (T['fin'] - p[4]) / 6, s_blip(1.4 + 0.15 * j), -22)
    fx = reverb(fx)
    # musique
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(M['debut']), '-t', str(total), '-i', music, '-f', 'f32le', '-ac', '2', '-ar', str(SR), '-'],
                         capture_output=True, check=True).stdout
    mus = np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).copy()
    if len(mus) < N: mus = np.concatenate([mus, np.zeros((N - len(mus), 2), np.float32)])
    mus = mus[:N] * 10 ** (M['gain_db'] / 20)
    mix = mus + fx[:, None] * np.array([1.0, 1.0])
    fo = int(M['fondu'] * SR)
    mix[-fo:] *= np.linspace(1, 0, fo)[:, None]
    mix = np.tanh(mix * 1.1) / np.tanh(1.1)       # limiteur doux
    mix = mix / (np.abs(mix).max() + 1e-9) * 10 ** (-1 / 20)
    pcm = np.clip(np.round(mix * 8388607), -8388608, 8388607).astype(np.int32)
    b3 = np.stack([(pcm >> s) & 255 for s in (0, 8, 16)], axis=-1).astype(np.uint8).reshape(-1)
    w = wave.open(out, 'wb'); w.setnchannels(2); w.setsampwidth(3); w.setframerate(SR); w.writeframes(b3.tobytes()); w.close()
    json.dump({'external': True, 'warp': False, 'bpm': M['bpm'], 'phase': M.get('phase', 0), 'start': M['debut'], 'duration': total}, open(meta, 'w'))
    print(f'histoire : {total:.2f} s, {len(H["bruitages"])} bruitages + pas, bips, cliquets -> {out}')


if __name__ == '__main__':
    main()
