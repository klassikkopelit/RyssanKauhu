"""Load a DOS/4GW LE executable (RYSS.EXE), apply fixups and write a flat image.
usage: python3 le_unpack.py RYSS.EXE image.bin   (objects are placed at their link base, image starts at the lowest one)
"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
le = struct.unpack_from('<I', d, 0x3c)[0]
def u32(o): return struct.unpack_from('<I', d, o)[0]
def u16(o): return struct.unpack_from('<H', d, o)[0]
H = lambda o: u32(le + o)
pagesize = H(0x28); lastpage = H(0x2c); npages = H(0x14)
objtab = le + H(0x40); nobj = H(0x44); pagemap = le + H(0x48)
fixpage = le + H(0x68); fixrec = le + H(0x6c); datapages = H(0x80)
objs = []
for i in range(nobj):
    o = objtab + i * 24
    vsize, base, flags, pmi, pmn, _ = struct.unpack_from('<6I', d, o)
    objs.append(dict(vsize=vsize, base=base, flags=flags, pmi=pmi, pmn=pmn))
    print('obj', i + 1, hex(base), hex(vsize), hex(flags), pmi, pmn)
# build image
lo = min(o['base'] for o in objs); hi = max(o['base'] + o['vsize'] for o in objs)
img = bytearray(hi - lo)
def page_off(p):  # p is 1-based
    return datapages + (p - 1) * pagesize
for o in objs:
    for k in range(o['pmn']):
        p = o['pmi'] + k
        e = pagemap + (p - 1) * 4
        # LE page map: 3-byte page number (big-endian-ish) + flags
        pn = (d[e] << 16) | (d[e + 1] << 8) | d[e + 2]
        sz = lastpage if p == npages else pagesize
        src = d[page_off(pn):page_off(pn) + sz]
        a = o['base'] - lo + k * pagesize
        img[a:a + len(src)] = src
# fixups
cnt = 0
for o in objs:
    for k in range(o['pmn']):
        p = o['pmi'] + k
        s = fixrec + u32(fixpage + (p - 1) * 4); e = fixrec + u32(fixpage + p * 4)
        pagebase = o['base'] + k * pagesize
        while s < e:
            src = d[s]; flg = d[s + 1]; s += 2
            if src & 0x20:
                n = d[s]; s += 1; offs = []
            else:
                offs = [struct.unpack_from('<h', d, s)[0]]; s += 2; n = None
            if flg & 3 != 0: raise Exception('non-internal fixup %x' % flg)
            if flg & 0x40: tobj = u16(s); s += 2
            else: tobj = d[s]; s += 1
            stype = src & 0xf
            if stype == 2:  # 16-bit selector
                toff = 0
            elif flg & 0x10: toff = u32(s); s += 4
            else: toff = u16(s); s += 2
            if n is not None:
                offs = [struct.unpack_from('<h', d, s + 2 * j)[0] for j in range(n)]; s += 2 * n
            tgt = objs[tobj - 1]['base'] + toff
            for so in offs:
                a = pagebase + so - lo
                if stype == 7:
                    if 0 <= a and a + 4 <= len(img): struct.pack_into('<I', img, a, tgt)
                elif stype == 8:
                    if 0 <= a and a + 4 <= len(img): struct.pack_into('<I', img, a, (tgt - (pagebase + so + 4)) & 0xffffffff)
                elif stype == 2:
                    pass
                else:
                    print('fixup type', stype)
                cnt += 1
print('fixups', cnt, 'lo', hex(lo))
open(sys.argv[2], 'wb').write(img)
