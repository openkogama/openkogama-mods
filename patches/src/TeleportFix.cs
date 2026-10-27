using Mono.Cecil;
using Mono.Cecil.Cil;
using Mono.Cecil.Rocks;

namespace OpenKogama.Mods.Patches;

public static class TeleportFix
{
    const string NetworkGame = "MVNetworkGame";
    const string LocalAvatar = "MVAvatarLocal";
    const string TransformPackage = "NetworkTransformPackage";
    const string TeleportHelper = "OpenKogamaTeleport";
    const string BuildAvatar = "MVBuildModeAvatarLocal";
    const string WorldObjectClient = "MVWorldObjectClient";
    const byte PackageType64 = 0x30;
    const byte Position64 = 0x10;
    const byte Rotation64 = 0x1C;
    const byte ClassName64 = 0x10;
    const byte PackageType32 = 0x28;
    const byte Position32 = 0x08;
    const byte Rotation32 = 0x14;
    const byte ClassName32 = 0x08;
    const byte ByteStore = 0x88;
    const byte Equal = 0x84;
    const byte NotEqual = 0x85;
    static readonly string[] Caves = ["OnDrawGizmos", "OnDrawGizmosSelected", "OnValidate"];
    const string Controller = "MVGameControllerBase";
    const byte Call = 0xE8;
    const byte Jump = 0xE9;
    const byte Return = 0xC3;

