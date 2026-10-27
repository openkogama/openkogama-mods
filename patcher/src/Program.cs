using Mono.Cecil;
using OpenKogama.Mods.Patches;

const string Game = Il2Cpp.Game;
const string Engine = Il2Cpp.Engine;

if (args is ["resolve", string binary, string metadata, .. var symbols])
    return Resolve(binary, metadata, symbols);
if (args is ["dump", string dumpBinary, string dumpMetadata, .. var filter])
    return Dump(dumpBinary, dumpMetadata, filter.FirstOrDefault());
if (args is ["signatures", string sigBinary, string sigMetadata, .. var sigFilter])
    return Signatures(sigBinary, sigMetadata, sigFilter.FirstOrDefault());
if (args is ["header", string headerMetadata])
    return Header(headerMetadata);
if (args is ["selftest"])
    return SelfTest();
if (args is ["params", string pBinary, string pMetadata, .. var pSymbols] && pSymbols.Length > 0)
    return ShowParams(pBinary, pMetadata, pSymbols);
if (args is ["mono", string monoAsm, .. var monoSymbols] && monoSymbols.Length > 0)
    return MonoResolve(monoAsm, monoSymbols);
if (args is ["monoil", string ilAsm, string ilSymbol])
    return MonoIl(ilAsm, ilSymbol);
if (args is ["patch", "loader", string loaderIn, string loaderOut])
    return InjectLoader(loaderIn, loaderOut);
if (args is ["patch", "fix", string fixBinary, string fixMetadata, string fixOut, .. var fixes] && fixes.Length > 0 && fixMetadata.EndsWith(".dat"))
    return ApplyFix(fixBinary, fixMetadata, fixOut, fixes);
if (args is ["patch", "fix", string fixAssembly, string fixAssemblyOut, .. var assemblyFixes] && assemblyFixes.Length > 0)
    return ApplyManagedFix(fixAssembly, fixAssemblyOut, assemblyFixes);
if (args is ["patch", "install", string tBinary, string tMetadata, string tOut])
    return Install(tBinary, tMetadata, tOut);
if (args is ["patch", "mono", string mBinary, string mShim, string mOut])
    return InstallMono(mBinary, mShim, mOut);
if (args is ["patch", "content", string cData, string cContent, string cOut])
    return PatchContent(cData, cContent, cOut);

Console.WriteLine("usage: okpatch resolve <GameAssembly.dll> <global-metadata.dat> [module!Type::Method/params ...]");
Console.WriteLine("       okpatch dump <GameAssembly.dll> <global-metadata.dat> [substring]");
Console.WriteLine("       okpatch signatures <GameAssembly.dll> <global-metadata.dat> [substring]");
Console.WriteLine("       okpatch header <global-metadata.dat>");
Console.WriteLine("       okpatch params <GameAssembly.dll> <global-metadata.dat> <Type::Method[/N] ...>");
Console.WriteLine("       okpatch patch loader <GameAssembly.dll> <out>");
Console.WriteLine($"       okpatch patch fix <GameAssembly.dll> <global-metadata.dat> <out> [{string.Join(" ", Fixes.All[1..^1])}]");
Console.WriteLine($"       okpatch patch fix <Assembly-CSharp.dll> <out> [{string.Join(" ", Fixes.All[..^1])}]");
Console.WriteLine("       okpatch patch install <GameAssembly.dll> <global-metadata.dat> <out>");
Console.WriteLine("       okpatch patch mono <Assembly-CSharp.dll> <OkModShim.dll> <out>");
Console.WriteLine("       okpatch patch content <data.unity3d or sharedassets0.assets> <content.json> <out>");
Console.WriteLine("  a symbol without a module defaults to Assembly-CSharp.dll; without /params, any count matches");
return 2;

static int ApplyFix(string binary, string metadata, string output, string[] fixes)
{
    Il2Cpp il2cpp;
    try
    {
        il2cpp = new Il2Cpp(File.ReadAllBytes(binary), File.ReadAllBytes(metadata));
        il2cpp.Load(Game, Engine);
    }
    catch (Exception error)
    {
        Console.WriteLine($"fix: {error.Message}");
        return 1;
    }

    foreach (string fix in fixes.Where(fix => !Fixes.All.Contains(fix))) Console.WriteLine($"fix: unknown fix {fix}");
    List<string> applied = Fixes.Apply(il2cpp, fixes, line => Console.WriteLine($"fix: {line}"));
    if (applied.Count == 0) { Console.WriteLine("fix: nothing changed"); return 3; }
    File.WriteAllBytes(output, il2cpp.Binary);
    foreach (string fix in applied) Console.WriteLine($"fix: applied {fix}");
    return 0;
}

