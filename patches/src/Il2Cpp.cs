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
    const int MaxTypes = 4_000_000;
    const int NoGenericContainer = -1;
    const uint ByRef = 1u << 29;

    readonly byte[] _metadata;
    readonly ulong _imageBase;
    readonly List<(uint Va, uint Size, uint Raw)> _sections = [];
    readonly Dictionary<(string Module, string Type, string Method, int Parameters), uint> _rvas = [];
    readonly Dictionary<(string Module, string Type, string Method, int Parameters), string[]> _params = [];
    readonly Dictionary<(string Module, string Type, string Method, int Parameters), bool> _static = [];
    readonly Dictionary<(string Module, string Type, string Method), List<(uint Token, int[] Types)>> _signatures = [];
    ulong[]? _types;
    bool _typesRead;
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

    public uint? Find(string module, string type, string method, IReadOnlyList<string> parameterTypes)
    {
        if (!_signatures.TryGetValue((module, type, method), out var candidates) || !_pointers.TryGetValue(module, out ulong[]? pointers)) return null;
        uint? found = null;
        foreach ((uint token, int[] types) in candidates)
        {
            if (types.Length != parameterTypes.Count || !types.Select(TypeName).SequenceEqual(parameterTypes)) continue;
            ulong va = pointers[(token & 0xFFFFFF) - 1];
            if (found is not null || va == 0) return null;
            found = (uint)(va - _imageBase);
        }
        return found;
    }

    public uint? Find(string type, string method, IReadOnlyList<string> parameterTypes) => Find(Game, type, method, parameterTypes);

    public IEnumerable<(string Module, string Type, string Method, string[] Types, uint Rva)> Signatures(string module)
    {
        if (!_pointers.TryGetValue(module, out ulong[]? pointers)) yield break;
        foreach (((string m, string type, string method), var candidates) in _signatures)
            if (m == module)
                foreach ((uint token, int[] types) in candidates)
                    if (pointers[(token & 0xFFFFFF) - 1] is ulong va and not 0)
                        yield return (m, type, method, [.. types.Select(TypeName)], (uint)(va - _imageBase));
    }
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
                    int parameterStart = Int(definition + parameterStartAt);
                    if (!_signatures.TryGetValue((module, key.Item1, key.Item2), out var signatures))
                        _signatures[(module, key.Item1, key.Item2)] = signatures = [];
                    signatures.Add((token, [.. Enumerable.Range(0, parameterCount).Select(i => Int(parameters + (parameterStart + i) * 12 + 8))]));
                    if (!tokens.TryAdd(key, token)) { overloaded.Add(key); continue; }
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

    string TypeName(int index)
    {
        ulong[]? types = TypeTable();
        return types is not null && index >= 0 && index < types.Length ? TypeNameAt(types[index]) : "?";
    }

    string TypeNameAt(ulong va)
    {
        if (TryOffset(va) is not int at) return "?";
        ulong data = Pointer(at);
        uint bits = BitConverter.ToUInt32(Binary, at + (Is64Bit ? 8 : 4));
        int definitions = BitConverter.ToInt32(_metadata, 164) / TypeSize;
        string name = ((bits >> 16) & 0xFF) switch
        {
            0x01 => "void",
            0x02 => "bool",
            0x03 => "char",
            0x04 => "sbyte",
            0x05 => "byte",
            0x06 => "short",
            0x07 => "ushort",
            0x08 => "int",
            0x09 => "uint",
            0x0A => "long",
            0x0B => "ulong",
            0x0C => "float",
            0x0D => "double",
            0x0E => "string",
            0x11 or 0x12 when (long)data < definitions => Simple(FullName((int)data)),
            0x15 when TryOffset(data) is int generic => TypeNameAt(Pointer(generic)),
            0x18 => "IntPtr",
            0x19 => "UIntPtr",
            0x1C => "object",
            0x1D => TypeNameAt(data) + "[]",
            uint kind => $"#{kind:X2}"
        };
        return (bits & ByRef) != 0 ? "ref " + name : name;
    }

    static string Simple(string name) => name[(name.LastIndexOf('.') + 1)..];

    ulong[]? TypeTable()
    {
        if (_typesRead) return _types;
        _typesRead = true;
        ulong definitions = (ulong)(BitConverter.ToInt32(_metadata, 164) / TypeSize);
        int size = Is64Bit ? 8 : 4;
        for (int at = 10 * size; at + 4 * size <= Binary.Length; at += size)
        {
            if (Pointer(at) != definitions || Pointer(at + 2 * size) != definitions) continue;
            if (TryOffset(Pointer(at + size)) is null || TryOffset(Pointer(at + 3 * size)) is null) continue;
            int registration = at - 10 * size;
            ulong count = Pointer(registration + 6 * size);
            if (count is 0 or > MaxTypes || TryOffset(Pointer(registration + 7 * size)) is not int table || table + (long)count * size > Binary.Length) continue;
            _types = [.. Enumerable.Range(0, (int)count).Select(i => Pointer(table + i * size))];
            break;
        }
        return _types;
    }

    int? TryOffset(ulong va)
    {
        if (va < _imageBase) return null;
        ulong rva = va - _imageBase;
        foreach ((uint start, uint size, uint raw) in _sections)
            if (rva >= start && rva < start + size && raw + (rva - start) < (ulong)Binary.Length) return (int)(rva - start + raw);
        return null;
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
