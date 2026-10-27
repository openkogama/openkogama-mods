using System.Text;

namespace OpenKogama.Mods.Patches;

public sealed class Il2Cpp
{
    public const string Game = "Assembly-CSharp.dll";
    public const string Engine = "UnityEngine.CoreModule.dll";
    const uint Sanity = 0xFAB11BAF;
    const int TypeSize = 88;
    const int ImageSize = 40;
    const int MaxMethods = 1_000_000;
    const int NoGenericContainer = -1;

    readonly byte[] _metadata;
    readonly ulong _imageBase;
    readonly List<(uint Va, uint Size, uint Raw)> _sections = [];
    readonly Dictionary<(string Module, string Type, string Method, int Parameters), uint> _rvas = [];
    readonly Dictionary<(string Module, string Type, string Method, int Parameters), string[]> _params = [];
    readonly Dictionary<(string Module, string Type, string Method, int Parameters), bool> _static = [];
    readonly Dictionary<string, ulong[]> _pointers = [];
    readonly Dictionary<int, string> _typeNames = [];
    readonly Dictionary<int, int> _parent = [];
    readonly HashSet<(string Module, string Type)> _nestedTypes = [];
    bool _nested;
    readonly Dictionary<uint, int> _uses = [];
    uint[] _starts = [];

    public byte[] Binary { get; }
    public bool Is64Bit { get; }
    public ulong ImageBase => _imageBase;

    public Il2Cpp(byte[] binary, byte[] metadata)
    {
        Binary = binary;
        _metadata = metadata;
        if (BitConverter.ToUInt32(metadata, 0) != Sanity) throw new InvalidDataException("not IL2CPP metadata");

        int pe = BitConverter.ToInt32(binary, 0x3C);
        int sections = BitConverter.ToUInt16(binary, pe + 6);
        int optional = BitConverter.ToUInt16(binary, pe + 20);
        Is64Bit = BitConverter.ToUInt16(binary, pe + 24) == 0x20B;
        _imageBase = Is64Bit ? BitConverter.ToUInt64(binary, pe + 24 + 24) : BitConverter.ToUInt32(binary, pe + 24 + 28);
        for (int i = 0; i < sections; i++)
        {
            int at = pe + 24 + optional + i * 40;
            _sections.Add((BitConverter.ToUInt32(binary, at + 12), BitConverter.ToUInt32(binary, at + 16), BitConverter.ToUInt32(binary, at + 20)));
        }
    }

    public void Load(params string[] modules)
    {
        foreach (string module in modules)
        {
            if (_pointers.ContainsKey(module)) continue;
            (Dictionary<(string, string, int), uint> tokens, int count) = Tokens(module);
            ulong[] pointers = Pointers(module, count);
            _pointers[module] = pointers;
            foreach (((string type, string method, int parameters), uint token) in tokens)
            {
                ulong va = pointers[(token & 0xFFFFFF) - 1];
                if (va != 0) _rvas[(module, type, method, parameters)] = (uint)(va - _imageBase);
            }
            foreach (ulong va in pointers.Where(va => va != 0))
                _uses[(uint)(va - _imageBase)] = _uses.GetValueOrDefault((uint)(va - _imageBase)) + 1;
        }
        _starts = [.. _uses.Keys.Order()];
    }

    public uint? Find(string module, string type, string method, int parameters) =>
        _rvas.TryGetValue((module, type, method, parameters), out uint rva) ? rva : null;

    public uint? Find(string type, string method, int parameters) => Find(Game, type, method, parameters);
    public uint? FindEngine(string type, string method, int parameters) => Find(Engine, type, method, parameters);

    public IEnumerable<(string Type, uint Rva)> Named(string method, int parameters) =>
        _rvas.Where(pair => pair.Key.Module == Game && pair.Key.Method == method && pair.Key.Parameters == parameters && !_nestedTypes.Contains((Game, pair.Key.Type)))
            .Select(pair => (pair.Key.Type, pair.Value));

    public HashSet<uint> CallTargets()
    {
        var targets = new HashSet<uint>();
        foreach ((uint va, uint size, uint raw) in _sections)
            for (long at = 0; at + 5 <= size && raw + at + 5 <= Binary.Length; at++)
                if (Binary[raw + at] is 0xE8 or 0xE9)
                    targets.Add((uint)(va + at + 5 + BitConverter.ToInt32(Binary, (int)(raw + at + 1))));
        return targets;
    }