static int ApplyManagedFix(string assembly, string output, string[] fixes)
{
    var resolver = new DefaultAssemblyResolver();
    resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(assembly)));
    try
    {
        using var module = ModuleDefinition.ReadModule(new MemoryStream(File.ReadAllBytes(assembly)), new ReaderParameters { AssemblyResolver = resolver });
        foreach (string fix in fixes.Where(fix => !Fixes.All.Contains(fix))) Console.WriteLine($"fix: unknown fix {fix}");
        List<string> applied = Fixes.Apply(module, fixes);
        if (applied.Count == 0) { Console.WriteLine("fix: nothing changed"); return 3; }
        module.Write(output);
        foreach (string fix in applied) Console.WriteLine($"fix: applied {fix}");
        return 0;
    }
    catch (Exception error)
    {
        Console.WriteLine($"fix: {error.Message}");
        return 1;
    }
}

static int Install(string binary, string metadata, string output)
{
    Il2Cpp il2cpp;
    try
    {
        il2cpp = new Il2Cpp(File.ReadAllBytes(binary), File.ReadAllBytes(metadata));
        il2cpp.Load(Game, Engine);
    }
    catch (Exception error)
    {
        Console.WriteLine($"install: {error.Message}");
        return 1;
    }

    if (Framework.Install(il2cpp, line => Console.WriteLine($"install: {line}")) is not { } installed)
    {
        Console.WriteLine("install: nothing to do");
        return 3;
    }
    File.WriteAllBytes(output, installed.Binary);
    File.WriteAllLines(output + ".sites", installed.Sites.Select(a => $"{a.Rva:X} {a.Kind} {a.Name} {a.Return}"));
    foreach (Hook.Site a in installed.Sites) Console.WriteLine($"install: {a.Kind} {a.Name} @ {a.Rva:X}");
    Console.WriteLine($"install: okmod loader + {installed.Sites.Count} detours");
    return 0;
}

static int PatchContent(string data, string content, string output)
{
    try
    {
        ServerContent served = ServerContent.FromJson(File.ReadAllText(content));
        byte[] patched = ContentPatch.Apply(File.ReadAllBytes(data), served, out List<string> themes);
        if (served.Sounds.Count == 0 && themes.Count == 0) { Console.WriteLine("content: nothing changed"); return 3; }
        File.WriteAllBytes(output, patched);
        Console.WriteLine($"content: {served.Sounds.Count} sounds added, themes {(themes.Count > 0 ? string.Join(", ", themes) : "unchanged")}");
        return 0;
    }
    catch (Exception error)
    {
        Console.WriteLine($"content: {error.Message}");
        return 1;
    }
}

static int InstallMono(string input, string shim, string output)
{
    MonoPatch.Toggle[] toggles = [.. Features.Toggles.Select(t => new MonoPatch.Toggle(t.Flag, t.Type, t.Method, t.Ret))];
    MonoPatch.FloatSet[] floats = [.. Features.Floats.Select(f => new MonoPatch.FloatSet(f.Flag, f.Type, f.Method, f.Param, f.Arg, f.Fallback))];
    try
    {
        List<string> applied = MonoPatch.Install(input, shim, output, toggles, floats);
        if (applied.Count == 0) { Console.WriteLine("mono: nothing resolved to patch"); return 3; }
        foreach (string a in applied) Console.WriteLine($"mono: {a}");
        Console.WriteLine($"mono: {applied.Count} IL toggles");
        return 0;
    }
    catch (Exception error)
    {
        Console.WriteLine($"mono: {error.Message}");
        return 1;
    }
}

static int InjectLoader(string input, string output)
{
    try
    {
        var pe = new Pe(File.ReadAllBytes(input));
        Loader.InjectImport(pe, Framework.Library, ["okmod_flag", "okmod_paramf", "okmod_gate", "okmod_pump"]);
        File.WriteAllBytes(output, pe.Bytes);
        Console.WriteLine($"loader: added okmod.dll import, wrote {output} ({pe.Bytes.Length} bytes)");
        return 0;
    }
    catch (Exception error)
    {
        Console.WriteLine($"loader: {error.Message}");
        return 1;
    }
}

static int MonoIl(string assembly, string symbol)
{
    var mono = new MonoImage(assembly);
    (_, string type, string method, int? parameters) = Parse(symbol);
    if (mono.Find(type, method, parameters) is not { HasBody: true } m) { Console.WriteLine("not found"); return 1; }
    bool refsShim = mono.Module.AssemblyReferences.Any(r => r.Name == "OkModShim");
    Console.WriteLine($"{symbol} (references OkModShim: {refsShim})");
    foreach (var insn in m.Body.Instructions.Take(6)) Console.WriteLine($"  {insn}");
    return 0;
}

