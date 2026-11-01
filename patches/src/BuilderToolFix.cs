using Mono.Cecil;
using Mono.Cecil.Cil;

namespace OpenKogama.Mods.Patches;

public static class BuilderToolFix
{
    const string Remote = "MVBuildModeAvatarRemote";
    const string Visibility = "SetLaserPointerVisibility";
    const string GameObject = "UnityEngine.GameObject";

    public static bool Apply(ModuleDefinition module)
        => ApplyTool(module) | ApplyPendingName(module);

    static bool ApplyTool(ModuleDefinition module)
    {
        if (module.GetType(Remote)?.Methods.FirstOrDefault(method => method.Name == Visibility
            && method.Parameters is [{ ParameterType.MetadataType: MetadataType.Boolean }] && method.HasBody) is not { } method)
            return false;
        var calls = method.Body.Instructions.Where(instruction => instruction.OpCode == OpCodes.Callvirt
            && instruction.Operand is MethodReference { Name: "SetActive", DeclaringType.FullName: GameObject }).ToArray();
        if (calls is not [var call] || call.Previous.OpCode != OpCodes.Ldc_I4_0) return false;
        call.Previous.OpCode = OpCodes.Ldarg_1;
        call.Previous.Operand = null;
        return true;
    }

    public static bool Apply(Il2Cpp image, Action<string> log)
        => ApplyTool(image, log) | ApplyPendingName(image, log);

    static bool ApplyTool(Il2Cpp image, Action<string> log)
    {
        if (!image.Is64Bit || image.Find(Remote, Visibility, 1) is not uint method || image.Shared(method)
            || image.FindEngine(GameObject, "SetActive", 1) is not uint setActive) return false;
        int start = image.Offset(method), size = (int)(image.End(method) - method);
        var sites = new List<int>();
        for (int at = start + 8; at + 5 <= start + size; ++at)
            if (image.Binary[at] == 0xE8 && method + (at - start) + 5 + BitConverter.ToInt32(image.Binary, at + 1) == setActive
                && image.Binary.AsSpan(at - 8, 8).SequenceEqual((byte[])[0x45, 0x33, 0xC0, 0x33, 0xD2, 0x48, 0x8B, 0xC8]))
                sites.Add(at - 5);
        if (sites is not [int site])
        {
            log($"{Fixes.BuilderTool}: found {sites.Count} supported visibility calls, left as it is");
            return false;
        }
        image.Binary[site] = 0xB2;
        image.Binary[site + 1] = 0x01;
        return true;
    }

    static bool ApplyPendingName(ModuleDefinition module)
    {
        if (module.GetType("AvatarUIHandlerRemote")?.Methods.FirstOrDefault(method => method.Name == "UpdateNameTag"
            && !method.HasParameters && method.HasBody) is not { } method) return false;
        var calls = method.Body.Instructions.Where(instruction => instruction.Operand is MethodReference
            { Name: "ContainsKey", DeclaringType.FullName: "MVPlayerContainer" }).ToArray();
        if (calls is not [var call] || call.Next is not { } branch) return false;
        bool whenTrue = branch.OpCode == OpCodes.Brtrue || branch.OpCode == OpCodes.Brtrue_S;
        bool whenFalse = branch.OpCode == OpCodes.Brfalse || branch.OpCode == OpCodes.Brfalse_S;
        if (!whenTrue && !whenFalse) return false;
        method.Body.GetILProcessor().InsertBefore(branch, Instruction.Create(OpCodes.Pop));
        branch.OpCode = whenTrue ? OpCodes.Br : OpCodes.Nop;
        if (whenFalse) branch.Operand = null;
        return true;
    }

    static bool ApplyPendingName(Il2Cpp image, Action<string> log)
    {
        if (!image.Is64Bit || image.Find("AvatarUIHandlerRemote", "UpdateNameTag", 0) is not uint method
            || image.Shared(method) || image.Find("MVPlayerContainer", "ContainsKey", 1) is not uint contains) return false;
        int start = image.Offset(method), size = (int)(image.End(method) - method);
        var sites = new List<int>();
        for (int at = start; at + 13 <= start + size; ++at)
            if (image.Binary[at] == 0xE8 && method + (at - start) + 5 + BitConverter.ToInt32(image.Binary, at + 1) == contains
                && image.Binary.AsSpan(at + 5, 4).SequenceEqual((byte[])[0x84, 0xC0, 0x0F, 0x84]))
                sites.Add(at + 7);
        if (sites is not [int site])
        {
            log($"{Fixes.BuilderTool}: found {sites.Count} pending-name guards, left as it is");
            return false;
        }
        image.Binary.AsSpan(site, 6).Fill(0x90);
        return true;
    }
}
