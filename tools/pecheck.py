import struct
import sys

def imports(path):
    b = open(path, "rb").read()
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    szopt = struct.unpack_from("<H", b, pe + 20)[0]
    opt = pe + 24
    is64 = struct.unpack_from("<H", b, opt)[0] == 0x20B
    imp_rva = struct.unpack_from("<I", b, opt + (112 if is64 else 96) + 8)[0]
    secs = []
    for i in range(nsec):
        s = opt + szopt + i * 40
        vs, va, rs, pr = struct.unpack_from("<IIII", b, s + 8)
        secs.append((va, vs, rs, pr))

    def r2o(rva):
        for va, vs, rs, pr in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + pr
        return None

    out = []
    at = r2o(imp_rva)
    while True:
        olt, _, _, namerva, ft = struct.unpack_from("<IIIII", b, at)
        if olt == 0 and namerva == 0 and ft == 0:
            break
        off = r2o(namerva)
        out.append(b[off:b.index(b"\0", off)].decode("latin1").lower())
        at += 20
    return out

if __name__ == "__main__":
    path, required = sys.argv[1], [x.lower() for x in sys.argv[2:]]
    dlls = imports(path)
    for d in dlls:
        print(d)
    missing = [r for r in required if r not in dlls]
    if missing:
        print("missing imports: " + ", ".join(missing))
        sys.exit(1)
