namespace OpenKogama.Mods.Patches;

public static class X64
{
    public readonly record struct Insn(int Length, bool RipRelative, bool RelBranch, int Displacement = 0);

    public static Insn Decode(byte[] code, int at, bool x86 = false)
    {
        int p = at;
        bool opsize = false, rexW = false;

        while (true)
        {
            byte b = code[p];
            if (b == 0x66) { opsize = true; p++; continue; }
            if (b is 0x67 or 0xF0 or 0xF2 or 0xF3 or 0x2E or 0x36 or 0x3E or 0x26 or 0x64 or 0x65) { p++; continue; }
            if (!x86 && b >= 0x40 && b <= 0x4F) { rexW = (b & 8) != 0; p++; break; }
            break;
        }

        byte op = code[p++];

        if (x86 && op >= 0x40 && op <= 0x4F) return new Insn(p - at, false, false);

        if (op == 0x0F)
        {
            byte op2 = code[p++];

            if (op2 >= 0x80 && op2 <= 0x8F) return new Insn(p - at + 4, false, true);

            if (op2 is 0xB6 or 0xB7 or 0xBE or 0xBF or 0xAF or (>= 0x90 and <= 0x9F) or 0x10 or 0x11 or 0x28 or 0x29 or 0x57 or 0x1E)
                return WithModRM(code, at, p, 0);
            return new Insn(0, false, false);
        }

        switch (op)
        {
            case 0x90:
            case 0xC3:
            case 0xCC:
            case 0xC9:
                return new Insn(p - at, false, false);
            case >= 0x50 and <= 0x5F:
                return new Insn(p - at, false, false);
            case 0xE8:
            case 0xE9:
                return new Insn(p - at + 4, false, true);
            case 0xEB:
            case >= 0x70 and <= 0x7F:
                return new Insn(p - at + 1, false, true);
            case >= 0xB8 and <= 0xBF:
                return new Insn(p - at + (rexW ? 8 : opsize ? 2 : 4), false, false);
            case 0x6A:
                return new Insn(p - at + 1, false, false);
            case 0x68:
                return new Insn(p - at + (opsize ? 2 : 4), false, false);
            case >= 0xA0 and <= 0xA3:
                return new Insn(p - at + (x86 ? 4 : 8), false, false);
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x84: case 0x85:
            case 0x30: case 0x31: case 0x32: case 0x33:
            case 0x28: case 0x29: case 0x2A: case 0x2B:
            case 0x00: case 0x01: case 0x02: case 0x03:
            case 0x20: case 0x21: case 0x22: case 0x23:
            case 0x08: case 0x09: case 0x0A: case 0x0B:
            case 0x38: case 0x39: case 0x3A: case 0x3B:
            case 0x8D:
            case 0xFF:
                return WithModRM(code, at, p, 0);
            case 0x80: case 0x82: case 0x83:
            case 0xC6:
                return WithModRM(code, at, p, 1);
            case 0x81:
            case 0xC7:
                return WithModRM(code, at, p, opsize ? 2 : 4);
            default:
                return new Insn(0, false, false);
        }
    }

    readonly record struct Mod(int Len, bool Rip);

    static Insn WithModRM(byte[] code, int at, int p, int immediate)
    {
        var m = ModRM(code, p);
        return new Insn(p + m.Len + immediate - at, m.Rip, false, m.Rip ? p + 1 - at : 0);
    }

    static Mod ModRM(byte[] code, int pos)
    {
        byte modrm = code[pos];
        int mod = modrm >> 6, rm = modrm & 7;
        int len = 1;
        bool rip = false;
        if (mod == 3) return new Mod(len, false);
        bool sib = rm == 4;
        int baseReg = rm;
        if (sib) { byte s = code[pos + 1]; len += 1; baseReg = s & 7; }
        if (mod == 0)
        {
            if (!sib && rm == 5) { len += 4; rip = true; }
            else if (sib && baseReg == 5) len += 4;
        }
        else if (mod == 1) len += 1;
        else if (mod == 2) len += 4;
        return new Mod(len, rip);
    }

    public static int StealLength(byte[] code, int at, int need, bool x86 = false)
    {
        int taken = 0;
        while (taken < need)
        {
            Insn insn = Decode(code, at + taken, x86);
            if (insn.Length == 0 || insn.RelBranch) return 0;
            taken += insn.Length;
        }
        return taken;
    }

    public static byte[] Relocate(byte[] code, uint from, uint to)
    {
        byte[] moved = [.. code];
        for (int at = 0; at < moved.Length;)
        {
            Insn insn = Decode(moved, at);
            if (insn.Length == 0) break;
            if (insn.RipRelative)
            {
                long target = BitConverter.ToInt32(moved, at + insn.Displacement) + (long)from - to;
                BitConverter.GetBytes((int)target).CopyTo(moved, at + insn.Displacement);
            }
            at += insn.Length;
        }
        return moved;
    }
}
