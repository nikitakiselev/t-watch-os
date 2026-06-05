#!/usr/bin/env python3
# Сборка спрайтов: каждый спрайт — отдельный файл sprites/N.png (16x16, имя=индекс).
# Ключ прозрачности — салатовый (0,255,0). Скрипт собирает их по порядку в:
#   - sprites/sprites.png  (один лист 10 колонок, для просмотра)
#   - ../watchos/src/programs/dungeon/sprites_gen.h  (SPRITES[N][256] RGB565, ключ 0xF81F)
import os, sys, zlib, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from png2tiles import decode_png, rgb565

DIR    = os.path.join(HERE, '..', 'watchos', 'src', 'programs', 'dungeon', 'sprites')
OUT_H  = os.path.join(DIR, '..', 'sprites_gen.h')
OUT_PNG= os.path.join(DIR, 'sprites.png')
PX, COLS, KEY = 16, 10, 0xF81F
SHADOW = 0xF81E   # маркер полупрозрачной тени (чёрный ~35%): рендер блендит с фоном под спрайтом
NAMES = {100: 'PLAYER', 110: 'CHEST', 111: 'TRADER', 112: 'STAIRS',
         113: 'KNIGHT_IDLE',   # idle-анимация игрока: кадры 113,114 (SPR_KNIGHT_IDLE + frame)
         115: 'KNIGHT_RUN',    # бег: кадры 115..120 (SPR_KNIGHT_RUN + frame, 6 шт., рисуются «вправо»)
         121: 'GOBLIN',        # гоблин (монстр): idle 2 кадра 121/122 (общий спрайт для всех тиров)
         123: 'TORCH'}         # настенный факел: 8 кадров 123..130 (анимация пламени)
MON_BASE = 101

def greenkey(r, g, b):
    # салатовый ключ прозрачности (с допуском на лёгкую кайму)
    return g >= 180 and r <= 70 and b <= 70

def load_cell(path):
    w, h, ch, px = decode_png(path)
    out, rgb = [], []
    for y in range(PX):
        for x in range(PX):
            sx, sy = x * w // PX, y * h // PX
            o = (sy * w + sx) * ch
            if ch == 4: r, g, b, a = px[o], px[o+1], px[o+2], px[o+3]
            elif ch == 3: r, g, b, a = px[o], px[o+1], px[o+2], 255
            else: v = px[o]; r = g = b = v; a = 255 if ch == 1 else px[o+1]
            if a < 32 or greenkey(r, g, b):
                out.append(KEY); rgb.append(None)              # полностью прозрачно
            elif a < 200:
                out.append(SHADOW); rgb.append((30, 30, 30))   # тень → блендится в рендере
            else:
                out.append(rgb565(r, g, b)); rgb.append((r, g, b))  # непрозрачно
    return out, rgb

idxs = [int(f[:-4]) for f in os.listdir(DIR) if f.endswith('.png') and f[:-4].isdigit()]
N = max(idxs) + 1
cells, rgbs = [], []
for i in range(N):
    p = os.path.join(DIR, f'{i}.png')
    if os.path.exists(p): c, rg = load_cell(p)
    else: c, rg = [KEY]*(PX*PX), [None]*(PX*PX)
    cells.append(c); rgbs.append(rg)

with open(OUT_H, 'w') as f:
    f.write('#pragma once\n#include <stdint.h>\n// Сгенерировано tools/gen_sprites.py из sprites/N.png — не править руками.\n')
    f.write(f'#define SPR_N {N}\n#define SPR_PX {PX}\n')
    for idx, nm in sorted(NAMES.items()): f.write(f'#define SPR_{nm} {idx}\n')
    f.write(f'#define SPR_MON_BASE {MON_BASE}   // спрайт монстра = SPR_MON_BASE + spriteId\n')
    f.write(f'static const uint16_t SPRITES[{N}][{PX*PX}] = {{\n')
    for c in cells: f.write('  {' + ','.join('0x%04X' % v for v in c) + '},\n')
    f.write('};\n')

# собрать sprites.png (10 кол.), прозрачное = салатовый
rows = (N + COLS - 1) // COLS
W, Hh = COLS * PX, rows * PX
buf = [(0, 255, 0)] * (W * Hh)
for i in range(N):
    cr, cc = i // COLS, i % COLS
    for y in range(PX):
        for x in range(PX):
            v = rgbs[i][y*PX + x]
            if v is not None: buf[(cr*PX+y)*W + (cc*PX+x)] = v
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d) & 0xffffffff)
raw = bytearray()
for y in range(Hh):
    raw.append(0)
    for x in range(W): r, g, b = buf[y*W + x]; raw += bytes((r, g, b))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, Hh, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
open(OUT_PNG, 'wb').write(png)
print(f'{N} спрайтов -> sprites_gen.h ({os.path.getsize(OUT_H)//1024}КБ) + sprites.png ({W}x{Hh})')
