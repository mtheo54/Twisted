#!/usr/bin/env python3
# Choisit le meilleur passage d'une musique pour une pub et le cale sur le montage.
#   python3 promo/pick_segment.py <musique (mp3, wav, …)> <lancement|le-son> <sortie.wav> <meta.json> [début en s]
# 1. tempo détecté (autocorrélation des attaques et de leurs multiples), entre 90 et 160 BPM ;
# 2. grille des temps (phase qui colle le mieux aux attaques) ;
# 3. le passage retenu commence sur une mesure et fait « drop » pile quand l'appareil apparaît
#    (temps 8 pour LANCEMENT, temps 4 pour LE SON) : énergie forte et montée nette à cet endroit ;
# 4. extrait en WAV 48 kHz 24 bits stéréo, fondu de fin ; meta.json donne le BPM et le début.
# Le montage est écrit à 120 BPM : la scène l'étire ou le resserre au tempo trouvé.
import json, subprocess, sys
import numpy as np

AD = {'lancement': {'beats': 40, 'drop': 8}, 'le-son': {'beats': 32, 'drop': 4}}


def decode(path, sr, ch):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', str(ch), '-ar', str(sr), '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, ch)


def main():
    src, ad, out_wav, out_meta = sys.argv[1:5]
    forced = float(sys.argv[5]) if len(sys.argv) > 5 else None
    cfg = AD[ad]
    sr = 22050
    x = decode(src, sr, 1)[:, 0]
    hop, win = 512, 2048
    n = 1 + (len(x) - win) // hop
    hann = np.hanning(win)
    frames = np.lib.stride_tricks.sliding_window_view(x, win)[::hop][:n] * hann
    mag = np.abs(np.fft.rfft(frames, axis=1))
    logm = np.log1p(10 * mag)
    flux = np.maximum(0, np.diff(logm, axis=0)).sum(axis=1)
    flux = np.concatenate([[0], flux])
    flux = (flux - flux.mean()) / (flux.std() + 1e-9)
    rms = np.sqrt((frames ** 2).mean(axis=1))
    fps = sr / hop
    # tempo : autocorrélation des attaques ; chaque candidat additionne aussi ses multiples (mesure,
    # demi-mesure), avec une préférence douce autour de 120 BPM
    ac = np.correlate(flux, flux, mode='full')[len(flux) - 1:]
    ac = ac / (ac[0] + 1e-9)
    best_b, best_v = 120.0, -1e9
    for b in np.arange(90, 160, 0.25):
        lagf = 60 * fps / b
        v = 0.0
        for m, wgt in ((1, 1.0), (2, 0.6), (4, 0.4), (0.5, 0.25)):
            l = lagf * m
            if 1 <= l < len(ac) - 1:
                i0 = int(l); fr = l - i0
                v += wgt * (ac[i0] * (1 - fr) + ac[i0 + 1] * fr)
        v *= np.exp(-0.5 * (np.log2(b / 120) / 0.6) ** 2) ** 0.3
        if v > best_v:
            best_b, best_v = b, v
    bpm = best_b
    period = 60 * fps / bpm
    # phase de la grille
    phases = np.arange(0, period, 0.5)
    score = [flux[np.round(np.arange(p, len(flux) - 1, period)).astype(int)].sum() for p in phases]
    phase = phases[int(np.argmax(score))]
    beats = np.arange(phase, len(flux) - 1, period)
    beat_t = beats / fps
    e = np.array([rms[int(b): int(b + period)].mean() if int(b + period) < len(rms) else 0 for b in beats])
    e = e / (e.max() + 1e-9)
    need, drop = cfg['beats'], cfg['drop']
    best, best_s = 0, -1e9
    for i in range(0, len(beats) - need, 4):  # départ sur une mesure (4 temps)
        if forced is not None:
            break
        win_e = e[i: i + need]
        before = e[i + drop - 4: i + drop].mean() if drop >= 4 else e[i: i + drop].mean()
        after = e[i + drop: i + drop + 4].mean()
        s = win_e.mean() + 1.5 * (after - before) + 0.5 * after
        if s > best_s:
            best, best_s = i, s
    start = forced if forced is not None else float(beat_t[best])
    dur = need * 60 / bpm
    st = decode(src, 48000, 2)
    a, b = int(start * 48000), int((start + dur) * 48000)
    seg = st[a: b].copy()
    if len(seg) < b - a:
        seg = np.concatenate([seg, np.zeros((b - a - len(seg), 2), dtype=np.float32)])
    fin = int(0.012 * 48000)
    seg[:fin] *= np.linspace(0, 1, fin)[:, None]
    fo = int(min(1.5, dur * 0.1) * 48000)
    seg[-fo:] *= np.linspace(1, 0, fo)[:, None]
    seg = seg / max(1e-9, np.abs(seg).max()) * 10 ** (-1 / 20)
    pcm = np.clip(np.round(seg * 8388607), -8388608, 8388607).astype(np.int32)
    b3 = np.stack([(pcm >> s) & 255 for s in (0, 8, 16)], axis=-1).astype(np.uint8).reshape(-1)
    import wave
    w = wave.open(out_wav, 'wb')
    w.setnchannels(2); w.setsampwidth(3); w.setframerate(48000)
    w.writeframes(b3.tobytes())
    w.close()
    json.dump({'external': True, 'bpm': round(float(bpm), 3), 'start': round(start, 3), 'duration': round(dur, 3), 'source': src.split('/')[-1]},
              open(out_meta, 'w'))
    print(f'{ad} : {bpm:.1f} BPM, passage {start:.2f} s → {start + dur:.2f} s ({dur:.1f} s)')


if __name__ == '__main__':
    main()
