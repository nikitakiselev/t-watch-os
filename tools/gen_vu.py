#!/usr/bin/env python3
# Конвертация подложки VU-метра: watchos/data/img/vu-meter.png (240×130 RGB)
#   -> watchos/src/programs/vu_gen.h  (VU_BG[VU_W*VU_H] RGB565, ряд за рядом).
# Картинка непрозрачная (фон-подложка), ключа прозрачности нет — берём пиксели как есть.
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from png2tiles import decode_png, rgb565

SRC = os.path.join(HERE, '..', 'watchos', 'data', 'img', 'vu-meter.png')
OUT = os.path.join(HERE, '..', 'watchos', 'src', 'programs', 'vu_gen.h')

w, h, ch, px = decode_png(SRC)
vals = []
for y in range(h):
    for x in range(w):
        o = (y * w + x) * ch
        if ch >= 3: r, g, b = px[o], px[o + 1], px[o + 2]
        else:       v = px[o]; r = g = b = v
        vals.append(rgb565(r, g, b))

with open(OUT, 'w') as f:
    f.write('#pragma once\n#include <stdint.h>\n')
    f.write('// Сгенерировано tools/gen_vu.py из data/img/vu-meter.png — не править руками.\n')
    f.write(f'#define VU_W {w}\n#define VU_H {h}\n')
    f.write(f'static const uint16_t VU_BG[VU_W * VU_H] = {{\n')
    for y in range(h):
        row = vals[y * w:(y + 1) * w]
        f.write('  ' + ','.join('0x%04X' % v for v in row) + ',\n')
    f.write('};\n')

print(f'VU {w}x{h} -> vu_gen.h ({os.path.getsize(OUT)//1024}КБ)')
