# openkogama-mods

A modding framework for OpenKogama clients.

Instead of injecting at runtime, the patcher rewrites the game's own files to add
hook points, then the game loads `okmod` and its mods on its own. One C API
(`include/okmod.h`) covers every client version; the host resolves what each call
means on the build it runs in, so mods never name a version.

| Part | What it is |
|------|------------|
| `include/okmod.h` | the API mods are built against |
| `core/` | `okmod`, loaded into the game: resolves the build, runs the hooks, loads mods |
| `patcher/` | rewrites `GameAssembly.dll` or `Assembly-CSharp.dll` to add the hooks and load `okmod` |
| `mods/` | native mods |
| `shim/` | `OkModShim`, the managed side for Mono builds; the patched `Assembly-CSharp.dll` IL calls into it, and it bridges to the same native okmod |

Later: a Lua mod on top of the same API, so servers can send sandboxed client scripts.
