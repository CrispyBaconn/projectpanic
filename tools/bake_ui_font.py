#!/usr/bin/env python3
# Prepara i font del menu (platform/ui_font.h): Bangers (titoli e pulsanti) e Lilita One (testo), entrambi OFL
# (licenze in platform/fonts/). Ogni carattere ha due immagini nell'atlante: il riempimento e il contorno (il
# riempimento allargato di R pixel), cosi' il menu disegna il contorno nero e poi la lettera con il gradiente.
# Uso: python3 tools/bake_ui_font.py   (serve Pillow)
import io, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPS = list(range(32, 127)) + list(range(160, 256)) + [0x2019, 0x2026, 0x20AC, 0x2013, 0x2014, 0x201C, 0x201D, 0x0152, 0x0153]
FONTS = [("display", "Bangers-Regular.ttf", 84, 6), ("body", "LilitaOne-Regular.ttf", 46, 4)]
ATLAS_W = 2048

def bake(name, file, size, R):
    font = ImageFont.truetype(os.path.join(ROOT, "platform", "fonts", file), size)
    asc, desc = font.getmetrics()
    glyphs = []
    for cp in CPS:
        ch = chr(cp)
        if cp != 32 and font.getmask(ch).getbbox() is None: continue
        adv = font.getlength(ch)
        bb = font.getbbox(ch)                       # rispetto al punto in alto a sinistra (ascender)
        if cp == 32 or bb[2] <= bb[0]:
            glyphs.append((cp, None, None, 0, 0, adv)); continue
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        im = Image.new("L", (w + 2 * R + 2, h + 2 * R + 2), 0)
        ImageDraw.Draw(im).text((R + 1 - bb[0], R + 1 - bb[1]), ch, font=font, fill=255)
        fill = im
        ol = im.filter(ImageFilter.MaxFilter(2 * R + 1)) if R else im
        # morbidezza del bordo del contorno
        ol = ol.filter(ImageFilter.GaussianBlur(0.6))
        glyphs.append((cp, fill, ol, bb[0] - R - 1, bb[1] - R - 1, adv))
    # impacchettamento a scaffali: riempimento e contorno della stessa lettera affiancati
    x = y = rowh = 0; place = []
    for g in glyphs:
        if g[1] is None: place.append(None); continue
        w, h = g[1].size
        if x + 2 * w + 2 > ATLAS_W: x = 0; y += rowh + 1; rowh = 0
        place.append((x, y)); x += 2 * w + 2; rowh = max(rowh, h)
    H = y + rowh + 1
    atlas = Image.new("L", (ATLAS_W, H), 0)
    recs = []
    for g, p in zip(glyphs, place):
        cp, fill, ol, ox, oy, adv = g
        if p is None: recs.append((cp, 0, 0, 0, 0, 0, 0, round(adv * 4))); continue
        w, h = fill.size
        atlas.paste(fill, (p[0], p[1])); atlas.paste(ol, (p[0] + w + 1, p[1]))
        recs.append((cp, p[0], p[1], w, h, ox, oy, round(adv * 4)))
    buf = io.BytesIO(); atlas.save(buf, "PNG", optimize=True)
    return dict(name=name, png=buf.getvalue(), recs=recs, asc=asc, desc=desc, size=size, R=R, H=H)

out = ["// generato da tools/bake_ui_font.py - non modificare a mano", "#pragma once", "#include <cstdint>",
       "namespace uifont {",
       "struct Glyph { uint16_t cp, x, y, w, h; int16_t ox, oy; uint16_t adv4; };   // contorno a (x + w + 1, y); adv4 = avanzamento * 4",
       "struct Font { const uint8_t* png; unsigned png_size; const Glyph* g; int n; int asc, desc, size, R; };"]
for f in FONTS:
    d = bake(*f)
    b = d["png"]
    out.append(f"static const uint8_t {d['name']}_png[{len(b)}] = {{")
    for i in range(0, len(b), 32): out.append("".join(f"{v}," for v in b[i:i + 32]))
    out.append("};")
    out.append(f"static const Glyph {d['name']}_g[{len(d['recs'])}] = {{")
    out.append(",".join("{%d,%d,%d,%d,%d,%d,%d,%d}" % r for r in d["recs"]))
    out.append("};")
    out.append(f"static const Font {d['name']} = {{{d['name']}_png, {len(b)}, {d['name']}_g, {len(d['recs'])}, {d['asc']}, {d['desc']}, {d['size']}, {d['R']}}};")
    print(d["name"], "atlante", ATLAS_W, "x", d["H"], len(b), "byte")
out.append("}  // namespace uifont")
open(os.path.join(ROOT, "platform", "ui_font.h"), "w").write("\n".join(out) + "\n")
