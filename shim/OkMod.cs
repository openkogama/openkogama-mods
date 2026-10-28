using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace OpenKogama.Mods
{
    public static class OkMod
    {
        const int Slots = 18;
        const int Registers = 8;
        static readonly Dictionary<string, int> Flags = new Dictionary<string, int>();
        static readonly Dictionary<string, double> Params = new Dictionary<string, double>();
        static bool _native = true;

        public static long Result;

        [DllImport("okmod", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int okmod_flag(string name);

        [DllImport("okmod", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern double okmod_paramf(string name, double fallback);

        [DllImport("okmod", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int okmod_event(string hook, long[] args, int argc, out long result);

        [DllImport("okmod", CallingConvention = CallingConvention.Cdecl)]
        static extern long okmod_address(IntPtr handle);

        public static bool Flag(string name)
        {
            if (_native)
            {
                try { return okmod_flag(name) != 0; }
                catch { _native = false; }
            }
            return Flags.TryGetValue(name, out int value) && value != 0;
        }

        public static double Param(string name, double fallback)
        {
            if (_native)
            {
                try { return okmod_paramf(name, fallback); }
                catch { _native = false; }
            }
            return Params.TryGetValue(name, out double value) ? value : fallback;
        }

        public static long[] Arguments() => new long[Slots];

        public static bool Event(string hook, long[] args)
        {
            if (!_native) return false;
            try { return okmod_event(hook, args, Registers, out Result) != 0; }
            catch { _native = false; return false; }
        }

        public static long Address(object value)
        {
            if (value == null || !_native) return 0;
            GCHandle handle = GCHandle.Alloc(value);
            try { return okmod_address(GCHandle.ToIntPtr(handle)); }
            catch { _native = false; return 0; }
            finally { handle.Free(); }
        }

        public static unsafe long Bits(float value) => *(int*)&value;

        public static unsafe float Single(long bits)
        {
            int low = (int)bits;
            return *(float*)&low;
        }

        public static void SetFlag(string name, bool on) { Flags[name] = on ? 1 : 0; }
        public static void SetParam(string name, double value) { Params[name] = value; }
    }
}
