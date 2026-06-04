#!/usr/bin/env python3
# Конвертер PNG -> C-заголовок с тайлами в RGB565 (декодер PNG на stdlib, без PIL).
#
# Лист на тайлы:
#   python3 png2tiles.py <in.png> <tileW> <tileH> <outPx> <NAME> <out.h>
# Несколько PNG -> один массив (каждый файл = тайл, масштабируется в outPx):
#   python3 png2tiles.py --multi <out.h> <NAME> <outPx> <f1.png> <f2.png> ...
#
# Прозрачные пиксели (alpha<128) -> ключ 0xF81F.
import sys, zlib, struct

def decode_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', "not a PNG"
    pos, w, h, ct = 8, 0, 0, 0
    idat = b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        chunk = d[pos+8:pos+8+ln]
        pos += 12 + ln
        if typ == b'IHDR':
            w, h, bitd, ct = struct.unpack('>IIBB', chunk[:10])
            assert bitd == 8, "only 8-bit"
        elif typ == b'IDAT':
            idat += chunk
        elif typ == b'IEND':
            break
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    stride = w * ch
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i+stride]); i += stride
        if f == 1:
            for x in range(ch, stride): line[x] = (line[x] + line[x-ch]) & 255
        elif f == 2:
            for x in range(stride): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x-ch] if x >= ch else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x-ch] if x >= ch else 0
                b = prev[x]; c = prev[x-ch] if x >= ch else 0
                p = a + b - c; pa = abs(p-a); pb = abs(p-b); pc = abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out += line; prev = line
    return w, h, ch, bytes(out)

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def build_tile(px, w, ch, x0, y0, tw, th, op):
    def get(x, y):
        o = (y * w + x) * ch
        if ch == 4: return px[o], px[o+1], px[o+2], px[o+3]
        if ch == 3: return px[o], px[o+1], px[o+2], 255
        v = px[o]; return v, v, v, (255 if ch == 1 else px[o+1])
    buf = []
    for yy in range(op):
        sy = y0 + yy * th // op
        for xx in range(op):
            sx = x0 + xx * tw // op
            r, g, b, a = get(sx, sy)
            buf.append(0xF81F if a < 128 else rgb565(r, g, b))
    return buf

def write_header(outh, name, op, tiles):
    n = len(tiles)
    with open(outh, 'w') as f:
        f.write("#pragma once\n#include <stdint.h>\n")
        f.write(f"#define {name}_N {n}\n#define {name}_PX {op}\n")
        f.write(f"static const uint16_t {name}[{n}][{op*op}] = {{\n")
        for t in tiles:
            f.write("  {" + ",".join("0x%04X" % v for v in t) + "},\n")
        f.write("};\n")

def main():
    if sys.argv[1] == '--multi':
        outh, name, op, files = sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5:]
        tiles = []
        for fp in files:
            w, h, ch, px = decode_png(fp)
            tiles.append(build_tile(px, w, ch, 0, 0, w, h, op))
        write_header(outh, name, op, tiles)
        print(f"{len(files)} files -> {len(tiles)} tiles {op}x{op} -> {outh}")
        return
    inp, tw, th, op, name, outh = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6]
    w, h, ch, px = decode_png(inp)
    cols, rows = w // tw, h // th
    tiles = []
    for ty in range(rows):
        for tx in range(cols):
            tiles.append(build_tile(px, w, ch, tx*tw, ty*th, tw, th, op))
    write_header(outh, name, op, tiles)
    print(f"{inp}: {w}x{h} -> {len(tiles)} tiles {op}x{op} -> {outh}")

if __name__ == '__main__':
    main()