static int MonoResolve(string assembly, string[] symbols)
{
    MonoImage mono;
    try { mono = new MonoImage(assembly); }
    catch (Exception error) { Console.WriteLine($"mono: {error.Message}"); return 1; }

    int missing = 0;
    foreach (string symbol in symbols)
    {
        (_, string type, string method, int? parameters) = Parse(symbol);
        if (mono.Find(type, method, parameters) is { } m)
        {
            string ps = string.Join(", ", m.Parameters.Select(p => p.Name));
            Console.WriteLine($"  ok   {symbol}: {(m.IsStatic ? "static " : "")}{m.ReturnType.Name}({ps})");
        }
        else { Console.WriteLine($"  MISS {symbol}"); missing++; }
    }
    Console.WriteLine($"{symbols.Length - missing}/{symbols.Length} found");
    return missing == 0 ? 0 : 3;
}

static int ShowParams(string binary, string metadata, string[] symbols)
{
    Il2Cpp il2cpp;
    try
    {
        il2cpp = new Il2Cpp(File.ReadAllBytes(binary), File.ReadAllBytes(metadata));
        il2cpp.Load(Game, Engine);
    }
    catch (Exception error)
    {
        Console.WriteLine($"params: {error.Message}");
        return 1;
    }
    foreach (string symbol in symbols)
    {
        (string module, string type, string method, int? parameters) = Parse(symbol);
        int count = parameters ?? Enumerable.Range(0, 16).FirstOrDefault(n => il2cpp.Find(module, type, method, n) is not null);
        IReadOnlyList<string>? names = il2cpp.ParamsOf(module, type, method, count);
        if (names is null) { Console.WriteLine($"  {symbol}: not resolved"); continue; }
        bool stat = il2cpp.IsStatic(module, type, method, count);
        Console.WriteLine($"  {symbol}: {(stat ? "static " : "")}({string.Join(", ", names)})");
    }
    return 0;
}

static int SelfTest()
{
    int bad = 0;
    void Expect(string name, byte[] code, int len, bool rip, bool branch)
    {
        X64.Insn i = X64.Decode(code, 0);
        bool ok = i.Length == len && i.RipRelative == rip && i.RelBranch == branch;
        Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {name}: len {i.Length} (want {len}), rip {i.RipRelative}, branch {i.RelBranch}");
        if (!ok) bad++;
    }
    Expect("sub rsp,28h", [0x48, 0x83, 0xEC, 0x28], 4, false, false);
    Expect("push rbp", [0x55], 1, false, false);
    Expect("mov [rsp+8],rbx", [0x48, 0x89, 0x5C, 0x24, 0x08], 5, false, false);
    Expect("mov eax,imm32", [0xB8, 1, 0, 0, 0], 5, false, false);
    Expect("mov rax,imm64", [0x48, 0xB8, 1, 0, 0, 0, 0, 0, 0, 0], 10, false, false);
    Expect("ret", [0xC3], 1, false, false);
    Expect("movzx eax,al", [0x0F, 0xB6, 0xC0], 3, false, false);
    Expect("call rel32", [0xE8, 0, 0, 0, 0], 5, false, true);
    Expect("lea rax,[rip+x]", [0x48, 0x8D, 0x05, 0x10, 0, 0, 0], 7, true, false);
    Expect("add rsp,28h", [0x48, 0x83, 0xC4, 0x28], 4, false, false);
    Expect("xor eax,eax", [0x33, 0xC0], 2, false, false);

    void Expect86(string name, byte[] code, int len)
    {
        X64.Insn i = X64.Decode(code, 0, x86: true);
        bool ok = i.Length == len;
        Console.WriteLine($"{(ok ? "ok  " : "FAIL")} x86 {name}: len {i.Length} (want {len})");
        if (!ok) bad++;
    }
    Expect86("push ebp", [0x55], 1);
    Expect86("mov ebp,esp", [0x8B, 0xEC], 2);
    Expect86("sub esp,10h", [0x83, 0xEC, 0x10], 3);
    Expect86("inc edi", [0x47], 1);
    Expect86("push edi", [0x57], 1);
    Expect86("mov eax,[esp+4]", [0x8B, 0x44, 0x24, 0x04], 4);

    int steal = X64.StealLength([0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x5C, 0x24, 0x08], 0, 5);
    bool stealOk = steal == 9;
    Console.WriteLine($"{(stealOk ? "ok  " : "FAIL")} steal 5 -> {steal} (want 9)");
    if (!stealOk) bad++;

    int refuse = X64.StealLength([0xE8, 0, 0, 0, 0, 0x90], 0, 5);
    bool refuseOk = refuse == 0;
    Console.WriteLine($"{(refuseOk ? "ok  " : "FAIL")} refuse rel branch -> {refuse} (want 0)");
    if (!refuseOk) bad++;

    Console.WriteLine(bad == 0 ? "selftest passed" : $"selftest FAILED ({bad})");
    return bad == 0 ? 0 : 1;
}

