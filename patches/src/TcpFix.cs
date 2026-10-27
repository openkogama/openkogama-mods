using Mono.Cecil;
using Mono.Cecil.Cil;

namespace OpenKogama.Mods.Patches;

public static class TcpFix
{
    const string NetworkGame = "MVNetworkGame";
    const int TcpProtocol = 1;
    const int MaxConstructorParameters = 4;
    static readonly byte?[] PeerCreation = [0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x48, 0x8B, null, 0x48, 0x8B, 0xC8];
    static readonly byte[] SetTcpArgument = [0x41, 0xB0, TcpProtocol];
    const int ProtocolArgumentAt = 3;
    static readonly byte?[] PeerCreation32 = [Call, null, null, null, null, 0x6A, 0x00, 0x6A, 0x00, 0x8B, null, null, null, Call];
    static readonly byte[] PushTcp = [0x6A, TcpProtocol];
    const int ProtocolPushAt = 7;
    const byte Call = 0xE8;

    public static bool Apply(ModuleDefinition module)
    {
        if (module.GetType(NetworkGame) is not { } game) return false;
        bool changed = false;
        foreach (MethodDefinition method in game.Methods.Where(method => method.HasBody))
        {
            ILProcessor il = method.Body.GetILProcessor();
            foreach (Instruction create in method.Body.Instructions.Where(PeerConstructor).ToList())
            {
                il.InsertBefore(create, il.Create(OpCodes.Pop));
                il.InsertBefore(create, il.Create(OpCodes.Ldc_I4, TcpProtocol));
                changed = true;
            }
        }
        return changed;
    }

    static bool PeerConstructor(Instruction instruction) =>
        instruction.OpCode == OpCodes.Newobj && instruction.Operand is MethodReference { Name: ".ctor", DeclaringType.Name: "PhotonPeer" } constructor
        && constructor.Parameters is [_, { ParameterType.Name: "ConnectionProtocol" }];


    public static bool Apply(Il2Cpp il2cpp, Action<string> log)
    {
        byte?[] pattern = il2cpp.Is64Bit ? PeerCreation : PeerCreation32;
        byte[] image = il2cpp.Binary;
        List<int> found = [];
        for (int parameters = 0; parameters <= MaxConstructorParameters; parameters++)
        {
            if (il2cpp.Find(NetworkGame, ".ctor", parameters) is not uint constructor) continue;
            int start = il2cpp.Offset(constructor), end = start + (int)(il2cpp.End(constructor) - constructor);
            for (int at = start; at + pattern.Length <= end; at++)
                if (Matches(image, at, pattern)) found.Add(at);
        }
        if (found is not [int site])
        {
            log($"{Fixes.Tcp}: found the photon peer {found.Count} times, left as it is");
            return false;
        }
        if (il2cpp.Is64Bit) SetTcpArgument.CopyTo(image, site + ProtocolArgumentAt);
        else PushTcp.CopyTo(image, site + ProtocolPushAt);
        return true;
    }


    static bool Matches(byte[] image, int at, byte?[] pattern)
    {
        for (int i = 0; i < pattern.Length; i++)
            if (pattern[i] is byte expected && image[at + i] != expected) return false;
        return true;
    }
}
