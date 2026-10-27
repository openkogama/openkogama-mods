namespace OpenKogama.Mods.Patches;

public static class Hook
{
    const uint SectionRX = 0x60000020;
    const int JmpBack = 5;
    const int TogglePrefix = 40;
    const int EventPrefix = 151;
    const int Toggle86Prefix = 29;

    public enum Kind { Toggle, Event }
    public readonly record struct Site(Kind Kind, string Name, uint Rva, byte Return);

    public static List<Site> Apply(Pe pe, uint flagIat, uint eventIat, IReadOnlyList<Site> sites, Action<string> log)
    {
        bool x86 = !pe.Is64;

        var plan = new List<(Site S, int Steal, byte[] Stolen)>();
        foreach (Site s in sites)
        {
            if (x86 && s.Kind == Kind.Event) { log($"detours: x86 event stub not written yet, skipping {s.Name}"); continue; }
            int raw = pe.Rva2Raw(s.Rva);
            int steal = X64.StealLength(pe.Bytes, raw, JmpBack, x86);
            if (steal == 0 || pe.Relocations(s.Rva, steal, false)) { log($"detours: cannot move the start of {s.Name} at {s.Rva:X}, skipping it"); continue; }
            plan.Add((s, steal, pe.Bytes[raw..(raw + steal)]));
        }
        if (plan.Count == 0) return [];

        int Prefix(Kind k) => x86 ? Toggle86Prefix : k == Kind.Toggle ? TogglePrefix : EventPrefix;
        uint v = pe.NextSectionRva();
        var stubRva = new uint[plan.Count];
        int cursor = 0;
        for (int i = 0; i < plan.Count; i++)
        {
            stubRva[i] = (uint)(v + cursor);
            cursor += Prefix(plan[i].S.Kind) + plan[i].Steal + JmpBack;
        }
        var nameRva = new Dictionary<string, uint>();
        var names = new List<byte>();
        foreach (var (s, _, _) in plan)
            if (!nameRva.ContainsKey(s.Name))
            {
                nameRva[s.Name] = (uint)(v + cursor + names.Count);
                names.AddRange(System.Text.Encoding.ASCII.GetBytes(s.Name));
                names.Add(0);
            }

        var blob = new List<byte>();
        for (int i = 0; i < plan.Count; i++)
        {
            var (s, steal, stolen) = plan[i];
            uint a = stubRva[i];
            if (x86) Toggle86(blob, a, s, steal, stolen, nameRva[s.Name], flagIat);
            else if (s.Kind == Kind.Toggle) Toggle(blob, a, s, steal, stolen, nameRva[s.Name], flagIat);
            else Event(blob, a, s, steal, stolen, nameRva[s.Name], eventIat);
        }
        blob.AddRange(names);

        uint got = pe.AddSection(".oktext", blob.ToArray(), SectionRX, out _);
        if (got != v) throw new InvalidDataException($"cave rva moved: predicted {v:X}, got {got:X}");

        var applied = new List<Site>();
        for (int i = 0; i < plan.Count; i++)
        {
            var (s, steal, _) = plan[i];
            int raw = pe.Rva2Raw(s.Rva);
            pe.Bytes[raw] = 0xE9;
            BitConverter.GetBytes((int)(stubRva[i] - (s.Rva + 5))).CopyTo(pe.Bytes, raw + 1);
            for (int b = 5; b < steal; b++) pe.Bytes[raw + b] = 0x90;
            pe.Relocations(s.Rva, steal, true);
            applied.Add(s);
        }
        return applied;
    }

    static void I32(List<byte> code, long value) => code.AddRange(BitConverter.GetBytes((int)value));

    static void Toggle(List<byte> code, uint a, Site s, int steal, byte[] stolen, uint name, uint flagIat)
    {
        code.AddRange([0x51, 0x52, 0x41, 0x50, 0x41, 0x51]);
        code.AddRange([0x48, 0x83, 0xEC, 0x28]);
        code.AddRange([0x48, 0x8D, 0x0D]); I32(code, name - (a + 17));
        code.AddRange([0xFF, 0x15]); I32(code, flagIat - (a + 23));
        code.AddRange([0x48, 0x83, 0xC4, 0x28]);
        code.AddRange([0x41, 0x59, 0x41, 0x58, 0x5A, 0x59]);
        code.AddRange([0x85, 0xC0, 0x74, 0x03]);
        code.AddRange([0xB0, s.Return, 0xC3]);
        code.AddRange(X64.Relocate(stolen, s.Rva, a + TogglePrefix));
        code.Add(0xE9); I32(code, s.Rva + (uint)steal - (a + (uint)(TogglePrefix + steal) + JmpBack));
    }

