namespace OpenKogama.Mods.Patches;

public sealed class Pe
{
    public byte[] Bytes;
    readonly int _pe;
    readonly int _opt;
    public bool Is64 { get; }

    public Pe(byte[] bytes)
    {
        Bytes = bytes;
        _pe = I32(0x3C);
        if (U16(_pe) != 0x4550 && BitConverter.ToUInt32(bytes, _pe) != 0x00004550) { }
        _opt = _pe + 24;
        Is64 = U16(_opt) == 0x20B;
        if (!Is64 && U16(_opt) != 0x10B) throw new InvalidDataException("not a PE optional header");
    }

    int Sections => U16(_pe + 6);
    int SizeOfOptional => U16(_pe + 20);
    int SectionTable => _opt + SizeOfOptional;
    public int SectionAlignment => I32(_opt + 32);
    public int FileAlignment => I32(_opt + 36);

    int DataDirCountOffset => _opt + (Is64 ? 108 : 92);
    int DataDirOffset => _opt + (Is64 ? 112 : 96);
    public int SizeOfImageOffset => _opt + 56;

    public (uint Rva, uint Size) DataDir(int index)
    {
        int at = DataDirOffset + index * 8;
        return (U32(at), U32(at + 4));
    }

    public void SetDataDir(int index, uint rva, uint size)
    {
        int at = DataDirOffset + index * 8;
        Write32(at, rva);
        Write32(at + 4, size);
    }

    public IEnumerable<(uint Va, uint VSize, uint Raw, uint RawSize)> SectionList()
    {
        for (int i = 0; i < Sections; i++)
        {
            int s = SectionTable + i * 40;
            yield return (U32(s + 12), U32(s + 8), U32(s + 20), U32(s + 16));
        }
    }

    static uint Align(uint value, uint to) => (value + to - 1) / to * to;

    public uint NextSectionRva()
    {
        uint va = 0;
        foreach (var s in SectionList()) va = Math.Max(va, Align(s.Va + s.VSize, (uint)SectionAlignment));
        return va;
    }

    public uint AddSection(string name, byte[] data, uint characteristics, out uint rawAt)
    {
        int count = Sections;
        int newHeader = SectionTable + count * 40;
        uint sizeOfHeaders = U32(_opt + 60);
        if ((uint)(newHeader + 40) > sizeOfHeaders)
            throw new InvalidDataException("no room in the header for another section");

        uint fileAlign = (uint)FileAlignment, sectAlign = (uint)SectionAlignment;
        uint va = 0, end = 0;
        foreach (var s in SectionList())
        {
            end = Math.Max(end, s.Raw + s.RawSize);
            va = Math.Max(va, Align(s.Va + s.VSize, sectAlign));
        }
        uint rawSize = Align((uint)data.Length, fileAlign);
        uint rawPtr = Align((uint)Bytes.Length, fileAlign);

        var grown = new byte[rawPtr + rawSize];
        Array.Copy(Bytes, grown, Bytes.Length);
        Array.Copy(data, 0, grown, rawPtr, data.Length);
        Bytes = grown;

        byte[] tag = new byte[8];
        System.Text.Encoding.ASCII.GetBytes(name).CopyTo(tag, 0);
        Array.Copy(tag, 0, Bytes, newHeader, 8);
        Write32(newHeader + 8, (uint)data.Length);
        Write32(newHeader + 12, va);
        Write32(newHeader + 16, rawSize);
        Write32(newHeader + 20, rawPtr);
        Write32(newHeader + 24, 0);
        Write32(newHeader + 28, 0);
        Write32(newHeader + 32, 0);
        Write32(newHeader + 36, characteristics);

        Write16(_pe + 6, (ushort)(count + 1));
        Write32(SizeOfImageOffset, Align(va + (uint)data.Length, sectAlign));
        rawAt = rawPtr;
        return va;
    }

    public int Rva2Raw(uint rva)
    {
        foreach (var s in SectionList())
            if (rva >= s.Va && rva < s.Va + Math.Max(s.VSize, s.RawSize)) return (int)(rva - s.Va + s.Raw);
        throw new InvalidDataException($"rva {rva:X} not in any section");
    }

    public bool Relocations(uint rva, int length, bool neutralize)
    {
        var (table, size) = DataDir(5);
        if (table == 0 || size == 0) return false;
        int start = Rva2Raw(table);
        bool any = false;
        for (int block = start; block + 8 <= start + size;)
        {
            uint page = U32(block);
            int blockSize = (int)U32(block + 4);
            if (blockSize < 8) break;
            for (int entry = block + 8; entry + 2 <= block + blockSize; entry += 2)
            {
                ushort value = U16(entry);
                int width = (value >> 12) switch { 3 => 4, 10 => 8, _ => 0 };
                uint at = page + (uint)(value & 0xFFF);
                if (width > 0 && at < rva + length && at + width > rva)
                {
                    any = true;
                    if (neutralize) Write16(entry, (ushort)(value & 0xFFF));
                }
            }
            block += blockSize;
        }
        return any;
    }

    public List<int>? MovableRelocations(uint rva, int length)
    {
        var offsets = new List<int>();
        var (table, size) = DataDir(5);
        if (table == 0 || size == 0) return offsets;
        int start = Rva2Raw(table);
        for (int block = start; block + 8 <= start + size;)
        {
            uint page = U32(block);
            int blockSize = (int)U32(block + 4);
            if (blockSize < 8) break;
            for (int entry = block + 8; entry + 2 <= block + blockSize; entry += 2)
            {
                ushort value = U16(entry);
                int type = value >> 12;
                int width = type switch { 3 => 4, 10 => 8, _ => 0 };
                uint at = page + (uint)(value & 0xFFF);
                if (width == 0 || at >= rva + length || at + width <= rva) continue;
                if (type != 3 || at < rva || at + width > rva + length) return null;
                offsets.Add((int)(at - rva));
            }
            block += blockSize;
        }
        return offsets;
    }

    public void AddRelocations(IEnumerable<uint> rvas)
    {
        var (table, size) = DataDir(5);
        var blocks = new List<byte>();
        if (table != 0 && size != 0) blocks.AddRange(Bytes.AsSpan(Rva2Raw(table), (int)size).ToArray());
        foreach (var page in rvas.GroupBy(rva => rva & ~0xFFFu).OrderBy(group => group.Key))
        {
            var entries = page.Order().Select(rva => (ushort)((3 << 12) | (int)(rva & 0xFFF))).ToList();
            if (entries.Count % 2 == 1) entries.Add(0);
            blocks.AddRange(BitConverter.GetBytes(page.Key));
            blocks.AddRange(BitConverter.GetBytes((uint)(8 + entries.Count * 2)));
            foreach (ushort entry in entries) blocks.AddRange(BitConverter.GetBytes(entry));
        }
        uint moved = AddSection(".okreloc", blocks.ToArray(), 0x42000040, out _);
        SetDataDir(5, moved, (uint)blocks.Count);
    }

    public ushort U16(int at) => BitConverter.ToUInt16(Bytes, at);
    public uint U32(int at) => BitConverter.ToUInt32(Bytes, at);
    int I32(int at) => BitConverter.ToInt32(Bytes, at);
    public void Write16(int at, ushort v) => BitConverter.GetBytes(v).CopyTo(Bytes, at);
    public void Write32(int at, uint v) => BitConverter.GetBytes(v).CopyTo(Bytes, at);
}
