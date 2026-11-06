#!/usr/bin/env python3
"""Makes the app icons for every device from one picture: platform/ui/icon.png (your own copy, not in the repository).

Usage: python3 tools/make_icons.py        (needs Pillow: pip install pillow)
Writes (all ignored by git):
  build_icons/icon.ico                        Windows executable (16..256 px)
  build_icons/android/mipmap-*/ic_launcher.png Android launcher (48..192 px)
  switch/icon.jpg                             Switch .nro (256x256 JPEG)
  wiiu/icon.png                               Wii U .wuhb (128x128)
  3ds/icon.png                                3DS .smdh (48x48)
The build scripts use them when they exist; without platform/ui/icon.png nothing changes.
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'platform', 'ui', 'icon.png')

def main():
    if not os.path.isfile(SRC):
        print('no platform/ui/icon.png: default icons'); return 0
    try:
        from PIL import Image
    except ImportError:
        print('icons: Pillow missing (pip install pillow), default icons'); return 0
    im = Image.open(SRC).convert('RGBA')
    sq = max(im.size)                                   # square canvas, picture centred
    if im.size[0] != im.size[1]:
        c = Image.new('RGBA', (sq, sq), (0, 0, 0, 0)); c.paste(im, ((sq - im.size[0]) // 2, (sq - im.size[1]) // 2)); im = c
    def sized(n): return im.resize((n, n), Image.LANCZOS)
    def out(*p):
        f = os.path.join(ROOT, *p); os.makedirs(os.path.dirname(f), exist_ok=True); return f
    sized(256).save(out('build_icons', 'icon.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    for name, n in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
        sized(n).save(out('build_icons', 'android', 'mipmap-' + name, 'ic_launcher.png'))
    bg = Image.new('RGB', (256, 256), (0, 0, 0)); s = sized(256); bg.paste(s, (0, 0), s)
    bg.save(out('switch', 'icon.jpg'), quality=92)
    sized(128).save(out('wiiu', 'icon.png'))
    sized(48).save(out('3ds', 'icon.png'))
    print('icons made from platform/ui/icon.png')
    return 0

if __name__ == '__main__':
    sys.exit(main())