    static void Toggle86(List<byte> code, uint a, Site s, int steal, byte[] stolen, uint name, uint flagIat)
    {
        code.AddRange([0xE8, 0, 0, 0, 0, 0x58]);
        code.AddRange([0x8D, 0x90]); I32(code, name - (a + 6));
        code.Add(0x52);
        code.AddRange([0xFF, 0x90]); I32(code, flagIat - (a + 6));
        code.AddRange([0x83, 0xC4, 0x04]);
        code.AddRange([0x85, 0xC0, 0x74, 0x03]);
        code.AddRange([0xB0, s.Return, 0xC3]);
        code.AddRange(stolen);
        code.Add(0xE9); I32(code, s.Rva + (uint)steal - (a + (uint)(Toggle86Prefix + steal) + JmpBack));
    }

    static void Event(List<byte> code, uint a, Site s, int steal, byte[] stolen, uint name, uint eventIat)
    {
        code.AddRange([0x48, 0x83, 0xEC, 0x68]);
        code.AddRange([0x48, 0x89, 0x4C, 0x24, 0x20]);
        code.AddRange([0x48, 0x89, 0x54, 0x24, 0x28]);
        code.AddRange([0x4C, 0x89, 0x44, 0x24, 0x30]);
        code.AddRange([0x4C, 0x89, 0x4C, 0x24, 0x38]);
        code.AddRange([0xF2, 0x0F, 0x11, 0x44, 0x24, 0x40]);
        code.AddRange([0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x48]);
        code.AddRange([0xF2, 0x0F, 0x11, 0x54, 0x24, 0x50]);
        code.AddRange([0xF2, 0x0F, 0x11, 0x5C, 0x24, 0x58]);
        code.AddRange([0x48, 0xC7, 0x44, 0x24, 0x60, 0, 0, 0, 0]);
        code.AddRange([0x48, 0x8D, 0x0D]); I32(code, name - (a + 64));
        code.AddRange([0x48, 0x8D, 0x54, 0x24, 0x20]);
        code.AddRange([0x41, 0xB8, 0x08, 0, 0, 0]);
        code.AddRange([0x4C, 0x8D, 0x4C, 0x24, 0x60]);
        code.AddRange([0xFF, 0x15]); I32(code, eventIat - (a + 86));
        code.AddRange([0x4C, 0x8B, 0x54, 0x24, 0x60]);
        code.AddRange([0x48, 0x8B, 0x4C, 0x24, 0x20]);
        code.AddRange([0x48, 0x8B, 0x54, 0x24, 0x28]);
        code.AddRange([0x4C, 0x8B, 0x44, 0x24, 0x30]);
        code.AddRange([0x4C, 0x8B, 0x4C, 0x24, 0x38]);
        code.AddRange([0xF2, 0x0F, 0x10, 0x44, 0x24, 0x40]);
        code.AddRange([0xF2, 0x0F, 0x10, 0x4C, 0x24, 0x48]);
        code.AddRange([0xF2, 0x0F, 0x10, 0x54, 0x24, 0x50]);
        code.AddRange([0xF2, 0x0F, 0x10, 0x5C, 0x24, 0x58]);
        code.AddRange([0x85, 0xC0, 0x74, 0x08]);
        code.AddRange([0x49, 0x8B, 0xC2]);
        code.AddRange([0x48, 0x83, 0xC4, 0x68, 0xC3]);
        code.AddRange([0x48, 0x83, 0xC4, 0x68]);
        code.AddRange(X64.Relocate(stolen, s.Rva, a + EventPrefix));
        code.Add(0xE9); I32(code, s.Rva + (uint)steal - (a + (uint)(EventPrefix + steal) + JmpBack));
    }
}
