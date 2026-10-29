using Mono.Cecil;
using Mono.Cecil.Cil;

namespace OpenKogama.Mods.Patches;

public static class MonoPatch
{
    public readonly record struct Toggle(string Flag, string Type, string Method, int? Return);

    public readonly record struct FloatSet(string Flag, string Type, string Method, string Param, string Arg, double Fallback);

    public readonly record struct Hook(string Event, string Type, string Method);

    enum Kind { Skip, Signed, Unsigned, Bool, Long, Float, Double, Object, Pointer, Struct }

    sealed record Calls(MethodReference Arguments, MethodReference Event, MethodReference Address, MethodReference Bits, MethodReference Single,
        MethodReference DoubleBits, FieldReference Result);

    static readonly Dictionary<string, string> Aliases = new()
    {
        ["string"] = "String", ["float"] = "Single", ["double"] = "Double", ["int"] = "Int32", ["uint"] = "UInt32",
        ["long"] = "Int64", ["bool"] = "Boolean", ["byte"] = "Byte", ["short"] = "Int16", ["object"] = "Object",
    };

    public static List<string> Install(string input, string shimPath, string output, IReadOnlyList<Toggle> toggles, IReadOnlyList<FloatSet> floats,
        IReadOnlyList<Hook> hooks)
    {
        var resolver = new DefaultAssemblyResolver();
        resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(input)));
        using ModuleDefinition module = ModuleDefinition.ReadModule(input, new ReaderParameters { AssemblyResolver = resolver });
        using ModuleDefinition shim = ModuleDefinition.ReadModule(shimPath);
        List<string> applied = Apply(module, shim, toggles, floats, hooks);
        if (applied.Count > 0) module.Write(output);
        return applied;
    }

    public static List<string> Apply(ModuleDefinition module, ModuleDefinition shim) => Apply(module, shim,
        [.. Features.Toggles.Select(t => new Toggle(t.Flag, t.Type, t.Method, t.Ret))],
        [.. Features.Floats.Select(f => new FloatSet(f.Flag, f.Type, f.Method, f.Param, f.Arg, f.Fallback))],
        [.. Features.Hooks.Select(h => new Hook(h.Event, h.Type, h.Method))]);

    public static List<string> Apply(ModuleDefinition module, ModuleDefinition shim, IReadOnlyList<Toggle> toggles, IReadOnlyList<FloatSet> floats,
        IReadOnlyList<Hook> hooks)
    {
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
        var calls = new Calls(
            module.ImportReference(okmod.Methods.First(m => m.Name == "Arguments")),
            module.ImportReference(okmod.Methods.First(m => m.Name == "Event")),
            module.ImportReference(okmod.Methods.First(m => m.Name == "Address")),
            module.ImportReference(okmod.Methods.First(m => m.Name == "Bits")),
            module.ImportReference(okmod.Methods.First(m => m.Name == "Single")),
            DoubleBits(module),
            module.ImportReference(okmod.Fields.First(f => f.Name == "Result")));
        var hooked = new HashSet<string>();
        foreach (Hook h in hooks)
        {
            if (hooked.Contains(h.Event)) continue;
            if (Resolve(image, h.Type, h.Method) is not { HasBody: true } method || method.HasGenericParameters || method.DeclaringType.HasGenericParameters) continue;
            Event(method, calls, h.Event);
            hooked.Add(h.Event);
            applied.Add($"event {h.Event} -> {h.Type}::{h.Method}");
        }
        return applied;
    }

    static MethodReference DoubleBits(ModuleDefinition module)
    {
        var converter = new TypeReference("System", "BitConverter", module, module.TypeSystem.CoreLibrary);
        var bits = new MethodReference("DoubleToInt64Bits", module.TypeSystem.Int64, converter);
        bits.Parameters.Add(new ParameterDefinition(module.TypeSystem.Double));
        return bits;
    }

    static MethodDefinition? Resolve(MonoImage image, string type, string method)
    {
        int open = method.IndexOf('(');
        if (open < 0) return image.Find(type, method, null);
        if (image.Type(type) is not { } owner) return null;
        string name = method[..open];
        string[] wanted = [.. method[(open + 1)..^1].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).Select(t => Aliases.GetValueOrDefault(t, t))];
        List<MethodDefinition> matches = [.. owner.Methods.Where(m => m.Name == name && m.Parameters.Select(p => p.ParameterType.Name).SequenceEqual(wanted))];
        return matches.Count == 1 ? matches[0] : null;
    }

    static Kind KindOf(TypeReference type, out OpCode convert)
    {
        convert = OpCodes.Conv_I8;
        if (type.IsByReference || type.IsPointer) return Kind.Pointer;
        if (type.IsGenericParameter) return Kind.Skip;
        switch (type.MetadataType)
        {
            case MetadataType.Boolean: return Kind.Bool;
            case MetadataType.SByte: convert = OpCodes.Conv_I1; return Kind.Signed;
            case MetadataType.Int16: convert = OpCodes.Conv_I2; return Kind.Signed;
            case MetadataType.Int32: convert = OpCodes.Conv_I4; return Kind.Signed;
            case MetadataType.Byte: convert = OpCodes.Conv_U1; return Kind.Unsigned;
            case MetadataType.Char:
            case MetadataType.UInt16: convert = OpCodes.Conv_U2; return Kind.Unsigned;
            case MetadataType.UInt32: convert = OpCodes.Conv_U4; return Kind.Unsigned;
            case MetadataType.Int64:
            case MetadataType.UInt64: return Kind.Long;
            case MetadataType.Single: return Kind.Float;
            case MetadataType.Double: return Kind.Double;
            case MetadataType.IntPtr:
            case MetadataType.UIntPtr: return Kind.Pointer;
            case MetadataType.String:
            case MetadataType.Object:
            case MetadataType.Class:
            case MetadataType.Array: return Kind.Object;
        }
        TypeDefinition? definition;
        try { definition = type.Resolve(); }
        catch (AssemblyResolutionException) { definition = null; }
        if (definition is null) return type.IsValueType ? Kind.Struct : Kind.Object;
        if (definition.IsEnum) return KindOf(definition.Fields.First(field => !field.IsStatic).FieldType, out convert);
        return definition.IsValueType ? Kind.Struct : Kind.Object;
    }

    static void Event(MethodDefinition method, Calls calls, string name)
    {
        ModuleDefinition module = method.Module;
        MethodBody body = method.Body;
        ILProcessor il = body.GetILProcessor();
        Instruction first = body.Instructions[0];
        var args = new VariableDefinition(new ArrayType(module.TypeSystem.Int64));
        body.Variables.Add(args);
        body.InitLocals = true;

        var code = new List<Instruction> { il.Create(OpCodes.Call, calls.Arguments), il.Create(OpCodes.Stloc, args) };
        void Store(int slot, params Instruction[] value)
        {
            code.Add(il.Create(OpCodes.Ldloc, args));
            code.Add(il.Create(OpCodes.Ldc_I4, slot));
            code.AddRange(value);
            code.Add(il.Create(OpCodes.Stelem_I8));
        }

        int position = 0;
        if (method.HasThis)
        {
            if (!method.DeclaringType.IsValueType) Store(0, il.Create(OpCodes.Ldarg_0), il.Create(OpCodes.Call, calls.Address));
            position = 1;
        }
        var back = new List<Instruction>();
        foreach (ParameterDefinition parameter in method.Parameters)
        {
            int at = position++;
            if (at >= 8) break;
            int whole = at < 4 ? at : 10 + at, real = at < 4 ? 4 + at : 10 + at;
            Kind kind = KindOf(parameter.ParameterType, out OpCode convert);
            switch (kind)
            {
                case Kind.Signed:
                case Kind.Unsigned:
                    Store(whole, il.Create(OpCodes.Ldarg, parameter), il.Create(kind == Kind.Signed ? OpCodes.Conv_I8 : OpCodes.Conv_U8));
                    back.AddRange([il.Create(OpCodes.Ldloc, args), il.Create(OpCodes.Ldc_I4, whole), il.Create(OpCodes.Ldelem_I8), il.Create(convert), il.Create(OpCodes.Starg, parameter)]);
                    break;
                case Kind.Bool:
                    Store(whole, il.Create(OpCodes.Ldarg, parameter), il.Create(OpCodes.Conv_U8));
                    back.AddRange([il.Create(OpCodes.Ldloc, args), il.Create(OpCodes.Ldc_I4, whole), il.Create(OpCodes.Ldelem_I8), il.Create(OpCodes.Ldc_I8, 0L),
                        il.Create(OpCodes.Cgt_Un), il.Create(OpCodes.Starg, parameter)]);
                    break;
                case Kind.Long:
                    Store(whole, il.Create(OpCodes.Ldarg, parameter));
                    back.AddRange([il.Create(OpCodes.Ldloc, args), il.Create(OpCodes.Ldc_I4, whole), il.Create(OpCodes.Ldelem_I8), il.Create(OpCodes.Starg, parameter)]);
                    break;
                case Kind.Float:
                    Store(real, il.Create(OpCodes.Ldarg, parameter), il.Create(OpCodes.Call, calls.Bits));
                    back.AddRange([il.Create(OpCodes.Ldloc, args), il.Create(OpCodes.Ldc_I4, real), il.Create(OpCodes.Ldelem_I8), il.Create(OpCodes.Call, calls.Single),
                        il.Create(OpCodes.Starg, parameter)]);
                    break;
                case Kind.Double:
                    Store(real, il.Create(OpCodes.Ldarg, parameter), il.Create(OpCodes.Call, calls.DoubleBits));
                    break;
                case Kind.Object:
                    Store(whole, il.Create(OpCodes.Ldarg, parameter), il.Create(OpCodes.Call, calls.Address));
                    break;
                case Kind.Pointer:
                    Store(whole, il.Create(OpCodes.Ldarg, parameter), il.Create(OpCodes.Conv_U), il.Create(OpCodes.Conv_U8));
                    break;
                case Kind.Struct:
                    Store(whole, il.Create(OpCodes.Ldarga, parameter), il.Create(OpCodes.Conv_U), il.Create(OpCodes.Conv_U8));
                    break;
            }
        }
        code.Add(il.Create(OpCodes.Ldstr, name));
        code.Add(il.Create(OpCodes.Ldloc, args));
        code.Add(il.Create(OpCodes.Call, calls.Event));
        code.Add(il.Create(OpCodes.Brfalse, back.Count > 0 ? back[0] : first));
        code.AddRange(Cancelled(method, calls, il));
        code.AddRange(back);
        foreach (Instruction instruction in code) il.InsertBefore(first, instruction);
    }

    static IEnumerable<Instruction> Cancelled(MethodDefinition method, Calls calls, ILProcessor il)
    {
        TypeReference type = method.ReturnType;
        if (type.MetadataType == MetadataType.Void) return [il.Create(OpCodes.Ret)];
        Kind kind = KindOf(type, out _);
        Instruction result = il.Create(OpCodes.Ldsfld, calls.Result);
        switch (kind)
        {
            case Kind.Bool: return [result, il.Create(OpCodes.Ldc_I8, 0L), il.Create(OpCodes.Cgt_Un), il.Create(OpCodes.Ret)];
            case Kind.Signed:
            case Kind.Unsigned: return [result, il.Create(OpCodes.Conv_I4), il.Create(OpCodes.Ret)];
            case Kind.Long: return [result, il.Create(OpCodes.Ret)];
            case Kind.Float: return [result, il.Create(OpCodes.Conv_R4), il.Create(OpCodes.Ret)];
            case Kind.Double: return [result, il.Create(OpCodes.Conv_R8), il.Create(OpCodes.Ret)];
            case Kind.Object: return [il.Create(OpCodes.Ldnull), il.Create(OpCodes.Ret)];
        }
        var empty = new VariableDefinition(type);
        method.Body.Variables.Add(empty);
        return [il.Create(OpCodes.Ldloca, empty), il.Create(OpCodes.Initobj, type), il.Create(OpCodes.Ldloc, empty), il.Create(OpCodes.Ret)];
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