static int Header(string metadata)
{
    byte[] m = File.ReadAllBytes(metadata);
    int Int(int at) => BitConverter.ToInt32(m, at);
    Console.WriteLine($"length {m.Length}");
    Console.WriteLine($"sanity {BitConverter.ToUInt32(m, 0):X8}  version {Int(4)}");

    for (int i = 2; i < 66; i += 2)
        Console.WriteLine($"  [{i * 4,3}] {Int(i * 4),11}   [{(i + 1) * 4,3}] {Int((i + 1) * 4),11}");
    return 0;
}

static int Dump(string binary, string metadata, string? filter)
{
    Il2Cpp il2cpp;
    try
    {
        il2cpp = new Il2Cpp(File.ReadAllBytes(binary), File.ReadAllBytes(metadata));
        il2cpp.Load(Game, Engine);
    }
    catch (Exception error)
    {
        Console.WriteLine($"dump: {error.Message}");
        return 1;
    }
    foreach ((string module, string type, string method, int parameters, uint rva) in il2cpp.Resolved
        .Where(e => filter is null || e.Type.Contains(filter, StringComparison.OrdinalIgnoreCase))
        .OrderBy(e => e.Type).ThenBy(e => e.Method))
        Console.WriteLine($"{module}!{type}::{method}/{parameters} {rva:X}");
    return 0;
}

static int Signatures(string binary, string metadata, string? filter)
{
    Il2Cpp il2cpp;
    try
    {
        il2cpp = new Il2Cpp(File.ReadAllBytes(binary), File.ReadAllBytes(metadata));
        il2cpp.Load(Game, Engine);
    }
    catch (Exception error)
    {
        Console.WriteLine($"signatures: {error.Message}");
        return 1;
    }
    foreach ((string module, string type, string method, string[] types, uint rva) in il2cpp.Signatures(Game).Concat(il2cpp.Signatures(Engine))
        .Where(e => filter is null || e.Type.Contains(filter, StringComparison.OrdinalIgnoreCase))
        .OrderBy(e => e.Type).ThenBy(e => e.Method))
        Console.WriteLine($"{module}!{type}::{method}({string.Join(",", types)}) {rva:X}");
    return 0;
}

static int Resolve(string binary, string metadata, string[] symbols)
{
    Il2Cpp il2cpp;
    try
    {
        il2cpp = new Il2Cpp(File.ReadAllBytes(binary), File.ReadAllBytes(metadata));
        il2cpp.Load(Game, Engine);
    }
    catch (Exception error)
    {
        Console.WriteLine($"resolve: {error.Message}");
        return 1;
    }

    Console.WriteLine($"{(il2cpp.Is64Bit ? "x64" : "x86")}, {il2cpp.Resolved.Count()} methods resolved");
    if (symbols.Length == 0) return 0;

    int missing = 0;
    foreach (string symbol in symbols)
    {
        (string module, string type, string method, int? parameters) = Parse(symbol);
        uint? rva = parameters is int p
            ? il2cpp.Find(module, type, method, p)
            : Enumerable.Range(0, 16).Select(n => il2cpp.Find(module, type, method, n)).FirstOrDefault(r => r is not null);
        if (rva is uint found) Console.WriteLine($"  ok   {symbol} -> {found:X}");
        else { Console.WriteLine($"  MISS {symbol}"); missing++; }
    }
    Console.WriteLine($"{symbols.Length - missing}/{symbols.Length} found");
    return missing == 0 ? 0 : 3;
}

static (string Module, string Type, string Method, int? Parameters) Parse(string symbol)
{
    string module = Game;
    int bang = symbol.IndexOf('!');
    if (bang >= 0) { module = symbol[..bang]; symbol = symbol[(bang + 1)..]; }
    int? parameters = null;
    int slash = symbol.IndexOf('/');
    if (slash >= 0) { parameters = int.Parse(symbol[(slash + 1)..]); symbol = symbol[..slash]; }
    int split = symbol.LastIndexOf("::", StringComparison.Ordinal);
    return (module, symbol[..split], symbol[(split + 2)..], parameters);
}
