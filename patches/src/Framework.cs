namespace OpenKogama.Mods.Patches;

public static class Framework
{
    public const string Library = "okmod.dll";
    const int MaxParameters = 16;
    static readonly string[] Exports = ["okmod_flag", "okmod_paramf", "okmod_gate", "okmod_pump", "okmod_event"];

    public sealed record Installed(byte[] Binary, List<Hook.Site> Sites);

    public static Installed? Install(Il2Cpp il2cpp, Action<string> log)
    {
        uint? Resolve(string type, string method)
        {
            int open = method.IndexOf('(');
            if (open >= 0)
                return il2cpp.Find(type, method[..open], method[(open + 1)..^1].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
            return Enumerable.Range(0, MaxParameters).Select(parameters => il2cpp.Find(type, method, parameters)).FirstOrDefault(rva => rva is not null);
        }

        var sites = new List<Hook.Site>();
        foreach (var (flag, type, method, ret) in Features.Toggles)
        {
            if (Resolve(type, method) is uint rva && !il2cpp.Shared(rva)) sites.Add(new Hook.Site(Hook.Kind.Toggle, flag, rva, (byte)(ret ?? 0)));
            else log($"toggle {type}::{method} not found or shared, skipped");
        }
        var hooked = new HashSet<string>();
        foreach (var (name, type, method) in Features.Hooks)
        {
            if (hooked.Contains(name)) continue;
            if (Resolve(type, method) is uint rva && !il2cpp.Shared(rva))
            {
                sites.Add(new Hook.Site(Hook.Kind.Event, name, rva, 0));
                hooked.Add(name);
            }
            else log($"hook {type}::{method} not found or shared, skipped");
        }
        if (sites.Count == 0) return null;

        var pe = new Pe(il2cpp.Binary);
        Dictionary<string, uint> iat = Loader.InjectImport(pe, Library, Exports);
        List<Hook.Site> applied = Hook.Apply(pe, iat["okmod_flag"], iat["okmod_event"], sites, log);
        return new Installed(pe.Bytes, applied);
    }
}
