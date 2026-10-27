import struct
import sys

def sections(b):
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    szopt = struct.unpack_from("<H", b, pe + 20)[0]
    opt = pe + 24
    is64 = struct.unpack_from("<H", b, opt)[0] == 0x20B
    secs = []
    for i in range(nsec):
        s = opt + szopt + i * 40
        name = b[s:s + 8].split(b"\0")[0].decode("latin1")
        vs, va, rs, pr = struct.unpack_from("<IIII", b, s + 8)
        ch = struct.unpack_from("<I", b, s + 36)[0]
        secs.append((name, va, vs, rs, pr, ch))
    return opt, is64, secs

def okmod_iat(b, opt, is64, r2o):
    ddoff = opt + (112 if is64 else 96)
    thunk_sz = 8 if is64 else 4
    at = r2o(struct.unpack_from("<I", b, ddoff + 8)[0])
    while True:
        olt, _, _, namerva, ft = struct.unpack_from("<IIIII", b, at)
        if olt == 0 and namerva == 0 and ft == 0:
            return {}
        off = r2o(namerva)
        if b[off:b.index(b"\0", off)].decode("latin1").lower() == "okmod.dll":
            out, thunk, i = {}, r2o(olt or ft), 0
            while True:
                t = struct.unpack_from("<Q" if is64 else "<I", b, thunk)[0]
                if t == 0:
                    break
                no = r2o(t & (0x7FFFFFFFFFFFFFFF if is64 else 0x7FFFFFFF))
                out[b[no + 2:b.index(b"\0", no + 2)].decode("latin1")] = ft + i * thunk_sz
                thunk += thunk_sz
                i += 1
            return out
        at += 20

def tail_jump(b, so, prefix):
    j = so + prefix
    while b[j] != 0xE9:
        j += 1
    steal = j - (so + prefix)
    return steal, j

def main(dll, sites):
    b = open(dll, "rb").read()
    opt, is64, secs = sections(b)

    def r2o(rva):
        for _, va, vs, rs, pr, _ in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + pr
        raise ValueError(f"rva {rva:x} not mapped")

    oktext = next((s for s in secs if s[0] == ".oktext"), None)
    if not oktext or oktext[5] != 0x60000020:
        print("FAIL .oktext section missing or not execute+read")
        return 1
    iat = okmod_iat(b, opt, is64, r2o)

    bad = 0
    for line in open(sites):
        parts = line.split()
        if not parts:
            continue
        rva_s, kind, name = parts[0], parts[1], parts[2]
        ret = int(parts[3]) if len(parts) > 3 else 0
        mrva = int(rva_s, 16)
        o = r2o(mrva)
        tag = f"{kind} {name} @ {rva_s}"
        if b[o] != 0xE9:
            print(f"FAIL {tag}: entry is not a jump")
            bad += 1
            continue
        stub = mrva + 5 + struct.unpack_from("<i", b, o + 1)[0]
        so = r2o(stub)

        if kind == "Toggle" and not is64:
            ok = b[so:so + 6].hex() == "e80000000058"
            call_t = stub + 6 + struct.unpack_from("<i", b, so + 15)[0]
            name_t = stub + 6 + struct.unpack_from("<i", b, so + 8)[0]
            ret_ok = b[so + 26] == 0xB0 and b[so + 27] == ret and b[so + 28] == 0xC3
            steal, j = tail_jump(b, so, 29)
            want_call = iat.get("okmod_flag")
        elif kind == "Toggle":
            ok = b[so:so + 6].hex() == "515241504151"
            call_t = stub + 23 + struct.unpack_from("<i", b, so + 19)[0]
            name_t = stub + 17 + struct.unpack_from("<i", b, so + 13)[0]
            ret_ok = b[so + 37] == 0xB0 and b[so + 38] == ret and b[so + 39] == 0xC3
            steal, j = tail_jump(b, so, 40)
            want_call = iat.get("okmod_flag")
        else:
            ok = b[so:so + 4].hex() == "4883ec68"
            call_t = stub + 86 + struct.unpack_from("<i", b, so + 82)[0]
            name_t = stub + 64 + struct.unpack_from("<i", b, so + 60)[0]
            ret_ok = True
            steal, j = tail_jump(b, so, 151)
            want_call = iat.get("okmod_event")

        no = r2o(name_t)
        got_name = b[no:b.index(b"\0", no)].decode("latin1")
        jback = stub + (j - so) + 5 + struct.unpack_from("<i", b, j + 1)[0]
        problems = []
        if not ok:
            problems.append("stub prologue")
        if call_t != want_call:
            problems.append("call target")
        if got_name != name:
            problems.append(f"name '{got_name}'")
        if not ret_ok:
            problems.append("return constant")
        if jback != mrva + steal:
            problems.append("tail jump")
        if problems:
            print(f"FAIL {tag}: " + ", ".join(problems))
            bad += 1
        else:
            print(f"ok   {tag}")
    print("okverify FAILED" if bad else "okverify passed")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
