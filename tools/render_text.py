#!/usr/bin/env python3
"""Prototype of the Namco System 23 C361/C404 text layer (from namcos23.cpp: draw_text_layer, screen_update).
Usage: render_text.py <dump_folder> <out.png>"""
import struct, sys
import numpy as np
from PIL import Image

W, H = 640, 480

def load(d):
    r = lambda n: open(f'{d}/{n}', 'rb').read()
    return r('charram.bin'), r('palette.bin'), r('c404.bin'), open(f'{d}/c361regs.txt').read()

def render(d):
    charram, pal, c404, regs = load(d)
    xs = int(regs.split('xscroll=')[1].split()[0], 16); ys = int(regs.split('yscroll=')[1].split()[0], 16)
    c4 = np.frombuffer(c404, dtype='>u2').astype(np.int32)              # C404 registers/gamma (BE half-words)
    P = np.frombuffer(pal, dtype='>u2').astype(np.int32)
    pal_r, pal_g, pal_b = (P[o // 2:o // 2 + 0x8000] & 0xff for o in (0, 0x10000, 0x20000))
    # Character RAM: 0x1e000 bytes = 128-byte tiles (16 rows x 8 bytes, 4 bpp, pixel0 = high nibble)
    cr = np.frombuffer(charram[:0x1e000], dtype=np.uint8).reshape(-1, 16, 8)
    hi, lo = cr >> 4, cr & 15
    tiles = np.stack([hi, lo], axis=-1).reshape(-1, 16, 16)             # [tile][row][pixel]
    # Text VRAM: 64 columns x 64 rows of BE words at 0x1e000 in the C361 block
    tv = np.frombuffer(charram[0x1e000:0x20000], dtype='>u2').astype(np.int32).reshape(64, 64)
    palbase = (c4[0x1b] << 8) & 0x7f00
    bg = (c4[0x08] & 255, c4[0x09] & 255, c4[0x0a] & 255)               # ram[8..a] (offset 0x10/12/14 byte)
    layer_flags = c4[0x1f]
    scroll = ((xs - 0x35c) & 0x3ff)
    img = np.zeros((H, W, 3), dtype=np.int32); img[:] = bg
    if layer_flags & 4:
        ys_ = (np.arange(H) + ys) & 0x3ff
        xs_ = (np.arange(W) + scroll) & 0x3ff
        trow, ly = ys_ >> 4, ys_ & 15
        tcol, px = xs_ >> 4, xs_ & 15
        td = tv[trow[:, None], tcol[None, :]]                             # [H][W] word tile
        code = td & 0x3ff; flipx = (td >> 10) & 1; flipy = (td >> 11) & 1; psel = (td >> 12) & 15
        lyy = np.where(flipy == 1, 15 - ly[:, None], ly[:, None])
        pxx = np.where(flipx == 1, 15 - px[None, :], px[None, :])
        ok = code < tiles.shape[0]
        val = np.where(ok, tiles[np.minimum(code, tiles.shape[0] - 1), lyy, pxx], 15)
        pen = palbase | (psel << 4) | val
        vis = val != 15
        col = np.stack([pal_r[pen], pal_g[pen], pal_b[pen]], axis=-1)
        img = np.where(vis[..., None], col, img)
    # final C404 gamma (ram[0x100+r], ram[0x200+g], ram[0x300+b])
    gr, gg, gb = c4[0x100:0x200] & 255, c4[0x200:0x300] & 255, c4[0x300:0x400] & 255
    out = np.stack([gr[img[..., 0]], gg[img[..., 1]], gb[img[..., 2]]], axis=-1).astype(np.uint8)
    return out, dict(palbase=hex(palbase), layer_flags=layer_flags, scroll=scroll, ys=ys, bg=bg)

if __name__ == '__main__':
    out, info = render(sys.argv[1]); print(info)
    Image.fromarray(out).save(sys.argv[2]); print('saved', sys.argv[2], out.shape)
