"""Extract GRAPH.DAT assets to PNG/WAV.  usage: python3 extract_assets.py GRAPH.DAT [outdir]  (needs Pillow)"""
import struct, sys, os
from PIL import Image
d = open(sys.argv[1], 'rb').read()
OUT = sys.argv[2] if len(sys.argv) > 2 else 'out'
p = 0
def rd(n):
    global p
    r = d[p:p + n]; p += n; return r
def u32():
    return struct.unpack('<I', rd(4))[0]
pal0 = rd(0x300)
sprsize = u32() ^ 0xf5d43dd7
print('sprsize', sprsize)
tabs = []
for t in range(4):
    tabs.append(list(struct.unpack('<4096H', rd(0x2000))))
for i in range(4096):
    k = (0x37ea - 2 * i) & 0xffff
    for t in range(4): tabs[t][i] ^= k
offs = [(v ^ ((i + 0xCC96C330) & 0xffffffff)) for i, v in enumerate(struct.unpack('<256I', rd(0x400)))]
print('offs', [hex(x) for x in offs[:30]])
ns = u32(); print('nsamples', ns)
samples = []
for i in range(ns):
    n = u32(); samples.append(rd(n)); print(' sample', i, n)
fsz = u32(); print('fontsize', fsz)
foffs = [v ^ 0x41fbe3 for v in struct.unpack('<768I', rd(0xc00))]
font = bytes(0xc00) + rd(fsz)
pal = bytearray(rd(0x300))
for i in range(0x300): pal[i] ^= (i * 3 + i + 0x84) & 0xff
bg = bytearray(64000); o = 0
while o < 64000:
    k = (o * 3 + 0x9d) & 0xff
    b = rd(1)[0] ^ k
    if b & 0xc0 == 0xc0:
        n = b & 0x3f; v = rd(1)[0] ^ k
        bg[o:o + n] = bytes([v]) * n; o += n
    else:
        bg[o] = b; o += 1
spr = bytearray(rd(sprsize))
for i in range(sprsize): spr[i] ^= (i * 6 + 0x9a) & 0xff
print('end', p, len(d))
os.makedirs(OUT, exist_ok=True)
P = [min(255, c * 4 + c // 16) for c in pal]
def img(buf, w, h):
    im = Image.frombytes('P', (w, h), bytes(buf)); im.putpalette(P); return im
img(bg, 320, 200).resize((640, 400)).save(OUT + '/bg.png')
print('pal0 == pal', pal0 == bytes(pal))
# sprites
for s in range(29):
    b = offs[s]
    w, h = struct.unpack_from('<HH', spr, b)
    rows = struct.unpack_from('<128I', spr, b + 4)
    canvas = bytearray([255]) * (w * h)
    q = b + 0x204; y = 0; x = 0
    while y < h:
        c = spr[q]; q += 1
        if c == 0xff: x += spr[q]; q += 1
        elif c == 0xfe: y += 1; x = 0
        else:
            for j in range(c):
                if x < w: canvas[y * w + x] = spr[q + j]
                x += 1
            q += c
    print('spr', s, w, h, 'rows0..3', [r for r in rows[:3]], [r for r in rows[h-3:h]], rows[127], 'data end', q - b)
    img(canvas, w, h).resize((w * 3, h * 3), Image.NEAREST).save(OUT + '/spr%02d.png' % s)
# font
for fs in range(3):
    size = [32, 16, 8][fs]
    sheet = bytearray(size * 16 * size * 16)
    for ch in range(256):
        q = foffs[fs * 256 + ch]
        if q >= len(font) or q < 0xc00: continue
        x = 0; y = 0; cx = (ch % 16) * size; cy = (ch // 16) * size
        guard = 0
        while y < size and guard < 5000:
            guard += 1
            b = font[q]; q += 1
            n = b >> 1
            if b & 1:
                for j in range(n):
                    if x < size: sheet[(cy + y) * size * 16 + cx + x] = 15
                    x += 1
            elif n: x += n
            else: y += 1; x = 0
    img(sheet, size * 16, size * 16).save(OUT + '/font%d.png' % size)
for i, smp in enumerate(samples):
    import wave
    w = wave.open(OUT + '/s%02d.wav' % i, 'wb'); w.setnchannels(1); w.setsampwidth(1); w.setframerate(8000); w.writeframes(bytes((b ^ 0x80) for b in smp)); w.close()
print('tab0', tabs[0][:8], tabs[0][2048:2056]); print('tab1', tabs[1][:4], tabs[1][2048], tabs[1][4095]); print('tab2', [t - 65536 if t > 32767 else t for t in tabs[2][::512]]); print('tab3', tabs[3][::256])