    public void Unrelocate(uint rva, int length)
    {
        int pe = BitConverter.ToInt32(Binary, 0x3C);
        int directory = pe + 24 + (Is64Bit ? 112 : 96) + 5 * 8;
        uint table = BitConverter.ToUInt32(Binary, directory), size = BitConverter.ToUInt32(Binary, directory + 4);
        if (table == 0 || size == 0) return;
        int start = Offset(table);
        for (int block = start; block + 8 <= start + size;)
        {
            uint page = BitConverter.ToUInt32(Binary, block);
            int blockSize = BitConverter.ToInt32(Binary, block + 4);
            if (blockSize < 8) return;
            for (int entry = block + 8; entry + 2 <= block + blockSize; entry += 2)
            {
                ushort value = BitConverter.ToUInt16(Binary, entry);
                int width = (value >> 12) switch { 3 => 4, 10 => 8, _ => 0 };
                uint at = page + (uint)(value & 0xFFF);
                if (width > 0 && at < rva + length && at + width > rva)
                    BitConverter.TryWriteBytes(Binary.AsSpan(entry), (ushort)(value & 0xFFF));
            }
            block += blockSize;
        }
    }

    public IEnumerable<(string Module, string Type, string Method, int Parameters, uint Rva)> Resolved =>
        _rvas.Select(e => (e.Key.Module, e.Key.Type, e.Key.Method, e.Key.Parameters, e.Value));

    public IReadOnlyList<string>? ParamsOf(string module, string type, string method, int parameters) =>
        _params.TryGetValue((module, type, method, parameters), out string[]? p) ? p : null;

    public IReadOnlyList<string>? ParamsOf(string type, string method, int parameters) => ParamsOf(Game, type, method, parameters);

    public bool IsStatic(string module, string type, string method, int parameters) =>
        _static.TryGetValue((module, type, method, parameters), out bool s) && s;

    public int ArgSlot(string type, string method, int parameters, string paramName, bool isFloat)
    {
        IReadOnlyList<string>? names = ParamsOf(type, method, parameters);
        int index = names is null ? -1 : names.ToList().IndexOf(paramName);
        if (index < 0) return -1;
        int reg = index + (IsStatic(Game, type, method, parameters) ? 0 : 1);
        return reg > 3 ? -1 : isFloat ? 4 + reg : reg;
    }

    public bool Shared(uint rva) => _uses.GetValueOrDefault(rva) > 1;
    public bool Known(uint rva) => _uses.ContainsKey(rva);

    public uint End(uint rva)
    {
        int next = Array.BinarySearch(_starts, rva + 1);
        return next >= 0 ? _starts[next] : ~next < _starts.Length ? _starts[~next] : rva;
    }

    public int Offset(uint rva)
    {
        foreach ((uint va, uint size, uint raw) in _sections)
            if (rva >= va && rva < va + size) return (int)(rva - va + raw);
        throw new InvalidDataException($"rva {rva:X} is outside the file");
    }

    (Dictionary<(string, string, int), uint> Tokens, int Count) Tokens(string module)
    {
        int version = BitConverter.ToInt32(_metadata, 4);
        int methodSize = version switch { 29 => 32, 31 => 36, _ => throw new NotSupportedException($"IL2CPP metadata version {version}") };
        int Int(int at) => BitConverter.ToInt32(_metadata, at);
        int methods = Int(48), images = Int(168);
        if (Int(52) % methodSize != 0 || Int(164) % TypeSize != 0 || Int(172) % ImageSize != 0)
            throw new NotSupportedException($"IL2CPP metadata version {version} has an unknown layout");

        int parameters = Int(88);
        int parameterStartAt = methodSize - 20;
        for (int image = images; image < images + Int(172); image += ImageSize)
        {
            if (Name(Int(image)) != module) continue;
            var tokens = new Dictionary<(string, string, int), uint>();
            var names = new Dictionary<(string, string, int), string[]>();
            var statics = new Dictionary<(string, string, int), bool>();
            var overloaded = new HashSet<(string, string, int)>();
            int count = 0;
            for (int type = Int(image + 8); type < Int(image + 8) + Int(image + 12); type++)
            {
                int at = Int(160) + type * TypeSize;
                string typeName = FullName(type);
                if (_parent.ContainsKey(type)) _nestedTypes.Add((module, typeName));
                for (int method = Int(at + 36); method < Int(at + 36) + BitConverter.ToUInt16(_metadata, at + 64); method++)
                {
                    int definition = methods + method * methodSize;
                    uint token = BitConverter.ToUInt32(_metadata, definition + methodSize - 12);
                    count = Math.Max(count, (int)(token & 0xFFFFFF));
                    if (Int(definition + methodSize - 16) != NoGenericContainer) continue;
                    int parameterCount = BitConverter.ToUInt16(_metadata, definition + methodSize - 2);
                    var key = (typeName, Name(Int(definition)), parameterCount);
                    if (!tokens.TryAdd(key, token)) { overloaded.Add(key); continue; }
                    int parameterStart = Int(definition + parameterStartAt);
                    names[key] = [.. Enumerable.Range(0, parameterCount).Select(i => Name(Int(parameters + (parameterStart + i) * 12)))];
                    statics[key] = (BitConverter.ToUInt16(_metadata, definition + methodSize - 8) & 0x10) != 0;
                }
            }
            foreach (var key in overloaded) { tokens.Remove(key); names.Remove(key); statics.Remove(key); }
            foreach (var (key, list) in names) _params[(module, key.Item1, key.Item2, key.Item3)] = list;
            foreach (var (key, value) in statics) _static[(module, key.Item1, key.Item2, key.Item3)] = value;
            return (tokens, count);
        }
        throw new InvalidDataException($"GameAssembly.dll has no {module}");
    }