    public static bool Apply(ModuleDefinition module)
    {
        if (module.GetType(NetworkGame) is not { } game
            || game.Methods.FirstOrDefault(method => method.Name == "OnUpdateWorldObjectEvent" && method.Parameters.Count == 1 && method.HasBody) is not { } handler
            || game.Methods.FirstOrDefault(method => method.Name == "get_WorldObjectClientManager" && !method.HasParameters) is not { } manager
            || module.GetType("MVWorldObjectClientManager")?.Methods.FirstOrDefault(method =>
                method.Name == "GetWorldObjectClient" && !method.HasGenericParameters && method.Parameters is [{ ParameterType.MetadataType: MetadataType.Int32 }]) is not { } find
            || module.GetType(LocalAvatar) is not { } avatar
            || avatar.Methods.Any(method => method.Name == TeleportHelper)
            || avatar.Methods.FirstOrDefault(method => method.Name == "SetTransform" && method.Parameters.Count == 2 && !method.IsStatic) is not { } setTransform
            || module.GetType(TransformPackage) is not { } package
            || package.Fields.FirstOrDefault(field => field.Name == "packageType") is not { } kind
            || package.Fields.FirstOrDefault(field => field.Name == "position") is not { } position
            || package.Fields.FirstOrDefault(field => field.Name == "rotation") is not { } rotation
            || handler.Body.Variables.FirstOrDefault(variable => variable.VariableType.FullName == TransformPackage) is not { } built
            || handler.Body.Variables.FirstOrDefault(variable => variable.VariableType.MetadataType == MetadataType.Int32) is not { } id
            || handler.Body.Instructions.FirstOrDefault(instruction => instruction.OpCode == OpCodes.Stfld && instruction.Operand is FieldReference { Name: "packageType" } field
                && field.DeclaringType.FullName == TransformPackage) is not { Next: { } after })
            return false;
        MethodDefinition? entering = Inherited(avatar, method => method.Name == "get_IsEnteringVehicle" && !method.HasParameters);
        MethodDefinition? seated = Inherited(avatar, method => method.Name == "get_IsSeated" && !method.HasParameters);
        MethodDefinition? leave = avatar.Methods.FirstOrDefault(method => method.Name == "LeaveVehicle" && !method.IsStatic
            && method.Parameters is [] or [{ ParameterType.MetadataType: MetadataType.Boolean }]);
        TypeDefinition? builder = module.GetType(BuildAvatar);
        TypeDefinition? client = module.GetType(WorldObjectClient);
        MethodDefinition? moveTo = client?.Methods.FirstOrDefault(method => method.Name == "set_WorldPosition" && method.Parameters.Count == 1);
        MethodDefinition? turnTo = client?.Methods.FirstOrDefault(method => method.Name == "set_WorldRotation" && method.Parameters.Count == 1);

        var helper = new MethodDefinition(TeleportHelper, MethodAttributes.Public | MethodAttributes.Static | MethodAttributes.HideBySig, module.TypeSystem.Boolean);
        helper.Parameters.Add(new ParameterDefinition("manager", ParameterAttributes.None, manager.ReturnType));
        helper.Parameters.Add(new ParameterDefinition("id", ParameterAttributes.None, module.TypeSystem.Int32));
        helper.Parameters.Add(new ParameterDefinition("package", ParameterAttributes.None, package));
        var found = new VariableDefinition(find.ReturnType);
        var local = new VariableDefinition(avatar);
        helper.Body.Variables.Add(found);
        helper.Body.Variables.Add(local);
        ILProcessor il = helper.Body.GetILProcessor();
        Instruction refuse = il.Create(OpCodes.Ldc_I4_0);
        Instruction building = il.Create(OpCodes.Nop);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Ldfld, kind);
        il.Emit(OpCodes.Brtrue, refuse);
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Brfalse, refuse);
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Ldarg_1);
        il.Emit(OpCodes.Callvirt, find);
        il.Emit(OpCodes.Stloc, found);
        il.Emit(OpCodes.Ldloc, found);
        il.Emit(OpCodes.Isinst, avatar);
        il.Emit(OpCodes.Stloc, local);
        il.Emit(OpCodes.Ldloc, local);
        il.Emit(OpCodes.Brfalse, building);
        if (entering is not null)
        {
            il.Emit(OpCodes.Ldloc, local);
            il.Emit(OpCodes.Callvirt, entering);
            il.Emit(OpCodes.Brtrue, refuse);
        }
        if (seated is not null && leave is not null)
        {
            Instruction standing = il.Create(OpCodes.Nop);
            il.Emit(OpCodes.Ldloc, local);
            il.Emit(OpCodes.Callvirt, seated);
            il.Emit(OpCodes.Brfalse, standing);
            il.Emit(OpCodes.Ldloc, local);
            if (leave.HasParameters) il.Emit(OpCodes.Ldc_I4_0);
            il.Emit(OpCodes.Callvirt, leave);
            il.Append(standing);
        }
        il.Emit(OpCodes.Ldloc, local);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Ldfld, position);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Ldfld, rotation);
        il.Emit(OpCodes.Call, setTransform);
        il.Emit(OpCodes.Ldc_I4_1);
        il.Emit(OpCodes.Ret);
        il.Append(building);
        if (builder is not null && moveTo is not null && turnTo is not null)
        {
            il.Emit(OpCodes.Ldloc, found);
            il.Emit(OpCodes.Isinst, builder);
            il.Emit(OpCodes.Brfalse, refuse);
            il.Emit(OpCodes.Ldloc, found);
            il.Emit(OpCodes.Ldarg_2);
            il.Emit(OpCodes.Ldfld, position);
            il.Emit(OpCodes.Callvirt, moveTo);
            il.Emit(OpCodes.Ldloc, found);
            il.Emit(OpCodes.Ldarg_2);
            il.Emit(OpCodes.Ldfld, rotation);
            il.Emit(OpCodes.Callvirt, turnTo);
            il.Emit(OpCodes.Ldc_I4_1);
            il.Emit(OpCodes.Ret);
        }
        il.Append(refuse);
        il.Emit(OpCodes.Ret);
        avatar.Methods.Add(helper);

        handler.Body.SimplifyMacros();
        ILProcessor patch = handler.Body.GetILProcessor();
        patch.InsertBefore(after, patch.Create(OpCodes.Ldarg_0));
        patch.InsertBefore(after, patch.Create(OpCodes.Call, manager));
        patch.InsertBefore(after, patch.Create(OpCodes.Ldloc, id));
        patch.InsertBefore(after, patch.Create(OpCodes.Ldloc, built));
        patch.InsertBefore(after, patch.Create(OpCodes.Call, helper));
        patch.InsertBefore(after, patch.Create(OpCodes.Brfalse, after));
        patch.InsertBefore(after, patch.Create(OpCodes.Ret));
        handler.Body.OptimizeMacros();
        return true;
    }

    static MethodDefinition? Inherited(TypeDefinition type, Func<MethodDefinition, bool> match)
    {
        for (TypeDefinition? at = type; at is not null; at = at.BaseType as TypeDefinition)
            if (at.Methods.FirstOrDefault(match) is { } found) return found;
        return null;
    }

    sealed record TeleportTargets(uint Manager, uint Find, uint SetTransform, uint MoveTo, uint TurnTo, uint Add, uint? Entering, uint? Seated, uint? Leave);

    public static bool Apply(Il2Cpp il2cpp, Action<string> log)
    {
        if (il2cpp.Find(NetworkGame, "OnUpdateWorldObjectEvent", 1) is not uint handler
            || il2cpp.Find("TransformNetworkManager", "AddTransformPackage", 2) is not uint add
            || il2cpp.Find(Controller, "get_WOCM", 0) is not uint manager
            || il2cpp.Find("MVWorldObjectClientManager", "GetWorldObjectClient", 1) is not uint find
            || il2cpp.Find(LocalAvatar, "SetTransform", 2) is not uint setTransform
            || il2cpp.Find(WorldObjectClient, "set_WorldPosition", 1) is not uint moveTo
            || il2cpp.Find(WorldObjectClient, "set_WorldRotation", 1) is not uint turnTo
            || il2cpp.Shared(handler))
            return false;

        bool wide = il2cpp.Is64Bit;
        byte[] image = il2cpp.Binary;
        int start = il2cpp.Offset(handler);
        byte typeAt = wide ? PackageType64 : PackageType32;
        List<int> sites = [];
        bool storesType = false;
        for (int at = start; at + 5 <= start + (il2cpp.End(handler) - handler); at++)
        {
            if (image[at] is Call or Jump && handler + (at - start) + 5 + BitConverter.ToInt32(image, at + 1) == add) sites.Add(at);
            if (image[at] == ByteStore && (image[at + 1] & 0xC0) == 0x40 && (image[at + 1] & 7) != 4 && image[at + 2] == typeAt) storesType = true;
        }
        if (sites is not [int site] || !storesType)
        {
            log($"{Fixes.Teleport}: found the transform queue {sites.Count} times, left as it is");
            return false;
        }

        var targets = new TeleportTargets(manager, find, setTransform, moveTo, turnTo, add,
            il2cpp.Find(LocalAvatar, "get_IsEnteringVehicle", 0),
            il2cpp.Find("MVAvatar", "get_IsSeated", 0) ?? il2cpp.Find(LocalAvatar, "get_IsSeated", 0),
            il2cpp.Find(LocalAvatar, "LeaveVehicle", 1));
        uint? leaving = null;
        if (targets.Seated is not null && targets.Leave is not null
            && Cave(il2cpp, TeleportCode(wide, 0, targets, 0).Count) is uint main
            && Cave(il2cpp, VehicleCode(wide, 0, targets).Count, main) is uint helper)
        {
            leaving = helper;
            List<byte> leave = VehicleCode(wide, helper, targets);
            leave.CopyTo(image, il2cpp.Offset(helper));
            il2cpp.Unrelocate(helper, leave.Count);
        }
        int size = TeleportCode(wide, 0, targets, leaving).Count;
        if (Cave(il2cpp, size, leaving) is not uint cave)
        {
            log($"{Fixes.Teleport}: no unused method has room for {size} bytes, left as it is");
            return false;
        }
        List<byte> code = TeleportCode(wide, cave, targets, leaving);
        code.CopyTo(image, il2cpp.Offset(cave));
        il2cpp.Unrelocate(cave, code.Count);
        uint call = (uint)(handler + (site - start));
        BitConverter.TryWriteBytes(image.AsSpan(site + 1), (int)(cave - (call + 5)));
        il2cpp.Unrelocate(call + 1, 4);
        return true;
    }

    static uint? Cave(Il2Cpp il2cpp, int size, uint? taken = null)
    {
        HashSet<uint> called = il2cpp.CallTargets();
        return Caves.SelectMany(name => il2cpp.Named(name, 0))
            .Where(method => method.Rva != taken && !il2cpp.Shared(method.Rva) && !called.Contains(method.Rva) && il2cpp.End(method.Rva) - method.Rva >= size)
            .OrderBy(method => method.Type, StringComparer.Ordinal)
            .Select(method => (uint?)method.Rva)
            .FirstOrDefault();
    }

    static List<byte> TeleportCode(bool wide, uint cave, TeleportTargets targets, uint? leaving)
    {
        var code = new List<byte>();
        var jumps = new List<(int At, string Label)>();
        var labels = new Dictionary<string, int>();
        const string Refused = "refused", Building = "building", Done = "done";

        void Relative(byte opcode, uint target)
        {
            code.Add(opcode);
            code.AddRange(BitConverter.GetBytes((int)(target - (cave + code.Count + 4))));
        }

        void Branch(byte[] opcode, string label)
        {
            code.AddRange(opcode);
            jumps.Add((code.Count, label));
            code.AddRange([0, 0, 0, 0]);
        }

        void Named(string name, string otherwise)
        {
            byte[] text = System.Text.Encoding.ASCII.GetBytes(name + "\0");
            for (int at = 0; at < text.Length;)
            {
                int left = text.Length - at;
                if (wide && left >= 8)
                {
                    code.AddRange([0x48, 0xBA, .. text[at..(at + 8)], 0x48, 0x39, 0x51, (byte)at]);
                    at += 8;
                }
                else if (left >= 4)
                {
                    code.AddRange([0x81, 0x79, (byte)at, .. text[at..(at + 4)]]);
                    at += 4;
                }
                else if (left >= 2)
                {
                    code.AddRange([0x66, 0x81, 0x79, (byte)at, .. text[at..(at + 2)]]);
                    at += 2;
                }
                else
                {
                    code.AddRange([0x80, 0x79, (byte)at, text[at]]);
                    at += 1;
                }
                Branch([0x0F, NotEqual], otherwise);
            }
        }

        if (wide)
        {
            code.AddRange([0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xCB, 0x89, 0xD6, 0x4C, 0x89, 0xC7, 0x4D, 0x89, 0xCC]);
            code.AddRange([0x80, 0x7F, PackageType64, 0x00]);
            Branch([0x0F, NotEqual], Refused);
            code.AddRange([0x31, 0xC9]);
            Relative(Call, targets.Manager);
            code.AddRange([0x48, 0x85, 0xC0]);
            Branch([0x0F, Equal], Refused);
            code.AddRange([0x48, 0x89, 0xC1, 0x89, 0xF2, 0x45, 0x31, 0xC0]);
            Relative(Call, targets.Find);
            code.AddRange([0x48, 0x85, 0xC0]);
            Branch([0x0F, Equal], Refused);
            code.AddRange([0x48, 0x8B, 0x08, 0x48, 0x8B, 0x49, ClassName64]);
            Named(LocalAvatar, Building);
            if (leaving is uint vehicle)
            {
                code.AddRange([0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x89, 0xC1]);
                Relative(Call, vehicle);
                code.AddRange([0x84, 0xC0]);
                Branch([0x0F, NotEqual], Refused);
                code.AddRange([0x48, 0x8B, 0x44, 0x24, 0x20]);
            }
            code.AddRange([0x48, 0x89, 0xC1, 0x48, 0x8D, 0x57, Position64, 0x4C, 0x8D, 0x47, Rotation64, 0x45, 0x31, 0xC9]);
            Relative(Call, targets.SetTransform);
            Branch([Jump], Done);
            labels[Building] = code.Count;
            Named(BuildAvatar, Refused);
            code.AddRange([0x48, 0x89, 0xC3, 0x48, 0x89, 0xD9, 0x48, 0x8D, 0x57, Position64, 0x45, 0x31, 0xC0]);
            Relative(Call, targets.MoveTo);
            code.AddRange([0x48, 0x89, 0xD9, 0x48, 0x8D, 0x57, Rotation64, 0x45, 0x31, 0xC0]);
            Relative(Call, targets.TurnTo);
            labels[Done] = code.Count;
            code.AddRange([0x48, 0x83, 0xC4, 0x28, 0x41, 0x5C, 0x5F, 0x5E, 0x5B, Return]);
            labels[Refused] = code.Count;
            code.AddRange([0x48, 0x89, 0xD9, 0x89, 0xF2, 0x49, 0x89, 0xF8, 0x4D, 0x89, 0xE1, 0x48, 0x83, 0xC4, 0x28, 0x41, 0x5C, 0x5F, 0x5E, 0x5B]);
        }
        else
        {
            code.AddRange([0x53, 0x56, 0x57, 0x8B, 0x74, 0x24, 0x14, 0x8B, 0x7C, 0x24, 0x18]);
            code.AddRange([0x80, 0x7F, PackageType32, 0x00]);
            Branch([0x0F, NotEqual], Refused);
            code.AddRange([0x6A, 0x00]);
            Relative(Call, targets.Manager);
            code.AddRange([0x83, 0xC4, 0x04, 0x85, 0xC0]);
            Branch([0x0F, Equal], Refused);
            code.AddRange([0x6A, 0x00, 0x56, 0x50]);
            Relative(Call, targets.Find);
            code.AddRange([0x83, 0xC4, 0x0C, 0x85, 0xC0]);
            Branch([0x0F, Equal], Refused);
            code.AddRange([0x8B, 0x08, 0x8B, 0x49, ClassName32]);
            Named(LocalAvatar, Building);
            if (leaving is uint vehicle)
            {
                code.AddRange([0x89, 0xC3, 0x53]);
                Relative(Call, vehicle);
                code.AddRange([0x83, 0xC4, 0x04, 0x84, 0xC0]);
                Branch([0x0F, NotEqual], Refused);
                code.AddRange([0x89, 0xD8]);
            }
            code.AddRange([0x6A, 0x00]);
            for (int field = 3; field >= 0; field--) code.AddRange([0xFF, 0x77, (byte)(Rotation32 + field * 4)]);
            for (int field = 2; field >= 0; field--) code.AddRange([0xFF, 0x77, (byte)(Position32 + field * 4)]);
            code.Add(0x50);
            Relative(Call, targets.SetTransform);
            code.AddRange([0x83, 0xC4, 0x24]);
            Branch([Jump], Done);
            labels[Building] = code.Count;
            Named(BuildAvatar, Refused);
            code.AddRange([0x89, 0xC3, 0x6A, 0x00]);
            for (int field = 2; field >= 0; field--) code.AddRange([0xFF, 0x77, (byte)(Position32 + field * 4)]);
            code.Add(0x53);
            Relative(Call, targets.MoveTo);
            code.AddRange([0x83, 0xC4, 0x14, 0x6A, 0x00]);
            for (int field = 3; field >= 0; field--) code.AddRange([0xFF, 0x77, (byte)(Rotation32 + field * 4)]);
            code.Add(0x53);
            Relative(Call, targets.TurnTo);
            code.AddRange([0x83, 0xC4, 0x18]);
            labels[Done] = code.Count;
            code.AddRange([0x5F, 0x5E, 0x5B, Return]);
            labels[Refused] = code.Count;
            code.AddRange([0x5F, 0x5E, 0x5B]);
        }
        Relative(Jump, targets.Add);
        foreach ((int at, string label) in jumps)
        {
            byte[] distance = BitConverter.GetBytes(labels[label] - (at + 4));
            for (int i = 0; i < 4; i++) code[at + i] = distance[i];
        }
        return code;
    }

    static List<byte> VehicleCode(bool wide, uint at, TeleportTargets targets)
    {
        var code = new List<byte>();
        var jumps = new List<(int At, string Label)>();
        var labels = new Dictionary<string, int>();
        const string Busy = "busy", Free = "free", End = "end";

        void Relative(byte opcode, uint target)
        {
            code.Add(opcode);
            code.AddRange(BitConverter.GetBytes((int)(target - (at + code.Count + 4))));
        }

        void Branch(byte[] opcode, string label)
        {
            code.AddRange(opcode);
            jumps.Add((code.Count, label));
            code.AddRange([0, 0, 0, 0]);
        }

        if (wide)
        {
            code.AddRange([0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCB]);
            if (targets.Entering is uint entering)
            {
                code.AddRange([0x48, 0x89, 0xD9, 0x31, 0xD2]);
                Relative(Call, entering);
                code.AddRange([0x84, 0xC0]);
                Branch([0x0F, NotEqual], Busy);
            }
            code.AddRange([0x48, 0x89, 0xD9, 0x31, 0xD2]);
            Relative(Call, targets.Seated!.Value);
            code.AddRange([0x84, 0xC0]);
            Branch([0x0F, Equal], Free);
            code.AddRange([0x48, 0x89, 0xD9, 0x31, 0xD2, 0x45, 0x31, 0xC0]);
            Relative(Call, targets.Leave!.Value);
            labels[Free] = code.Count;
            code.AddRange([0x31, 0xC0]);
            Branch([Jump], End);
            labels[Busy] = code.Count;
            code.AddRange([0xB0, 0x01]);
            labels[End] = code.Count;
            code.AddRange([0x48, 0x83, 0xC4, 0x20, 0x5B, Return]);
        }
        else
        {
            code.AddRange([0x53, 0x8B, 0x5C, 0x24, 0x08]);
            if (targets.Entering is uint entering)
            {
                code.AddRange([0x6A, 0x00, 0x53]);
                Relative(Call, entering);
                code.AddRange([0x83, 0xC4, 0x08, 0x84, 0xC0]);
                Branch([0x0F, NotEqual], Busy);
            }
            code.AddRange([0x6A, 0x00, 0x53]);
            Relative(Call, targets.Seated!.Value);
            code.AddRange([0x83, 0xC4, 0x08, 0x84, 0xC0]);
            Branch([0x0F, Equal], Free);
            code.AddRange([0x6A, 0x00, 0x6A, 0x00, 0x53]);
            Relative(Call, targets.Leave!.Value);
            code.AddRange([0x83, 0xC4, 0x0C]);
            labels[Free] = code.Count;
            code.AddRange([0x31, 0xC0]);
            Branch([Jump], End);
            labels[Busy] = code.Count;
            code.AddRange([0xB0, 0x01]);
            labels[End] = code.Count;
            code.AddRange([0x5B, Return]);
        }
        foreach ((int from, string label) in jumps)
        {
            byte[] distance = BitConverter.GetBytes(labels[label] - (from + 4));
            for (int i = 0; i < 4; i++) code[from + i] = distance[i];
        }
        return code;
    }
}
