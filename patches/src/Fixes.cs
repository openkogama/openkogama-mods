using Mono.Cecil;

namespace OpenKogama.Mods.Patches;

public static class Fixes
{
    public const string ColossusLaser = "colossus-laser";
    public const string WorldSwitch = "world-switch";
    public const string Tcp = "tcp";
    public const string Teleport = "teleport";
    public const string Camera = "camera";
    public const string Mods = "mods";
    public const string Shim = "OkModShim.dll";
    const string GameCode = "Assembly-CSharp.dll";
    public static readonly string[] All = [ColossusLaser, WorldSwitch, Tcp, Teleport, Camera, Mods];

    public static List<string> Apply(string input, string output, IReadOnlyCollection<string> fixes, Action<string> log) =>
        Path.GetFileName(output).Equals("GameAssembly.dll", StringComparison.OrdinalIgnoreCase)
            ? Native(input, output, fixes, log)
            : Managed(input, output, fixes.Count == 0 ? [ColossusLaser] : fixes);

    public static List<string> Apply(ModuleDefinition module, IReadOnlyCollection<string> fixes)
    {
        List<string> applied = [];
        if (fixes.Contains(ColossusLaser) && ColossusLaserFix.Apply(module)) applied.Add(ColossusLaser);
        if (fixes.Contains(WorldSwitch) && WorldSwitchFix.Apply(module)) applied.Add(WorldSwitch);
        if (fixes.Contains(Tcp) && TcpFix.Apply(module)) applied.Add(Tcp);
        if (fixes.Contains(Teleport) && TeleportFix.Apply(module)) applied.Add(Teleport);
        if (fixes.Contains(Camera) && CameraFix.Apply(module)) applied.Add(Camera);
        return applied;
    }

    public static List<string> Apply(Il2Cpp il2cpp, IReadOnlyCollection<string> fixes, Action<string> log)
    {
        List<string> applied = [];
        if (fixes.Contains(WorldSwitch) && WorldSwitchFix.Apply(il2cpp)) applied.Add(WorldSwitch);
        if (fixes.Contains(Tcp) && TcpFix.Apply(il2cpp, log)) applied.Add(Tcp);
        if (fixes.Contains(Teleport) && TeleportFix.Apply(il2cpp, log)) applied.Add(Teleport);
        return applied;
    }

    static List<string> Managed(string input, string output, IReadOnlyCollection<string> fixes)
    {
        byte[] file = File.ReadAllBytes(input);
        if (UnityArchive.IsPlayerStream(file)) return Stream(file, output, fixes);
        string folder = Path.GetDirectoryName(Path.GetFullPath(input))!;
        var resolver = new DefaultAssemblyResolver();
        resolver.AddSearchDirectory(folder);
        using var module = ModuleDefinition.ReadModule(new MemoryStream(File.ReadAllBytes(input)), new ReaderParameters { AssemblyResolver = resolver });
        List<string> applied = Apply(module, fixes);
        string shimPath = Path.Combine(folder, Shim);
        if (fixes.Contains(Mods) && File.Exists(shimPath))
        {
            using ModuleDefinition shim = ModuleDefinition.ReadModule(shimPath);
            if (MonoPatch.Apply(module, shim).Count > 0) applied.Add(Mods);
        }
        if (applied.Count == 0) return applied;
        var fixedAssembly = new MemoryStream();
        module.Write(fixedAssembly);
        Save(output, fixedAssembly.ToArray());
        return applied;
    }

    static List<string> Stream(byte[] file, string output, IReadOnlyCollection<string> fixes)
    {
        PlayerStream stream = UnityArchive.ReadPlayerStream(file);
        List<ArchiveNode> nodes = [.. stream.Nodes];
        int index = nodes.FindIndex(node => Path.GetFileName(node.Name).Equals(GameCode, StringComparison.OrdinalIgnoreCase));
        if (index < 0) return [];
        string folder = Directory.CreateTempSubdirectory("okfix-").FullName;
        List<string> applied = [];
        try
        {
            foreach (ArchiveNode node in nodes.Where(node => node.Name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)))
                File.WriteAllBytes(Path.Combine(folder, Path.GetFileName(node.Name)), node.Data);
            using var resolver = new DefaultAssemblyResolver();
            resolver.AddSearchDirectory(folder);
            using var module = ModuleDefinition.ReadModule(new MemoryStream(nodes[index].Data), new ReaderParameters { AssemblyResolver = resolver });
            foreach (string fix in fixes.Where(fix => fix != Mods))
            {
                try
                {
                    applied.AddRange(Apply(module, [fix]));
                }
                catch (AssemblyResolutionException)
                {
                }
            }
            if (applied.Count == 0) return applied;
            var patched = new MemoryStream();
            module.Write(patched);
            nodes[index] = nodes[index] with { Data = patched.ToArray() };
        }
        finally
        {
            try
            {
                Directory.Delete(folder, true);
            }
            catch (IOException)
            {
            }
        }
        if (!Holds(output, nodes[index])) Save(output, UnityArchive.WritePlayerStream(stream, nodes));
        return applied;
    }

    static bool Holds(string output, ArchiveNode code)
    {
        try
        {
            return File.Exists(output) && UnityArchive.ReadPlayerStream(File.ReadAllBytes(output)).Nodes
                .Any(node => node.Name == code.Name && node.Data.AsSpan().SequenceEqual(code.Data));
        }
        catch (Exception error) when (error is InvalidDataException or NotSupportedException or IndexOutOfRangeException or ArgumentException)
        {
            return false;
        }
    }

    static List<string> Native(string input, string output, IReadOnlyCollection<string> fixes, Action<string> log)
    {
        if (!fixes.Any(fix => fix is WorldSwitch or Tcp or Teleport or Mods) || Metadata(output) is not string metadata) return [];
        Il2Cpp il2cpp;
        try
        {
            il2cpp = new Il2Cpp(File.ReadAllBytes(input), File.ReadAllBytes(metadata));
            il2cpp.Load(Il2Cpp.Game, Il2Cpp.Engine);
        }
        catch (Exception error) when (error is NotSupportedException or InvalidDataException)
        {
            log(error.Message);
            return [];
        }
        List<string> applied = Apply(il2cpp, fixes, log);
        byte[] binary = il2cpp.Binary;
        if (fixes.Contains(Mods) && Framework.Install(il2cpp, log) is { } installed)
        {
            binary = installed.Binary;
            applied.Add(Mods);
        }
        if (applied.Count > 0) Save(output, binary);
        return applied;
    }

    static void Save(string output, byte[] bytes)
    {
        if (File.Exists(output) && File.ReadAllBytes(output).AsSpan().SequenceEqual(bytes)) return;
        File.WriteAllBytes(output + ".tmp", bytes);
        File.Move(output + ".tmp", output, overwrite: true);
    }

    static string? Metadata(string gameAssembly) =>
        Directory.GetDirectories(Path.GetDirectoryName(Path.GetFullPath(gameAssembly))!, "*_Data")
            .Select(folder => Path.Combine(folder, "il2cpp_data", "Metadata", "global-metadata.dat"))
            .FirstOrDefault(File.Exists);
}