    string Name(int index)
    {
        int strings = BitConverter.ToInt32(_metadata, 24) + index;
        return Encoding.UTF8.GetString(_metadata, strings, Array.IndexOf(_metadata, (byte)0, strings) - strings);
    }

    void EnsureNesting()
    {
        if (_nested) return;
        _nested = true;
        int Int(int at) => BitConverter.ToInt32(_metadata, at);
        int typesBase = Int(160), typeCount = Int(164) / TypeSize;
        int nestedOff = Int(128), nestedCount = Int(132) / 4;
        for (int t = 0; t < typeCount; t++)
        {
            int at = typesBase + t * TypeSize;
            int start = Int(at + 48), count = BitConverter.ToUInt16(_metadata, at + 72);
            if (count == 0 || start < 0) continue;
            for (int k = start; k < start + count && k < nestedCount; k++)
                _parent[Int(nestedOff + k * 4)] = t;
        }
    }

    string FullName(int type)
    {
        if (_typeNames.TryGetValue(type, out string? cached)) return cached;
        EnsureNesting();
        int at = BitConverter.ToInt32(_metadata, 160) + type * TypeSize;
        string simple = Name(BitConverter.ToInt32(_metadata, at));
        string name;
        if (_parent.TryGetValue(type, out int declaring))
            name = FullName(declaring) + "." + simple;
        else
        {
            string space = Name(BitConverter.ToInt32(_metadata, at + 4));
            name = space.Length > 0 ? space + "." + simple : simple;
        }
        _typeNames[type] = name;
        return name;
    }

    ulong[] Pointers(string module, int count)
    {
        byte[] name = Encoding.ASCII.GetBytes(module + "\0");
        var names = new HashSet<ulong>();
        for (int at = Binary.AsSpan().IndexOf(name); at >= 0; at = Next(Binary, name, at + 1))
            if (Rva(at) is uint rva) names.Add(_imageBase + rva);

        int size = Is64Bit ? 8 : 4;
        for (int at = 0; at + 3 * size <= Binary.Length; at += size)
        {
            if (!names.Contains(Pointer(at)) || (Is64Bit ? BitConverter.ToInt64(Binary, at + size) : BitConverter.ToInt32(Binary, at + size)) != count) continue;
            ulong table = Pointer(at + 2 * size);
            if (table < _imageBase || count is <= 0 or > MaxMethods) continue;
            int offset = Offset((uint)(table - _imageBase));
            if (offset + (long)count * size > Binary.Length) continue;
            return [.. Enumerable.Range(0, count).Select(i => Pointer(offset + i * size))];
        }
        throw new InvalidDataException($"GameAssembly.dll has no code module for {module} with {count} methods");
    }

    ulong Pointer(int at) => Is64Bit ? BitConverter.ToUInt64(Binary, at) : BitConverter.ToUInt32(Binary, at);

    uint? Rva(int offset)
    {
        foreach ((uint va, uint size, uint raw) in _sections)
            if (offset >= raw && offset < raw + size) return (uint)(offset - raw + va);
        return null;
    }

    static int Next(byte[] data, byte[] value, int from)
    {
        int found = data.AsSpan(from).IndexOf(value);
        return found < 0 ? -1 : from + found;
    }
}
