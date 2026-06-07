#!/usr/bin/env python3
# Генерация VLW-шрифта (латиница + кириллица) из TTF -> watchos/notesfont.h.
# VLW (Processing PFont) — формат, который грузит TFT_eSPI loadFont(const uint8_t[]).
# Глифы: ASCII 0x20..0x7E + кириллица U+0410..U+044F + Ё(0x401)/ё(0x451).
#
# Формат VLW (big-endian int32):
#   заголовок: gCount, version(11), fontSize, 0, ascent, descent
#   gCount × метрики: unicode, height, width, xAdvance, gdY, gdX, 0
#   затем подряд 8-битные альфа-битмапы (width*height байт на глиф) в том же порядке.
import os, struct
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, '..', 'watchos', 'notesfont.h')
TTF  = os.environ.get('NOTES_TTF', '/System/Library/Fonts/Supplemental/Arial.ttf')
SIZE = int(os.environ.get('NOTES_SIZE', '18'))
PAD  = 4   # левое поле канвы (на случай отрицательного бординга)


def codepoints():
    cps = list(range(0x20, 0x7F))            # ASCII печатные
    cps += [0x401, 0x451]                     # Ё ё
    cps += list(range(0x410, 0x450))          # А..я
    return cps


def main():
    font = ImageFont.truetype(TTF, SIZE)
    ascent, descent = font.getmetrics()       # пиксели до/ниже базовой линии
    cps = codepoints()
    H = ascent + descent + 2                   # высота канвы; базовая линия на y=ascent

    metrics = []   # (unicode, w, h, xadv, gdY, gdX)
    bitmaps = []   # bytes 8-bit alpha, len = w*h

    for cp in cps:
        ch = chr(cp)
        xadv = round(font.getlength(ch))
        W = xadv + 2 * PAD + 4
        img = Image.new('L', (W, H), 0)
        d = ImageDraw.Draw(img)
        # anchor='ls' — (x,y) = левый конец базовой линии; перо по x=PAD.
        d.text((PAD, ascent), ch, fill=255, font=font, anchor='ls')
        bbox = img.getbbox()
        if bbox is None:                       # пробел и пустые глифы
            metrics.append((cp, 0, 0, xadv, 0, 0))
            bitmaps.append(b'')
            continue
        l, t, r, b = bbox
        w, h = r - l, b - t
        gdY = ascent - t                       # от базовой вверх до верха битмапа
        gdX = l - PAD                           # левый бординг относительно пера
        metrics.append((cp, w, h, xadv, gdY, gdX))
        bitmaps.append(img.crop((l, t, r, b)).tobytes())

    out = bytearray()
    def be32(v): out.extend(struct.pack('>i', v))
    be32(len(cps)); be32(11); be32(SIZE); be32(0); be32(ascent); be32(descent)
    for (cp, w, h, xadv, gdY, gdX) in metrics:
        be32(cp); be32(h); be32(w); be32(xadv); be32(gdY); be32(gdX); be32(0)
    for bm in bitmaps:
        out.extend(bm)

    with open(OUT, 'w') as f:
        f.write('#pragma once\n')
        f.write('// Сгенерировано tools/gen_font.py — НЕ редактировать вручную.\n')
        f.write('// VLW-шрифт (латиница+кириллица) для TFT_eSPI loadFont().\n')
        f.write('#include <stdint.h>\n')
        f.write('static const uint8_t notesfont[] PROGMEM = {\n')
        for i in range(0, len(out), 16):
            row = ','.join(str(byte) for byte in out[i:i + 16])
            f.write('  ' + row + ',\n')
        f.write('};\n')
    print(f'notesfont.h: {len(cps)} glyphs, {len(out)} bytes, '
          f'size {SIZE}, ascent {ascent}, descent {descent}, TTF {TTF}')


if __name__ == '__main__':
    main()
