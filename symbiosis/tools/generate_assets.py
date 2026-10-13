#!/usr/bin/env python3
"""
SYMBIOSIS - offline texture generator for the VSTGUI editor.

Renders, per pixel and with supersampling, the textures that vector drawing in
CDrawContext cannot do well (per-pixel lighting, bevels, blur, bloom):

  background.png      ivory paper, marbled grey ink smoke, black ink specks, slight
                      radial darkening, calm zones behind faders and labels
  astrolabe_ring.png  brushed, bevelled gunmetal ring, toothed crown, light enamel
                      band with dense raised black runes, zodiac and planetary
                      glyphs, degree scale, soft drop shadow
  knob_bezel.png      knurled dark steel bezel with a chrome lip (with shadow)
  knob_gem_smoke.png  faceted smoky crystal (PULL): vertical filmstrip, 16 frames of
                      the stone turning through one 45-degree period of its cut,
                      each lit under the same fixed light
  knob_cap.png        glossy black steel dome with turning rings (HARMONICS), 1 frame
  knob_gloss.png      fixed-light gloss: window reflection, specular, rim light
  plasma_a.png        ink-smoke wisps for the Orbit visualizer
  plasma_b.png        red mist, cross-faded with the ink smoke

Coordinates are in editor canvas units (860 x 560 canvas, see symbiosis_editor.h);
each texture is written at a fixed pixel density (scale) that the editor declares
to VSTGUI with IPlatformBitmap::setScaleFactor.

Requirements: Python 3.9+, numpy, Pillow.  Usage:
    python3 symbiosis/tools/generate_assets.py [output_dir]
The output is deterministic (fixed seeds).
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "resource", "textures")

# pixel densities (pixels per canvas unit); must match kAssetScale* in symbiosis_editor.cpp
SCALE_BACKGROUND = 2.0
SCALE_RING = 2.4
SCALE_KNOB = 3.0
SCALE_PLASMA = 2.0
SUPERSAMPLE = 2

CANVAS_W, CANVAS_H = 860, 560
ORBIT = (430.0, 245.0)
LIGHT = np.array([-0.52, -0.62, 0.86])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


# --------------------------------------------------------------------------------------
# helpers
def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def noise(h, w, cells, rng):
    """Smooth value noise in [0, 1] (bicubic upsampling of a random lattice)."""
    cy = max(2, int(round(cells * h / max(h, w))))
    cx = max(2, int(round(cells * w / max(h, w))))
    lattice = rng.random((cy + 3, cx + 3)).astype(np.float32)
    img = Image.fromarray(lattice, "F").resize((w + 2 * w // cx, h + 2 * h // cy), Image.BICUBIC)
    a = np.asarray(img, dtype=np.float32)
    oy, ox = h // cy, w // cx
    return np.clip(a[oy:oy + h, ox:ox + w], 0.0, 1.0)


def fbm(h, w, cells, octaves, rng, gain=0.5):
    total = np.zeros((h, w), np.float32)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += amp * noise(h, w, cells * (2 ** o), rng)
        norm += amp
        amp *= gain
    return total / norm


def sample(a, x, y):
    """Bilinear lookup of a 2-D array at float coordinates (clamped)."""
    h, w = a.shape
    x = np.clip(x, 0, w - 1.001)
    y = np.clip(y, 0, h - 1.001)
    x0 = np.floor(x).astype(np.int32)
    y0 = np.floor(y).astype(np.int32)
    fx, fy = x - x0, y - y0
    v00, v10 = a[y0, x0], a[y0, x0 + 1]
    v01, v11 = a[y0 + 1, x0], a[y0 + 1, x0 + 1]
    return (v00 * (1 - fx) + v10 * fx) * (1 - fy) + (v01 * (1 - fx) + v11 * fx) * fy


def box_blur(a, r):
    r = int(r)
    if r < 1:
        return a
    out = a.astype(np.float32)
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (r + 1, r)
        p = np.pad(out, pad, mode="edge")
        c = np.cumsum(p, axis=axis, dtype=np.float64)
        if axis == 0:
            out = ((c[2 * r + 1:, :] - c[:-2 * r - 1, :]) / (2 * r + 1)).astype(np.float32)
        else:
            out = ((c[:, 2 * r + 1:] - c[:, :-2 * r - 1]) / (2 * r + 1)).astype(np.float32)
    return out


def blur(a, sigma):
    """~Gaussian blur: three box passes."""
    if sigma <= 0.3:
        return a
    r = max(1, int(round(sigma * 0.85)))
    return box_blur(box_blur(box_blur(a, r), r), r)


def ramp(t, stops):
    """Colour ramp: stops = [(position, (r, g, b)), ...] -> float RGB array."""
    t = np.clip(t, 0.0, 1.0)
    pos = np.array([s[0] for s in stops], np.float32)
    cols = np.array([s[1] for s in stops], np.float32) / 255.0
    out = np.empty(t.shape + (3,), np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, pos, cols[:, c])
    return out


def hex_rgb(h):
    return (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16))


def normals_from_height(hgt, pixel):
    """Surface normals from a height field (heights in canvas units, pixel = canvas units per pixel)."""
    gy, gx = np.gradient(hgt, pixel)
    n = np.stack([-gx, -gy, np.ones_like(hgt)], axis=-1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def shade(n, shininess=40.0):
    diff = np.clip(np.tensordot(n, LIGHT, axes=([-1], [0])), 0.0, 1.0)
    half = LIGHT + np.array([0.0, 0.0, 1.0])
    half /= np.linalg.norm(half)
    spec = np.clip(np.tensordot(n, half, axes=([-1], [0])), 0.0, 1.0) ** shininess
    return diff, spec


def save_rgba(path, rgb, alpha, downsample=1):
    img = np.concatenate([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)[..., None]], axis=-1)
    im = Image.fromarray((img * 255.0 + 0.5).astype(np.uint8), "RGBA")
    if downsample > 1:
        im = im.resize((im.width // downsample, im.height // downsample), Image.LANCZOS)
    im.save(path, optimize=True)
    print(f"  {os.path.basename(path):22s} {im.width}x{im.height}  {os.path.getsize(path) / 1024:.0f} KiB")


def save_rgb(path, rgb):
    im = Image.fromarray((np.clip(rgb, 0, 1) * 255.0 + 0.5).astype(np.uint8), "RGB")
    im.save(path, optimize=True)
    print(f"  {os.path.basename(path):22s} {im.width}x{im.height}  {os.path.getsize(path) / 1024:.0f} KiB")


# --------------------------------------------------------------------------------------
# 1. Background: ivory paper, marbled grey ink smoke, black ink specks
def make_background():
    s = SCALE_BACKGROUND
    w, h = int(CANVAS_W * s), int(CANVAS_H * s)
    rng = np.random.default_rng(5359)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    cx, cy = xx / s, yy / s  # canvas coordinates

    # domain-warped smoke, with thin marble veins
    wx = (fbm(h, w, 5, 4, rng) - 0.5) * 240.0 * s
    wy = (fbm(h, w, 5, 4, rng) - 0.5) * 240.0 * s
    dens = sample(fbm(h, w, 4, 7, rng), xx + wx, yy + wy)
    veins = (1.0 - np.abs(2.0 * sample(fbm(h, w, 8, 5, rng), xx + 1.7 * wx, yy + 1.7 * wy) - 1.0)) ** 10
    fine = fbm(h, w, 60, 3, rng)

    # where the smoke is: soft masses that reach the edges (relief everywhere)
    blobs = [(300, 30, 190, 1.0), (720, 250, 190, 1.0), (860, 140, 160, 0.9), (40, 300, 170, 0.8),
             (560, 520, 190, 0.7), (200, 470, 160, 0.6), (470, 40, 170, 0.7), (880, 470, 170, 0.8),
             (0, 560, 180, 0.7), (120, 120, 140, 0.5), (330, 330, 160, 0.5), (620, 380, 150, 0.5)]
    mask = np.full((h, w), 0.18, np.float32)
    for bx, by, br, bw in blobs:
        mask += bw * np.exp(-((cx - bx) ** 2 + (cy - by) ** 2) / (br * br))
    mask = np.minimum(mask, 1.1)
    smoke = np.clip(smoothstep(0.38, 0.85, dens) * mask, 0, 1)
    smoke = blur(smoke, 1.2 * s) * (0.85 + 0.3 * fine)
    ink = np.clip(0.48 * smoke + 0.5 * veins * smoothstep(0.1, 0.6, smoke), 0, 1)

    paper = np.array([0.948, 0.944, 0.936])
    rgb = paper * (1.0 - 0.012 * (fine[..., None] - 0.5))  # paper grain
    smoke_col = np.array([0.38, 0.38, 0.40])
    rgb = rgb * (1 - ink[..., None]) + smoke_col * ink[..., None]

    # black ink specks and splatter
    specks = np.zeros((h, w), np.float32)
    n_small = 9000
    sx = rng.integers(0, w, n_small)
    sy = rng.integers(0, h, n_small)
    specks[sy, sx] = rng.power(5.0, n_small) ** 5 * 0.9 + 0.1
    big = np.zeros((h, w), np.float32)
    for _ in range(260):
        x0, y0 = int(rng.integers(0, w)), int(rng.integers(0, h))
        big[y0, x0] = 1.0
    dots = np.clip(specks + blur(big, 0.9 * s) * 14.0 + blur(big, 2.2 * s) * 6.0, 0, 1)
    rgb *= (1.0 - 0.92 * dots)[..., None]

    # periphery slightly darkened (radial), calm zones behind controls and labels
    nx = (cx - CANVAS_W * 0.5) / (CANVAS_W * 0.5)
    ny = (cy - CANVAS_H * 0.46) / (CANVAS_H * 0.5)
    r = np.sqrt(nx * nx + ny * ny)
    vignette = 1.0 - 0.16 * smoothstep(0.6, 1.3, r)
    rgb *= vignette[..., None]
    calm = np.zeros((h, w), np.float32)
    for (x0, y0, x1, y1) in [(40, 270, 230, 500), (630, 270, 820, 500)]:
        calm = np.maximum(calm, smoothstep(-70, 30, np.minimum.reduce([cx - x0, x1 - cx, cy - y0, y1 - cy])))
    calm = blur(calm, 20 * s)
    rgb = rgb * (1 - 0.3 * calm[..., None]) + paper * 0.3 * calm[..., None]
    save_rgb(os.path.join(OUT, "background.png"), rgb)


# --------------------------------------------------------------------------------------
# glyphs: runes, zodiac, planets, stars as stroke lists in a [-1, 1] box (y down)
def arc(cx, cy, r, a0, a1, n=14):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


S = [(0, -1), (0, 1)]  # stave
RUNES = [
    [S, [(0, -0.2), (0.7, -0.8)], [(0, 0.2), (0.7, -0.3)]],                     # fehu
    [[(-0.5, 1), (-0.5, -1), (0.5, -0.4), (0.5, 1)]],                            # uruz
    [[(-0.3, -1), (-0.3, 1)], [(-0.3, -0.5), (0.4, 0), (-0.3, 0.5)]],          # thurisaz
    [[(-0.3, -1), (-0.3, 1)], [(-0.3, -1), (0.45, -0.5)], [(-0.3, -0.4), (0.45, 0.1)]],  # ansuz
    [[(-0.4, 1), (-0.4, -1), (0.4, -0.5), (-0.4, 0), (0.4, 1)]],                 # raido
    [[(0.4, -0.6), (-0.3, 0), (0.4, 0.6)]],                                     # kaunan
    [[(-0.6, -1), (0.6, 1)], [(0.6, -1), (-0.6, 1)]],                           # gebo
    [[(-0.3, 1), (-0.3, -1), (0.4, -0.5), (-0.3, 0)]],                           # wunjo
    [[(-0.5, -1), (-0.5, 1)], [(0.5, -1), (0.5, 1)], [(-0.5, -0.3), (0.5, 0.3)]],  # hagalaz
    [S, [(-0.5, -0.3), (0.5, 0.3)]],                                            # naudiz
    [[(-0.1, -0.85), (-0.6, -0.25), (-0.1, 0.35)], [(0.1, -0.35), (0.6, 0.25), (0.1, 0.85)]],  # jera
    [S, [(0, -1), (0.5, -0.6)], [(0, 1), (-0.5, 0.6)]],                         # eihwaz
    [[(-0.4, -1), (-0.4, 1)], [(-0.4, -1), (0.4, -0.5), (0.5, -0.85)], [(-0.4, 1), (0.4, 0.5), (0.5, 0.85)]],  # perthro
    [S, [(0, -0.2), (-0.6, -0.9)], [(0, -0.2), (0.6, -0.9)]],                   # algiz
    [[(0.4, -1), (-0.4, -0.3), (0.4, 0.3), (-0.4, 1)]],                          # sowilo
    [S, [(-0.6, -0.45), (0, -1), (0.6, -0.45)]],                                # tiwaz
    [[(-0.4, 1), (-0.4, -1), (0.4, -0.5), (-0.4, 0), (0.4, 0.5), (-0.4, 1)]],    # berkanan
    [[(-0.5, 1), (-0.5, -1), (0, -0.3), (0.5, -1), (0.5, 1)]],                   # ehwaz
    [[(-0.5, 1), (-0.5, -1), (0.5, -0.2)], [(0.5, 1), (0.5, -1), (-0.5, -0.2)]],  # mannaz
    [[(-0.3, 1), (-0.3, -1), (0.45, -0.4)]],                                     # laguz
    [[(0, -0.8), (0.5, 0), (0, 0.8), (-0.5, 0), (0, -0.8)]],                     # ingwaz
    [[(-0.5, -1), (-0.5, 1), (0.5, -1), (0.5, 1), (-0.5, -1)]],                   # dagaz
    [[(-0.6, 1), (0.35, 0), (0, -0.6), (-0.35, 0), (0.6, 1)]],                   # othala
]
ZODIAC = [
    [[(0, 0.9), (0, -0.2)], arc(-0.45, -0.3, 0.45, 0, -200), arc(0.45, -0.3, 0.45, 180, 380)],          # aries
    [arc(0, 0.35, 0.55, 0, 360, 24), arc(0, -1.05, 0.85, 150, 30)],                                      # taurus
    [[(-0.4, -0.7), (-0.4, 0.7)], [(0.4, -0.7), (0.4, 0.7)], arc(0, -1.6, 1.0, 120, 60), arc(0, 1.6, 1.0, 240, 300)],  # gemini
    [arc(-0.5, -0.4, 0.25, 0, 360, 12), arc(0.5, 0.4, 0.25, 0, 360, 12), arc(0, 0.2, 0.9, 200, 330), arc(0, -0.2, 0.9, 20, 150)],  # cancer
    [arc(-0.55, 0.3, 0.3, 0, 360, 12), [(-0.3, 0.1), (-0.4, -0.6), (0.2, -0.95), (0.6, -0.5), (0.3, 0.2), (0.5, 0.75), (0.85, 0.55)]],  # leo
    [[(-0.8, -0.6), (-0.8, 0.7)], arc(-0.55, -0.4, 0.25, 180, 360), [(-0.3, -0.4), (-0.3, 0.7)], arc(-0.05, -0.4, 0.25, 180, 360),
     [(0.2, -0.4), (0.2, 0.5), (0.5, 0.95)], arc(0.45, 0.3, 0.3, 120, 420)],                             # virgo
    [[(-0.9, 0.7), (0.9, 0.7)], [(-0.9, 0.2), (-0.35, 0.2)], arc(0, -0.05, 0.42, 145, 395), [(0.35, 0.2), (0.9, 0.2)]],  # libra
    [[(-0.9, -0.6), (-0.9, 0.7)], arc(-0.65, -0.4, 0.25, 180, 360), [(-0.4, -0.4), (-0.4, 0.7)], arc(-0.15, -0.4, 0.25, 180, 360),
     [(0.1, -0.4), (0.1, 0.6), (0.5, 0.9), (0.85, 0.5)], [(0.55, 0.5), (0.85, 0.5), (0.85, 0.85)]],       # scorpio
    [[(-0.8, 0.8), (0.8, -0.8)], [(0.1, -0.8), (0.8, -0.8), (0.8, -0.1)], [(-0.6, -0.2), (0.2, 0.6)]],     # sagittarius
    [[(-0.9, -0.6), (-0.5, 0.6), (-0.1, -0.6), (0.1, 0.5)], arc(0.45, 0.45, 0.35, 180, 520)],            # capricorn
    [[(-0.9, -0.1), (-0.45, -0.5), (0, -0.1), (0.45, -0.5), (0.9, -0.1)],
     [(-0.9, 0.55), (-0.45, 0.15), (0, 0.55), (0.45, 0.15), (0.9, 0.55)]],                                # aquarius
    [arc(-1.3, 0, 0.9, -50, 50), arc(1.3, 0, 0.9, 130, 230), [(-0.7, 0), (0.7, 0)]],                     # pisces
]
PLANETS = [
    [arc(0, 0, 0.75, 0, 360, 28), arc(0, 0, 0.12, 0, 360, 8)],                                           # sun
    [arc(0.15, 0, 0.8, 70, 290, 20), arc(0.55, 0, 0.62, 105, 255, 16)],                                  # moon
    [arc(0, -0.25, 0.42, 0, 360, 18), [(0, 0.17), (0, 0.95)], [(-0.35, 0.6), (0.35, 0.6)], arc(0, -0.95, 0.4, 20, 160)],  # mercury
    [arc(0, -0.3, 0.5, 0, 360, 20), [(0, 0.2), (0, 1.0)], [(-0.4, 0.62), (0.4, 0.62)]],                  # venus
    [arc(-0.2, 0.25, 0.5, 0, 360, 20), [(0.15, -0.1), (0.75, -0.75)], [(0.3, -0.75), (0.75, -0.75), (0.75, -0.3)]],  # mars
    [[(-0.7, -0.5), (-0.4, -0.9), (0, -0.7), (-0.6, 0.25), (0.8, 0.25)], [(0.4, -0.7), (0.4, 0.95)]],     # jupiter
    [[(-0.4, -1), (-0.4, 0.4)], [(-0.8, -0.6), (0.0, -0.6)], [(-0.4, -0.1), (0.2, -0.3), (0.5, 0.1), (0.2, 0.6), (0.5, 0.95)]],  # saturn
]


def star_glyph(points=8, r_out=0.9, r_in=0.32):
    pts = []
    for i in range(points * 2 + 1):
        r = r_out if i % 2 == 0 else r_in
        a = math.radians(-90 + 180.0 * i / points)
        pts.append((r * math.cos(a), r * math.sin(a)))
    return [pts]


# --------------------------------------------------------------------------------------
# 2. Astrolabe ring
R_DISC_EDGE = 158.0
R_RING_OUT = 183.0
R_TEETH = 192.0
R_WIRE = 196.0
R_BAND_IN, R_BAND_OUT = 200.0, 229.0
R_HAIRLINE = 238.0
R_IMAGE = 245.0


def make_ring():
    sc = SCALE_RING * SUPERSAMPLE
    size = int(2 * R_IMAGE * sc)
    rng = np.random.default_rng(1977)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    dx = (xx + 0.5) / sc - R_IMAGE
    dy = (yy + 0.5) / sc - R_IMAGE
    r = np.sqrt(dx * dx + dy * dy)
    ang = np.degrees(np.arctan2(dy, dx))  # screen angle: 0 = right, clockwise

    hgt = np.zeros((size, size), np.float32)
    gold = np.zeros((size, size), np.float32)   # 1 = gold material
    plate = np.zeros((size, size), np.float32)  # 1 = dark bronze plate
    aa = 1.0 / sc

    def band(r0, r1):
        return smoothstep(r0 - aa, r0 + aa, r) * (1 - smoothstep(r1 - aa, r1 + aa, r))

    # thick brushed ring: rounded profile, bevelled lips, engraved grooves
    ring = band(R_DISC_EDGE, R_RING_OUT)
    t = np.clip((r - R_DISC_EDGE) / (R_RING_OUT - R_DISC_EDGE), 0, 1)
    profile = 2.2 * np.clip(np.sin(np.pi * t), 0, 1) ** 0.6
    lip = 1.0 - smoothstep(0.0, 0.10, t) * (1 - smoothstep(0.90, 1.0, t))
    profile -= 0.8 * lip
    for g in (163.5, 177.5):
        profile -= 0.55 * np.exp(-((r - g) / 0.55) ** 2)
    # engraved graduations on the inner lip (every 2 degrees, longer every 10)
    tick_phase = np.abs(((ang + 360.0) % 2.0) - 1.0)
    major = np.abs(((ang + 360.0) % 10.0) - 5.0) > 4.75
    tick_len = np.where(major, 6.2, 3.6)
    ticks = (tick_phase > 0.86) & (r > R_DISC_EDGE + 1.6) & (r < R_DISC_EDGE + 1.6 + tick_len)
    profile -= 0.45 * ticks
    hgt += ring * profile
    gold = np.maximum(gold, ring)

    # gear teeth (180)
    tooth_phase = ((ang + 360.0) % 2.0) / 2.0
    tooth_w = 0.62 - 0.25 * smoothstep(R_RING_OUT, R_TEETH, r)
    tooth = (np.abs(tooth_phase - 0.5) < tooth_w * 0.5).astype(np.float32)
    teeth = band(R_RING_OUT - 0.5, R_TEETH) * tooth
    teeth = box_blur(teeth.astype(np.float32), 1)
    hgt += teeth * (1.4 - 0.6 * smoothstep(R_RING_OUT, R_TEETH, r))
    gold = np.maximum(gold, teeth)

    # wire, hairline
    for rw, wwide, hh in ((R_WIRE, 0.9, 0.8), (R_HAIRLINE, 0.7, 0.5)):
        wire = band(rw - wwide, rw + wwide)
        hgt += wire * hh * np.sqrt(np.clip(1 - ((r - rw) / wwide) ** 2, 0, 1))
        gold = np.maximum(gold, wire)

    # dark bronze band with bead rims
    pl = band(R_BAND_IN, R_BAND_OUT)
    plate = np.maximum(plate, pl)
    hgt += pl * 0.6
    for rb in (R_BAND_IN + 0.8, R_BAND_OUT - 0.8):
        bead = band(rb - 0.9, rb + 0.9)
        hgt += bead * 0.9 * np.sqrt(np.clip(1 - ((r - rb) / 0.9) ** 2, 0, 1))
        gold = np.maximum(gold, bead)
    # degree scale along the inner rim of the band
    fine_tick = (np.abs(((ang + 360.0) % 1.0) - 0.5) > 0.40)
    tick5 = (np.abs(((ang + 360.0) % 5.0) - 2.5) > 2.38)
    scale_len = np.where(tick5, 4.2, 2.4)
    scale_ticks = fine_tick & (r > R_BAND_IN + 1.8) & (r < R_BAND_IN + 1.8 + scale_len)
    hgt += 0.5 * scale_ticks * pl
    scale_ink = scale_ticks.astype(np.float32) * pl

    # dense raised glyphs: runes, zodiac, planets, stars
    glyph_mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(glyph_mask)
    slots = 48
    radius = (R_BAND_IN + R_BAND_OUT) * 0.5 + 1.8
    half = 5.4
    stroke = max(1, int(round(1.05 * sc)))
    sequence = []
    ri = zi = pi = 0
    for i in range(slots):
        k = i % 6
        if k in (0, 2, 4):
            sequence.append(RUNES[ri % len(RUNES)]); ri += 1
        elif k == 1:
            sequence.append(ZODIAC[zi % len(ZODIAC)]); zi += 1
        elif k == 3:
            sequence.append(PLANETS[pi % len(PLANETS)]); pi += 1
        else:
            sequence.append(star_glyph())
    for i in range(slots):
        a = -90.0 + 360.0 * (i + 0.5) / slots
        if abs(((a + 90.0 + 180.0) % 360.0) - 180.0) < 6.0:
            continue  # the crown ornament sits at the top
        ar = math.radians(a)
        o = (math.cos(ar), math.sin(ar))
        tg = (-math.sin(ar), math.cos(ar))
        cxg = R_IMAGE + radius * o[0]
        cyg = R_IMAGE + radius * o[1]
        for poly in sequence[i]:
            pts = []
            for (u, v) in poly:
                px = cxg + half * (u * tg[0] - v * o[0])
                py = cyg + half * (u * tg[1] - v * o[1])
                pts.append((px * sc, py * sc))
            draw.line(pts, fill=255, width=stroke, joint="curve")
            for p in (pts[0], pts[-1]):
                rr = stroke / 2
                draw.ellipse([p[0] - rr, p[1] - rr, p[0] + rr, p[1] + rr], fill=255)
    gm = np.asarray(glyph_mask, np.float32) / 255.0
    gm_soft = blur(gm, 0.35 * sc)
    hgt += 1.1 * gm_soft * pl
    glyph = np.clip(gm * 1.3, 0, 1) * pl  # black ink relief on the light band

    # brushing: concentric micro-grooves (anisotropic), plus aged surface
    rr_lines = np.interp(r * 22.0, np.arange(0, R_IMAGE * 22.0 + 2), rng.random(int(R_IMAGE * 22.0) + 2)).astype(np.float32)
    rr_lines2 = np.interp(r * 5.0, np.arange(0, R_IMAGE * 5.0 + 2), rng.random(int(R_IMAGE * 5.0) + 2)).astype(np.float32)
    brush = 0.07 * (rr_lines - 0.5) + 0.05 * (rr_lines2 - 0.5)
    hgt += brush * (gold + plate * 0.6)
    age = fbm(size, size, 18, 5, rng)
    pits = smoothstep(0.80, 0.95, fbm(size, size, 120, 2, rng))

    n = normals_from_height(hgt, 1.0 / sc)
    diff, spec = shade(n, 55.0)
    _, spec_wide = shade(n, 8.0)
    ao = np.clip(1.0 - 1.6 * np.clip(blur(hgt, 1.2 * sc) - hgt, 0, None), 0.35, 1.0)

    # gunmetal / dark steel
    metal_rgb = ramp(diff * (0.85 + 0.2 * age), [(0.0, hex_rgb("#070708")), (0.35, hex_rgb("#151618")),
                                                 (0.6, hex_rgb("#2c2d31")), (0.8, hex_rgb("#4f5157")),
                                                 (0.93, hex_rgb("#80838a")), (1.0, hex_rgb("#c4c7cc"))])
    metal_rgb += np.array([0.95, 0.97, 1.0]) * (0.55 * spec + 0.06 * spec_wide)[..., None]
    metal_rgb *= (ao * (1.0 - 0.3 * pits))[..., None]
    metal_rgb *= (0.9 + 0.1 * age)[..., None]

    # light enamelled band, slightly aged, with black raised glyphs and degree scale
    band_rgb = np.array([0.90, 0.895, 0.885]) * (0.82 + 0.2 * diff)[..., None]
    band_rgb += np.array([1.0, 1.0, 1.0]) * (0.12 * spec)[..., None]
    band_rgb *= (0.94 + 0.06 * age)[..., None] * ao[..., None]
    ink_rgb = np.array([0.07, 0.07, 0.075]) + np.array([0.35, 0.35, 0.37]) * (0.5 * spec)[..., None] \
        + np.array([0.10, 0.10, 0.11]) * diff[..., None]
    dark = np.clip(np.maximum(glyph, scale_ink), 0, 1)[..., None]
    band_rgb = band_rgb * (1 - dark) + ink_rgb * dark

    g = np.clip(gold, 0, 1)[..., None]
    rgb = band_rgb * (1 - g) + metal_rgb * g
    alpha = np.clip(np.maximum(gold, plate), 0, 1)

    # soft drop shadow under the metal (offset down-right)
    sh = blur(alpha, 2.5 * sc)
    off = int(round(2.5 * sc))
    sh = np.pad(sh, ((off, 0), (off, 0)))[:size, :size]
    sh_a = 0.35 * sh * (1 - alpha)
    out_a = alpha + sh_a
    rgb = (rgb * alpha[..., None]) / np.maximum(out_a, 1e-6)[..., None]
    save_rgba(os.path.join(OUT, "astrolabe_ring.png"), rgb, out_a, SUPERSAMPLE)


# --------------------------------------------------------------------------------------
# 3. Knob: bezel, faceted gems, gloss
KNOB_HALF = 52.0       # image half-size (canvas units)
GEM_R = 38.0
BEZEL_IN, BEZEL_OUT = 38.0, 47.0


def knob_grid():
    sc = SCALE_KNOB * SUPERSAMPLE
    size = int(2 * KNOB_HALF * sc)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    dx = (xx + 0.5) / sc - KNOB_HALF
    dy = (yy + 0.5) / sc - KNOB_HALF
    return sc, size, dx, dy, np.sqrt(dx * dx + dy * dy), np.degrees(np.arctan2(dy, dx))


def make_bezel():
    sc, size, dx, dy, r, ang = knob_grid()
    rng = np.random.default_rng(42)
    aa = 1.0 / sc
    body = smoothstep(BEZEL_IN - aa, BEZEL_IN + aa, r) * (1 - smoothstep(BEZEL_OUT - aa, BEZEL_OUT + aa, r))
    t = np.clip((r - BEZEL_IN) / (BEZEL_OUT - BEZEL_IN), 0, 1)
    hgt = 2.4 * np.clip(np.sin(np.pi * np.clip(t * 1.1, 0, 1)), 0, 1) ** 0.5
    # polished inner lip and knurled outer band
    knurl = (np.cos(np.radians(ang) * 90.0) * 0.5 + 0.5) ** 2
    hgt -= 0.6 * knurl * smoothstep(0.45, 0.55, t)
    hgt -= 0.5 * np.exp(-((r - (BEZEL_IN + 2.6)) / 0.4) ** 2)
    brush = np.interp(r * 30.0, np.arange(0, KNOB_HALF * 30 + 2), rng.random(int(KNOB_HALF * 30) + 2)).astype(np.float32)
    hgt += 0.05 * (brush - 0.5)
    hgt *= body
    n = normals_from_height(hgt, 1.0 / sc)
    diff, spec = shade(n, 60.0)
    rgb = ramp(diff, [(0.0, hex_rgb("#060607")), (0.4, hex_rgb("#17181b")), (0.7, hex_rgb("#33353a")),
                      (0.9, hex_rgb("#6c6f76")), (1.0, hex_rgb("#b9bcc2"))])
    rgb += np.array([0.95, 0.97, 1.0]) * (0.6 * spec)[..., None]
    alpha = body
    sh = blur(alpha, 2.0 * sc)
    off = int(round(2.0 * sc))
    sh = np.pad(sh, ((off, 0), (off, 0)))[:size, :size]
    # shadow also falls inside, onto the gem edge
    sh_a = 0.45 * sh * (1 - alpha)
    out_a = alpha + sh_a
    rgb = (rgb * alpha[..., None]) / np.maximum(out_a, 1e-6)[..., None]
    save_rgba(os.path.join(OUT, "knob_bezel.png"), rgb, out_a, SUPERSAMPLE)


GEM_FRAMES = 16          # filmstrip frames over one 45-degree symmetry period of the cut
GEM_PERIOD = 45.0


def gem_frame(palette, rotation, sc, size, dx, dy, r, ang):
    """One frame of the faceted cabochon, the stone turned by `rotation` degrees under a fixed light."""
    R = GEM_R + 0.6
    aa = 1.0 / sc
    inside = 1 - smoothstep(R - aa, R + aa, r)
    rn = r / R
    a = (ang - rotation + 720.0) % 360.0  # angle in the stone's own frame

    # facet layout (brilliant-like, 8-fold symmetric): octagonal table, 16 star/kite
    # facets, 16 offset girdle facets
    sector8 = (a % 45.0) - 22.5
    table_edge = 0.36 / np.cos(np.radians(sector8))
    is_table = rn < table_edge
    is_ring_a = (~is_table) & (rn < 0.70)
    is_ring_b = rn >= 0.70
    parity_a = np.floor(a / 22.5) % 2
    parity_b = np.floor((a + 11.25) / 22.5) % 2
    az_a = (np.floor(a / 22.5) + 0.5) * 22.5
    az_b = (np.floor((a + 11.25) / 22.5) + 0.5) * 22.5 - 11.25
    az = np.where(is_ring_a, az_a, az_b) + rotation  # facet azimuth in the world frame
    tilt = np.where(is_table, 0.0, np.where(is_ring_a, 24.0 + 6.0 * parity_a, 50.0 - 4.0 * parity_b))
    tr, ar = np.radians(tilt), np.radians(az)
    n = np.stack([np.sin(tr) * np.cos(ar), np.sin(tr) * np.sin(ar), np.cos(tr)], axis=-1)

    # facet edges (distance to the nearest facet boundary, canvas units)
    def to_boundary(phase):  # angular distance (degrees) to the nearest multiple of 22.5
        m = phase % 22.5
        return np.minimum(m, 22.5 - m)
    d_ring = np.minimum(np.abs(rn - 0.70) * R, np.abs(rn - table_edge) * R)
    d_sector = np.where(is_ring_a, to_boundary(a), np.where(is_ring_b, to_boundary(a - 11.25), 99.0))
    d_sector = np.where(is_table, 99.0, d_sector * np.pi / 180.0 * r)
    d_table = np.where(is_table, np.abs(table_edge - rn) * R, 99.0)
    dist = np.minimum.reduce([d_ring, d_sector, d_table])
    edge = np.exp(-(dist / 0.32) ** 2)

    # fake refraction: facet families see different depths of the stone; light leaves
    # opposite the source (bottom-right glow); facets facing the light flash
    facet_tone = np.where(is_table, 0.35, np.where(is_ring_a, np.where(parity_a > 0, 0.75, 0.25),
                                                    np.where(parity_b > 0, 0.55, 0.15)))
    back = np.clip(-(n[..., 0] * LIGHT[0] + n[..., 1] * LIGHT[1]) * 1.4 + 0.2, 0, 1)
    toward = np.clip(-(dx * LIGHT[0] + dy * LIGHT[1]) / R, -1, 1)
    base = 0.06 + 0.62 * facet_tone ** 1.7
    glow = 0.50 * back * rn + 0.28 * np.clip(toward, 0, 1)
    within = 0.20 * np.sin(np.radians(a - (az - rotation)) * 7.0 + rn * 10.0 + facet_tone * 6.0)
    table_pat = np.where(is_table, 0.28 * np.cos(np.radians(a) * 8.0) * smoothstep(0.05, 0.36, rn), 0.0)
    flash = np.exp(-(((n[..., 0] - 0.75 * LIGHT[0]) ** 2 + (n[..., 1] - 0.75 * LIGHT[1]) ** 2) / 0.012))
    internal = base + glow + within * (1 - is_table) + table_pat + 0.55 * flash
    internal += 0.30 * np.exp(-((dx - 10) ** 2 + (dy - 12) ** 2) / (2 * 8.0 ** 2))  # caustic pool (light-fixed)
    diff, spec = shade(n, 70.0)
    lum = np.clip(internal * (0.78 + 0.3 * diff), 0, 1.2)
    rgb = ramp(lum / 1.2, palette)
    edge_light = 0.35 + 0.65 * np.clip(diff, 0, 1)
    rgb += np.array(EDGE_TINT) * (0.55 * edge * edge_light)[..., None]
    rgb += np.array([1.0, 0.97, 0.97]) * (0.9 * spec)[..., None]
    rgb *= (1.0 - 0.55 * smoothstep(0.86, 1.0, rn))[..., None]  # fresnel darkening at the girdle
    return rgb, inside


def make_gem(name, palette):
    """Vertical filmstrip: frame k = stone turned by k * 45 / GEM_FRAMES degrees."""
    sc, size, dx, dy, r, ang = knob_grid()
    frames = []
    for k in range(GEM_FRAMES):
        rgb, alpha = gem_frame(palette, k * GEM_PERIOD / GEM_FRAMES, sc, size, dx, dy, r, ang)
        img = np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], axis=-1)
        im = Image.fromarray((img * 255.0 + 0.5).astype(np.uint8), "RGBA")
        frames.append(im.resize((size // SUPERSAMPLE, size // SUPERSAMPLE), Image.LANCZOS))
    w, h = frames[0].size
    strip = Image.new("RGBA", (w, h * GEM_FRAMES))
    for k, f in enumerate(frames):
        strip.paste(f, (0, k * h))
    path = os.path.join(OUT, name)
    strip.save(path, optimize=True)
    print(f"  {name:22s} {strip.width}x{strip.height} ({GEM_FRAMES} frames)  {os.path.getsize(path) / 1024:.0f} KiB")


def make_cap(name):
    """Glossy black steel dome with fine concentric turning rings (HARMONICS), one frame."""
    sc, size, dx, dy, r, ang = knob_grid()
    rng = np.random.default_rng(77)
    R = GEM_R + 0.6
    aa = 1.0 / sc
    inside = 1 - smoothstep(R - aa, R + aa, r)
    rn = np.clip(r / R, 0, 1)
    hgt = 7.0 * np.sqrt(np.clip(1 - rn ** 2, 0, 1)) ** 0.5
    rings = np.interp(r * 6.0, np.arange(0, KNOB_HALF * 6 + 2), rng.random(int(KNOB_HALF * 6) + 2)).astype(np.float32)
    hgt += 0.06 * (rings - 0.5) + 0.25 * np.exp(-((rn - 0.84) / 0.025) ** 2)  # turned ring near the edge
    n = normals_from_height(hgt * inside, 1.0 / sc)
    diff, spec = shade(n, 90.0)
    _, wide = shade(n, 10.0)
    rgb = ramp(diff * 0.9, [(0.0, hex_rgb("#030304")), (0.5, hex_rgb("#141518")), (0.8, hex_rgb("#34363b")),
                           (1.0, hex_rgb("#6d7077"))])
    rgb += np.array([0.95, 0.97, 1.0]) * (0.8 * spec + 0.10 * wide)[..., None]
    rgb *= (1.0 - 0.5 * smoothstep(0.9, 1.0, rn))[..., None]
    save_rgba(os.path.join(OUT, name), rgb, inside, SUPERSAMPLE)


def make_gloss():
    sc, size, dx, dy, r, ang = knob_grid()
    R = GEM_R
    rn = r / R
    inside = 1 - smoothstep(R - 0.5, R + 0.5, r)
    # window reflection, bent by the dome: a soft curved pane fading downward
    u = dx / R
    v = dy / R
    wx = (u + 0.22) / 0.46
    wy = (v + 0.50 + 0.55 * (u + 0.22) ** 2) / 0.22
    pane = (1 - smoothstep(0.55, 1.0, np.abs(wx))) * (1 - smoothstep(0.45, 1.0, np.abs(wy)))
    window = 0.42 * pane * np.clip(0.85 - 0.35 * wy, 0, 1) * (1 - smoothstep(0.6, 0.95, rn))
    window = blur(window, 0.6 * sc)
    hot = np.exp(-(((u + 0.40) / 0.06) ** 2 + ((v + 0.50) / 0.045) ** 2))
    hot2 = 0.5 * np.exp(-(((u - 0.52) / 0.035) ** 2 + ((v - 0.42) / 0.035) ** 2))
    rim = smoothstep(0.80, 0.98, rn) * np.clip(0.5 + 0.5 * (u * 0.55 + v * 0.65) / np.maximum(rn, 1e-3), 0, 1)
    dark_rim = smoothstep(0.88, 1.0, rn) * (1 - rim)
    rgb = np.ones((size, size, 3), np.float32)
    a_white = np.clip(window + hot * 0.95 + hot2, 0, 1)
    a_rim = np.clip(rim * 0.45, 0, 1)
    a_dark = np.clip(dark_rim * 0.45, 0, 1)
    # composite white highlights, pink rim light and dark fresnel into one premult-free RGBA
    alpha = 1 - (1 - a_white) * (1 - a_rim) * (1 - a_dark)
    col = (np.array([1, 1, 1]) * a_white[..., None] + np.array([0.85, 0.86, 0.9]) * a_rim[..., None]
           + np.array([0.05, 0.0, 0.01]) * a_dark[..., None]) / np.maximum(a_white + a_rim + a_dark, 1e-6)[..., None]
    rgb = col
    save_rgba(os.path.join(OUT, "knob_gloss.png"), rgb, alpha * inside, SUPERSAMPLE)


# --------------------------------------------------------------------------------------
# 4. Plasma annulus for the Orbit visualizer
def make_plasma(name, seed):
    s = SCALE_PLASMA
    half = 160.0
    size = int(2 * half * s)
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    dx = (xx + 0.5) / s - half
    dy = (yy + 0.5) / s - half
    r = np.sqrt(dx * dx + dy * dy)
    wx = (fbm(size, size, 6, 4, rng) - 0.5) * 70 * s
    wy = (fbm(size, size, 6, 4, rng) - 0.5) * 70 * s
    wisps = sample(fbm(size, size, 7, 6, rng), xx + wx, yy + wy)
    ridge = (1 - np.abs(2 * sample(fbm(size, size, 12, 4, rng), xx + 1.5 * wx, yy + 1.5 * wy) - 1)) ** 6
    ann = smoothstep(70, 92, r) * (1 - smoothstep(140, 158, r))
    dens = np.clip(smoothstep(0.35, 0.8, wisps) * 0.8 + ridge * 0.7, 0, 1) * ann
    rgb = ramp(dens, PLASMA_COLORS[seed % 2])
    alpha = np.clip(dens * 0.75 + blur(dens, 6 * s) * 0.35, 0, 1)
    save_rgba(os.path.join(OUT, name), rgb, alpha)


SMOKY = [(0.0, hex_rgb("#050505")), (0.25, hex_rgb("#17151a")), (0.5, hex_rgb("#352f35")),
         (0.72, hex_rgb("#62585e")), (0.88, hex_rgb("#a39aa0")), (1.0, hex_rgb("#f2eef0"))]
EDGE_TINT = [0.95, 0.92, 0.95]
PLASMA_COLORS = [
    [(0.0, hex_rgb("#a4a4a8")), (0.5, hex_rgb("#5a5a5e")), (1.0, hex_rgb("#1c1c1f"))],  # ink smoke
    [(0.0, hex_rgb("#d9a0a4")), (0.5, hex_rgb("#c4303c")), (1.0, hex_rgb("#7a0a14"))],  # red mist
]

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("SYMBIOSIS textures ->", os.path.abspath(OUT))
    make_background()
    make_ring()
    make_bezel()
    make_gem("knob_gem_smoke.png", SMOKY)
    make_cap("knob_cap.png")
    make_gloss()
    make_plasma("plasma_a.png", 808)
    make_plasma("plasma_b.png", 1607)
