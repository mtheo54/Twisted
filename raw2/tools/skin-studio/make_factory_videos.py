#!/usr/bin/env python3
"""Vidéos des skins d'usine : taches de lumière qui bougent en boucle parfaite (10 s, 24 i/s).

    python3 tools/skin-studio/make_factory_videos.py resources/factory/Skins
"""
import os, subprocess, sys
import numpy as np

W, H, FPS, DUR = 640, 480, 24, 10


def hexc(s):
    s = s.lstrip("#")
    return np.array([int(s[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32) / 255.0


def render(path, base, blobs, seed):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    params = [(hexc(c), rng.uniform(0, 6.28, 2), rng.integers(1, 3, 2), rng.uniform(0.28, 0.42), r, a)
              for c, r, a in blobs]
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
           "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-profile:v", "main", "-crf", "24",
           "-preset", "slow", "-movflags", "+faststart", "-an", path]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    bg = hexc(base)
    for f in range(FPS * DUR):
        ph = 2 * np.pi * f / (FPS * DUR)  # périodique : la dernière image rejoint la première
        img = np.empty((H, W, 3), np.float32)
        img[:] = bg
        for col, off, fr, amp, rad, alpha in params:
            cx = W * (0.5 + amp * np.sin(fr[0] * ph + off[0]))
            cy = H * (0.5 + amp * np.cos(fr[1] * ph + off[1]))
            d2 = ((xx - cx) ** 2 + (yy - cy) ** 2) / (rad * W) ** 2
            img += alpha * np.exp(-d2 * 2.2)[..., None] * col
        img = 1 - np.exp(-img * 1.4)  # écrêtage doux
        noise = np.random.default_rng(f).normal(0, 0.012, (H, W, 1)).astype(np.float32)  # grain léger (évite les bandes)
        p.stdin.write((np.clip(img + noise, 0, 1) * 255).astype(np.uint8).tobytes())
    p.stdin.close()
    p.wait()


out = sys.argv[1]
render(os.path.join(out, "VERRE NOIR", "fond.mp4"), "#050507",
       [("#FF4D00", 0.32, 0.9), ("#4B1E8C", 0.45, 0.8), ("#FF7A2E", 0.18, 0.6), ("#1C0A3A", 0.5, 0.7), ("#FF4D00", 0.22, 0.5)], 11)
render(os.path.join(out, "ONIRIQUE", "fond.mp4"), "#0E0418",
       [("#FF5FA2", 0.30, 0.85), ("#7A2BD9", 0.45, 0.9), ("#C86BFF", 0.22, 0.7), ("#2B0B5F", 0.5, 0.8), ("#FF8AC2", 0.16, 0.5)], 3)
