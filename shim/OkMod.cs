using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace OpenKogama.Mods
{
    public static class OkMod
    {
        static readonly Dictionary<string, int> Flags = new Dictionary<string, int>();
        static readonly Dictionary<string, double> Params = new Dictionary<string, double>();
        static bool _native = true;

        [DllImport("okmod", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int okmod_flag(string name);

        [DllImport("okmod", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern double okmod_paramf(string name, double fallback);

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

        public static void SetFlag(string name, bool on) { Flags[name] = on ? 1 : 0; }
        public static void SetParam(string name, double value) { Params[name] = value; }
    }
}
