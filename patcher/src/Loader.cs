using System.Text;

namespace OpenKogama.Mods.Patcher;

public static class Loader
{
    const int DescSize = 20;
    const uint SectionRWX = 0xC0000040;

    public static Dictionary<string, uint> InjectImport(Pe pe, string dll, string[] symbols)
    {
        int thunk = pe.Is64 ? 8 : 4;
        (uint importRva, _) = pe.DataDir(1);

        var existing = new List<byte[]>();
        if (importRva != 0)
        {
            int at = pe.Rva2Raw(importRva);
            while (true)
            {
                byte[] d = pe.Bytes[at..(at + DescSize)];
                bool end = true;
                foreach (byte b in d) if (b != 0) { end = false; break; }
                if (end) break;
                existing.Add(d);
                at += DescSize;
            }
        }

        uint v = pe.NextSectionRva();

        int descCount = existing.Count + 1;
        int descBytes = (descCount + 1) * DescSize;
        int iltOff = descBytes;
        int iltBytes = (symbols.Length + 1) * thunk;
        int iatOff = iltOff + iltBytes;
        int nameOff = iatOff + iltBytes;

        var blob = new List<byte>();
        void Put32(int value) => blob.AddRange(BitConverter.GetBytes((uint)value));
        void PutThunk(long value) => blob.AddRange(pe.Is64 ? BitConverter.GetBytes((ulong)value) : BitConverter.GetBytes((uint)value));

        int cursor = nameOff;
        int dllNameOff = cursor;
        byte[] dllName = Encoding.ASCII.GetBytes(dll + "\0");
        cursor += dllName.Length;
        var entryOff = new int[symbols.Length];
        var entries = new List<byte>();
        entries.AddRange(dllName);
        for (int i = 0; i < symbols.Length; i++)
        {
            entryOff[i] = cursor;
            byte[] name = Encoding.ASCII.GetBytes(symbols[i]);
            var entry = new List<byte> { 0, 0 };
            entry.AddRange(name);
            entry.Add(0);
            if (entry.Count % 2 != 0) entry.Add(0);
            entries.AddRange(entry);
            cursor += entry.Count;
        }

        foreach (byte[] d in existing) blob.AddRange(d);
        Put32((int)(v + iltOff));
        Put32(0);
        Put32(0);
        Put32((int)(v + dllNameOff));
        Put32((int)(v + iatOff));
        blob.AddRange(new byte[DescSize]);

        for (int pass = 0; pass < 2; pass++)
        {
            foreach (int off in entryOff) PutThunk(v + off);
            PutThunk(0);
        }

        blob.AddRange(entries);

        uint got = pe.AddSection(".okmod", blob.ToArray(), SectionRWX, out _);
        if (got != v) throw new InvalidDataException($"section rva moved: predicted {v:X}, got {got:X}");

        pe.SetDataDir(1, v, (uint)descBytes);
        pe.SetDataDir(11, 0, 0);

        var iat = new Dictionary<string, uint>();
        for (int i = 0; i < symbols.Length; i++) iat[symbols[i]] = (uint)(v + iatOff + i * thunk);
        return iat;
    }
}
