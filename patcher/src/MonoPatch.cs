using Mono.Cecil;
using Mono.Cecil.Cil;

namespace OpenKogama.Mods.Patcher;

public static class MonoPatch
{
    public readonly record struct Toggle(string Flag, string Type, string Method, int? Return);

    public readonly record struct FloatSet(string Flag, string Type, string Method, string Param, string Arg, double Fallback);

    public static List<string> Install(string input, string shimPath, string output, IReadOnlyList<Toggle> toggles, IReadOnlyList<FloatSet> floats)
    {
        var resolver = new DefaultAssemblyResolver();
        resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(input)));
        using ModuleDefinition module = ModuleDefinition.ReadModule(input, new ReaderParameters { AssemblyResolver = resolver });
        using ModuleDefinition shim = ModuleDefinition.ReadModule(shimPath);

        TypeDefinition okmod = shim.GetType("OpenKogama.Mods.OkMod");
        MethodReference flagRef = module.ImportReference(okmod.Methods.First(m => m.Name == "Flag"));
        MethodReference paramRef = module.ImportReference(okmod.Methods.First(m => m.Name == "Param"));

        var image = new MonoImage(module);
        var applied = new List<string>();
        foreach (Toggle t in toggles)
        {
            if (image.Find(t.Type, t.Method, null) is not { HasBody: true } method) continue;
            Prefix(method, flagRef, t.Flag, t.Return);
            applied.Add($"toggle {t.Flag} -> {t.Type}::{t.Method}");
        }
        foreach (FloatSet f in floats)
        {
            if (image.Find(f.Type, f.Method, null) is not { HasBody: true } method) continue;
            if (method.Parameters.FirstOrDefault(p => p.Name == f.Arg) is not { } arg) continue;
            SetFloat(method, flagRef, paramRef, f, arg);
            applied.Add($"float {f.Flag} -> {f.Type}::{f.Method}({f.Arg})");
        }
        if (applied.Count > 0) module.Write(output);
        return applied;
    }

    static void SetFloat(MethodDefinition method, MethodReference flag, MethodReference param, FloatSet f, ParameterDefinition arg)
    {
        ILProcessor il = method.Body.GetILProcessor();
        Instruction first = method.Body.Instructions[0];
        il.InsertBefore(first, il.Create(OpCodes.Ldstr, f.Flag));
        il.InsertBefore(first, il.Create(OpCodes.Call, flag));
        il.InsertBefore(first, il.Create(OpCodes.Brfalse, first));
        il.InsertBefore(first, il.Create(OpCodes.Ldstr, f.Param));
        il.InsertBefore(first, il.Create(OpCodes.Ldc_R8, f.Fallback));
        il.InsertBefore(first, il.Create(OpCodes.Call, param));
        il.InsertBefore(first, il.Create(OpCodes.Conv_R4));
        il.InsertBefore(first, il.Create(OpCodes.Starg, arg));
    }

    static void Prefix(MethodDefinition method, MethodReference flag, string name, int? ret)
    {
        ILProcessor il = method.Body.GetILProcessor();
        Instruction first = method.Body.Instructions[0];
        il.InsertBefore(first, il.Create(OpCodes.Ldstr, name));
        il.InsertBefore(first, il.Create(OpCodes.Call, flag));
        il.InsertBefore(first, il.Create(OpCodes.Brfalse, first));
        if (ret is int value) il.InsertBefore(first, il.Create(OpCodes.Ldc_I4, value));
        il.InsertBefore(first, il.Create(OpCodes.Ret));
    }
}
