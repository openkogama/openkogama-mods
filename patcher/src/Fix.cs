namespace OpenKogama.Mods.Patcher;

public static class Fix
{
    const string Game = "Assembly-CSharp.dll";
    const string NetworkGame = "MVNetworkGame";
    const int Tcp = 1;
    const int MaxConstructorParameters = 4;

    static readonly byte?[] PeerCreation = [0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x48, 0x8B, null, 0x48, 0x8B, 0xC8];
    static readonly byte[] SetTcpArgument = [0x41, 0xB0, Tcp];
    const int ProtocolArgumentAt = 3;
    static readonly byte?[] PeerCreation32 = [0xE8, null, null, null, null, 0x6A, 0x00, 0x6A, 0x00, 0x8B, null, null, null, 0xE8];
    static readonly byte[] PushTcp = [0x6A, Tcp];
    const int ProtocolPushAt = 7;

    public static bool UseTcp(Il2Cpp il2cpp)
    {
        byte?[] pattern = il2cpp.Is64Bit ? PeerCreation : PeerCreation32;
        byte[] image = il2cpp.Binary;
        List<int> found = [];
        for (int parameters = 0; parameters <= MaxConstructorParameters; parameters++)
        {
            if (il2cpp.Find(Game, NetworkGame, ".ctor", parameters) is not uint ctor) continue;
            int start = il2cpp.Offset(ctor), end = start + (int)(il2cpp.End(ctor) - ctor);
            for (int at = start; at + pattern.Length <= end; at++)
                if (Matches(image, at, pattern)) found.Add(at);
        }
        if (found is not [int site])
        {
            Console.WriteLine($"tcp: found the photon peer {found.Count} times, left as it is");
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

    const byte Call = 0xE8;
    const byte Jump = 0xE9;
    const byte Return = 0xC3;
    const int ReloadFrames = 60;
    const byte WindowMoverArguments = 7;
    const int CursorUnlocked = 0;
    const string Controller = "MVGameControllerBase";

    public static bool Reload(Il2Cpp il2cpp)
    {
        if (il2cpp.Find(Controller, "TryReauth", 0) is not uint tryReauth
            || il2cpp.Find("AsyncWWWManager", "WWWRequest", 1) is not uint request
            || il2cpp.Find(Controller, "OnReceivedReAuthWebParametersFromHttpRequest", 1) is not uint cave
            || il2cpp.Find("MVGameControllerDesktop", "get_Instance", 0) is not uint instance
            || il2cpp.FindEngine("UnityEngine.Component", "get_gameObject", 0) is not uint gameObject
            || il2cpp.FindEngine("UnityEngine.Object", "Destroy", 1) is not uint destroy
            || il2cpp.Find("GameLoader", "Cleanup", 0) is not uint cleanup
            || il2cpp.Find("GameLoader", "UnloadGame", 0) is not uint unload
            || il2cpp.Shared(tryReauth) || il2cpp.Shared(cave))
            return false;

        byte[] image = il2cpp.Binary;
        int start = il2cpp.Offset(tryReauth);
        List<int> sites = [];
        for (int at = start; at + 5 <= start + (il2cpp.End(tryReauth) - tryReauth); at++)
            if (image[at] == Call && tryReauth + (at - start) + 5 + BitConverter.ToInt32(image, at + 1) == request) sites.Add(at);
        if (sites.Count != 1) return false;

        var code = new List<byte>();
        uint? windowMover = WindowMover(il2cpp);
        if (windowMover is not null) code.AddRange(il2cpp.Is64Bit ? [0x31, 0xC0, Return] : [0x31, 0xC0, 0xC2, WindowMoverArguments * 4, 0x00]);
        uint entry = cave + (uint)code.Count;
        if (il2cpp.Is64Bit) code.AddRange([0x48, 0x83, 0xEC, 0x28]);
        if (windowMover is uint slot) Point(code, cave, il2cpp.Is64Bit, slot, cave);
        (uint? Target, int? Argument)[] optional =
        [
            (il2cpp.Find("UpdateController", "Clear", 0), 0),
            (il2cpp.Find("AsyncWWWManager", "Reset", 0), 0),
            (il2cpp.FindEngine("UnityEngine.Cursor", "set_lockState", 1), CursorUnlocked),
            (il2cpp.FindEngine("UnityEngine.Cursor", "set_visible", 1), 1),
        ];
        (uint Target, int? Argument)[] calls =
        [
            (instance, 0),
            (gameObject, null),
            (destroy, null),
            (cleanup, 0),
            .. optional.Where(call => call.Target is not null).Select(call => (call.Target!.Value, call.Argument)),
            (unload, 0),
        ];
        foreach ((uint target, int? argument) in calls)
        {
            if (il2cpp.Is64Bit) code.AddRange(argument switch { null => [0x48, 0x89, 0xC1], 0 => [0x31, 0xC9], int value => [0xB9, .. BitConverter.GetBytes(value)] });
            else code.AddRange(argument is int value ? [0x6A, 0x00, 0x6A, (byte)value] : [0x6A, 0x00, 0x50]);
            code.AddRange(il2cpp.Is64Bit ? [0x31, 0xD2, Call] : [Call]);
            code.AddRange(BitConverter.GetBytes((int)(target - (cave + code.Count + 4))));
            if (!il2cpp.Is64Bit) code.AddRange([0x83, 0xC4, 0x08]);
        }
        if (il2cpp.Is64Bit) code.AddRange([0x48, 0x83, 0xC4, 0x28]);
        code.Add(Return);
        if (il2cpp.End(cave) - cave < code.Count) return false;

        List<(int At, uint Site, uint Stub)> resizes = [];
        if (il2cpp.FindEngine("UnityEngine.Time", "get_frameCount", 0) is uint frames)
        {
            var window = new List<byte>(code);
            foreach ((int at, uint call, uint target) in Resizes(il2cpp))
            {
                uint stub = cave + (uint)window.Count;
                KeepWindow(window, cave, il2cpp.Is64Bit, frames, target);
                resizes.Add((at, call, stub));
            }
            if (il2cpp.End(cave) - cave >= window.Count) code = window;
            else resizes.Clear();
        }

        code.CopyTo(image, il2cpp.Offset(cave));
        il2cpp.Unrelocate(cave, code.Count);
        uint site = (uint)(tryReauth + (sites[0] - start));
        BitConverter.TryWriteBytes(image.AsSpan(sites[0] + 1), (int)(entry - (site + 5)));
        il2cpp.Unrelocate(site + 1, 4);
        foreach ((int at, uint resize, uint stub) in resizes)
        {
            BitConverter.TryWriteBytes(image.AsSpan(at + 1), (int)(stub - (resize + 5)));
            il2cpp.Unrelocate(resize + 1, 4);
        }
        if (il2cpp.Find("HackingToolDetector", "Initialize", 1) is uint detector && !il2cpp.Shared(detector))
        {
            image[il2cpp.Offset(detector)] = Return;
            il2cpp.Unrelocate(detector, 1);
        }
        return true;
    }

    static uint? WindowMover(Il2Cpp il2cpp)
    {
        if (il2cpp.Find(Controller, "SetWindowPos", WindowMoverArguments) is not uint wrapper) return null;
        byte[] image = il2cpp.Binary;
        byte[] store = il2cpp.Is64Bit ? [0x48, 0x89, 0x05] : [0x83, 0xC4, 0x04, 0xA3];
        int from = il2cpp.Offset(wrapper);
        List<uint> slots = [];
        for (int at = from; at + 9 + store.Length <= from + (il2cpp.End(wrapper) - wrapper); at++)
        {
            if (image[at] != Call || !image.AsSpan(at + 5, store.Length).SequenceEqual(store)) continue;
            int operand = at + 5 + store.Length;
            slots.Add(il2cpp.Is64Bit
                ? (uint)(wrapper + (operand + 4 - from) + BitConverter.ToInt32(image, operand))
                : (uint)(BitConverter.ToUInt32(image, operand) - il2cpp.ImageBase));
        }
        return slots.Count == 1 ? slots[0] : null;
    }

    static void Point(List<byte> code, uint cave, bool wide, uint slot, uint target)
    {
        if (wide)
        {
            code.AddRange([0x48, 0x8D, 0x05]);
            code.AddRange(BitConverter.GetBytes((int)(target - (cave + code.Count + 4))));
            code.AddRange([0x48, 0x89, 0x05]);
            code.AddRange(BitConverter.GetBytes((int)(slot - (cave + code.Count + 4))));
            return;
        }
        code.AddRange([Call, 0, 0, 0, 0, 0x58]);
        uint here = cave + (uint)code.Count - 1;
        code.AddRange([0x8D, 0x88]);
        code.AddRange(BitConverter.GetBytes((int)(target - here)));
        code.AddRange([0x89, 0x88]);
        code.AddRange(BitConverter.GetBytes((int)(slot - here)));
    }

    static List<(int At, uint Site, uint Target)> Resizes(Il2Cpp il2cpp)
    {
        if (il2cpp.Find("MVGameControllerDesktop", "Start", 0) is not uint start
            || il2cpp.Find("FullScreenController", "set_FullScreen", 1) is not uint fullScreen)
            return [];
        byte[] image = il2cpp.Binary;
        int from = il2cpp.Offset(start);
        List<(int, uint, uint)> calls = [];
        for (int at = from; at + 5 <= from + (il2cpp.End(start) - start) && calls.Count < 2; at++)
        {
            if (image[at] != Call) continue;
            uint site = (uint)(start + (at - from));
            uint target = (uint)(site + 5 + BitConverter.ToInt32(image, at + 1));
            if (target == fullScreen || calls.Count == 1 && il2cpp.Known(target)) calls.Add((at, site, target));
        }
        return calls.Count == 2 ? calls : [];
    }

    static void KeepWindow(List<byte> code, uint cave, bool wide, uint frames, uint target)
    {
        void Relative(byte opcode, uint to)
        {
            code.Add(opcode);
            code.AddRange(BitConverter.GetBytes((int)(to - (cave + code.Count + 4))));
        }

        if (wide) code.AddRange([0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x48, 0x83, 0xEC, 0x28, 0x31, 0xC9]);
        else code.AddRange([0x6A, 0x00]);
        Relative(Call, frames);
        if (wide) code.AddRange([0x48, 0x83, 0xC4, 0x28, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59]);
        else code.AddRange([0x83, 0xC4, 0x04]);
        code.Add(0x3D);
        code.AddRange(BitConverter.GetBytes(ReloadFrames));
        code.AddRange([0x7F, 0x05]);
        Relative(Jump, target);
        code.Add(Return);
    }
}
