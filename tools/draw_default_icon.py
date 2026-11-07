#!/usr/bin/env python3
# Draws the repository's original icon (platform/icon_default.png): balloons over funfair sun rays, no game art.
# Usage: python3 tools/draw_default_icon.py platform/icon_default.png   (needs Pillow)
import math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops
S = 1024
def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))
# background: vertical gradient
bg = Image.new('RGBA', (S, S))
top, bot = (52, 18, 120), (230, 40, 120)
px = bg.load()
for y in range(S):
    c = lerp(top, bot, y / S) + (255,)
    for x in range(S): px[x, y] = c
# sun rays from a point low in the picture
rays = Image.new('L', (S, S), 0); d = ImageDraw.Draw(rays)
cx, cy = S * 0.5, S * 0.86
for k in range(24):
    a0 = k * 2 * math.pi / 24; a1 = a0 + math.pi / 24
    d.polygon([(cx, cy), (cx + 2000 * math.cos(a0), cy + 2000 * math.sin(a0)), (cx + 2000 * math.cos(a1), cy + 2000 * math.sin(a1))], fill=48)
bg = Image.composite(Image.new('RGBA', (S, S), (255, 214, 90, 255)), bg, rays)
# glow behind
glow = Image.new('L', (S, S), 0); ImageDraw.Draw(glow).ellipse((cx - 330, cy - 330, cx + 330, cy + 330), fill=150)
glow = glow.filter(ImageFilter.GaussianBlur(90))
bg = Image.composite(Image.new('RGBA', (S, S), (255, 230, 140, 255)), bg, glow)
d = ImageDraw.Draw(bg)
# balloons: (centre x, centre y, radius, colour)
balloons = [(330, 400, 175, (255, 70, 60)), (640, 330, 190, (255, 205, 40)), (500, 560, 165, (60, 200, 110))]
knot_y = 905
strings = Image.new('RGBA', (S, S), (0, 0, 0, 0)); ds = ImageDraw.Draw(strings)
for bx, by, r, c in balloons:
    # wavy string down to the common knot
    pts = []
    x0, y0 = bx, by + r * 1.18; x1, y1 = 512, knot_y
    for i in range(41):
        t = i / 40; x = x0 + (x1 - x0) * t + 18 * math.sin(t * 3 * math.pi); y = y0 + (y1 - y0) * t; pts.append((x, y))
    ds.line(pts, fill=(255, 255, 255, 230), width=9, joint='curve')
bg.alpha_composite(strings)
for bx, by, r, c in balloons:
    lay = Image.new('RGBA', (S, S), (0, 0, 0, 0)); dl = ImageDraw.Draw(lay)
    # outline, body (slightly taller than wide), knot
    dl.ellipse((bx - r - 14, by - r * 1.12 - 14, bx + r + 14, by + r * 1.12 + 14), fill=(30, 10, 60, 255))
    dl.polygon([(bx - 34, by + r * 1.12 + 40), (bx + 34, by + r * 1.12 + 40), (bx, by + r * 1.12 - 6)], fill=(30, 10, 60, 255))
    dl.ellipse((bx - r, by - r * 1.12, bx + r, by + r * 1.12), fill=c + (255,))
    dl.polygon([(bx - 22, by + r * 1.12 + 28), (bx + 22, by + r * 1.12 + 28), (bx, by + r * 1.12 - 8)], fill=c + (255,))
    # shading: darker lower right
    sh = Image.new('L', (S, S), 0); ImageDraw.Draw(sh).ellipse((bx - r * 0.6, by - r * 0.5, bx + r * 1.4, by + r * 1.6), fill=90)
    sh = sh.filter(ImageFilter.GaussianBlur(40))
    mask = Image.new('L', (S, S), 0); ImageDraw.Draw(mask).ellipse((bx - r, by - r * 1.12, bx + r, by + r * 1.12), fill=255)
    sh = ImageChops.multiply(sh, mask)
    dark = Image.new('RGBA', (S, S), tuple(int(v * 0.55) for v in c) + (255,))
    lay = Image.composite(dark, lay, sh)
    # highlight
    hl = Image.new('L', (S, S), 0); ImageDraw.Draw(hl).ellipse((bx - r * 0.62, by - r * 0.85, bx - r * 0.12, by - r * 0.25), fill=210)
    hl = hl.filter(ImageFilter.GaussianBlur(10))
    lay = Image.composite(Image.new('RGBA', (S, S), (255, 255, 255, 255)), lay, hl)
    bg.alpha_composite(lay)
# little stars
def star(x, y, r, col):
    pts = []
    for k in range(10):
        a = -math.pi / 2 + k * math.pi / 5; rr = r if k % 2 == 0 else r * 0.45
        pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
    d.polygon(pts, fill=col)
d = ImageDraw.Draw(bg)
for x, y, r in ((150, 170, 46), (880, 160, 38), (860, 640, 30), (130, 690, 26), (760, 90, 20)):
    star(x, y, r + 8, (30, 10, 60, 255)); star(x, y, r, (255, 240, 120, 255))
# rounded square mask
m = Image.new('L', (S, S), 0); ImageDraw.Draw(m).rounded_rectangle((0, 0, S - 1, S - 1), radius=200, fill=255)
out = Image.new('RGBA', (S, S), (0, 0, 0, 0)); out.paste(bg, (0, 0), m)
# border
b = Image.new('RGBA', (S, S), (0, 0, 0, 0)); ImageDraw.Draw(b).rounded_rectangle((10, 10, S - 11, S - 11), radius=192, outline=(255, 255, 255, 200), width=16)
out.alpha_composite(b)
out.resize((512, 512), Image.LANCZOS).save(sys.argv[1])
