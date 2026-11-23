#!/usr/bin/env python3
# Analyse la musique d'une pub image par image (30 i/s) pour que la scène réagisse au son :
# niveau, graves / médiums / aigus, flux spectral (attaques), 32 bandes de spectre, chroma (12 notes,
# pour les touches du clavier 3D), coups de grosse caisse détectés dans les graves, et un morceau de
# la forme d'onde (oscilloscopes 3D).
#   python3 promo/audio_features.py <musique.wav> <sortie.json> [fps]
import json, sys, wave
import numpy as np


def read_wav(path):
    w = wave.open(path)
    n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    elif sw == 2:
        x = np.frombuffer(raw, dtype=np.int16) / 32768.0
    else:
        raise SystemExit('WAV 16 ou 24 bits attendu')
    return x.reshape(-1, ch).mean(axis=1), sr


def norm(v, lo=5, hi=99.5):
    a, b = np.percentile(v, lo), np.percentile(v, hi)
    return np.clip((v - a) / max(b - a, 1e-9), 0, 1)


def main():
    path, out = sys.argv[1], sys.argv[2]
    fps = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    x, sr = read_wav(path)
    hop, win = sr // fps, 4096
    nfr = int(np.ceil(len(x) / hop))
    pad = np.concatenate([np.zeros(win), x, np.zeros(win)])
    hann = np.hanning(win)
    freqs = np.fft.rfftfreq(win, 1 / sr)
    edges = np.geomspace(30, 16000, 33)
    bidx = []
    for i in range(32):  # bandes étroites du grave : au moins le point FFT le plus proche
        m = (freqs >= edges[i]) & (freqs < edges[i + 1])
        if not m.any():
            m = np.zeros_like(freqs, dtype=bool)
            m[np.argmin(np.abs(freqs - np.sqrt(edges[i] * edges[i + 1])))] = True
        bidx.append(m)
    lowm, midm, highm = freqs < 150, (freqs >= 150) & (freqs < 2000), (freqs >= 2000) & (freqs < 12000)
    rms, low, mid, high, flux, bands, chroma, scope, lowflux = [], [], [], [], [], [], [], [], []
    prev = None
    prevlow = None
    # classe de note de chaque point FFT (70 Hz à 2 kHz : la partie tonale)
    pc = np.full(len(freqs), -1)
    ok = (freqs > 70) & (freqs < 2000)
    pc[ok] = (np.round(12 * np.log2(freqs[ok] / 440.0)) + 69).astype(int) % 12
    kickm = (freqs >= 35) & (freqs < 120)
    for f in range(nfr):
        c = win + f * hop
        seg = pad[c - win // 2: c + win // 2] * hann
        mag = np.abs(np.fft.rfft(seg)) + 1e-9
        logm = np.log10(mag)
        rms.append(np.sqrt(np.mean(seg ** 2)))
        low.append(np.log10(np.sum(mag[lowm] ** 2) + 1e-9))
        mid.append(np.log10(np.sum(mag[midm] ** 2) + 1e-9))
        high.append(np.log10(np.sum(mag[highm] ** 2) + 1e-9))
        flux.append(0.0 if prev is None else float(np.sum(np.maximum(0, logm - prev))))
        prev = logm
        bands.append([20 * np.log10(np.mean(mag[m]) + 1e-9) for m in bidx])
        ch = np.array([np.sum(mag[pc == k] ** 2) for k in range(12)])
        chroma.append(ch / (ch.max() + 1e-12))
        lk = np.log10(np.sum(mag[kickm] ** 2) + 1e-9)
        lowflux.append(0.0 if prevlow is None else max(0.0, lk - prevlow))
        prevlow = lk
        # oscilloscope : 256 points à partir d'un passage par zéro montant (image stable)
        raw = pad[c - 1024: c + 1536]
        z = np.where((raw[:-1] <= 0) & (raw[1:] > 0))[0]
        z0 = int(z[0]) if len(z) and z[0] < 1024 else 0
        sc = raw[z0: z0 + 1024][::4]
        scope.append(sc / (np.abs(pad).max() + 1e-9))
    bands = np.array(bands)
    bands = np.clip((bands - np.percentile(bands, 20)) / max(np.percentile(bands, 99.5) - np.percentile(bands, 20), 1e-9), 0, 1)
    r3 = lambda v: [round(float(a), 3) for a in v]
    data = {'fps': fps, 'n': nfr, 'rms': r3(norm(np.log10(np.array(rms) + 1e-6))), 'low': r3(norm(np.array(low))),
            'mid': r3(norm(np.array(mid))), 'high': r3(norm(np.array(high))), 'flux': r3(norm(np.array(flux), 10, 99)),
            'bands': [r3(b) for b in bands], 'chroma': [r3(c) for c in chroma],
            'kick': r3(norm(np.array(lowflux), 50, 99.5)), 'scope': [r3(s) for s in scope]}
    json.dump(data, open(out, 'w'), separators=(',', ':'))
    print(f'analyse : {nfr} images -> {out}')


if __name__ == '__main__':
    main()
