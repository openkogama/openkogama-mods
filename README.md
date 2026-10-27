# openkogama-mods

Everything OpenKogama changes in the files of a KoGaMa client, and a modding framework on top of it.

Nothing is injected into the game process. Before every launch the launcher rewrites the game's own files from a copy of the
original (`.original` next to each file), and the game starts with the changes in place. The code that does that lives here, in
`patches/`, and openkogama-server builds against it: the launcher runs `openkogama-server client fix` and `client patch`, which call
this library.

| Part | What it is |
|------|------------|
| `patches/` | the library with every change to game files: the code fixes, the server's sounds and themes in the game data, the okmod install, and the IL2CPP, PE and Unity file readers they share |
| `patcher/` | `okpatch`, the command line over `patches/` |
| `include/okmod.h` | the API mods are built against |
| `core/` | `okmod`, loaded into the game: resolves the build, runs the hooks, loads mods |
| `mods/` | native mods |
| `shim/` | `OkModShim`, the managed side for Mono builds; the patched `Assembly-CSharp.dll` IL calls into it, and it bridges to the same native okmod |

## Code fixes

`client fix <original> <output> [fixes]` rewrites `kogama_Data/Managed/Assembly-CSharp.dll` on Mono builds (Mono.Cecil) or
`GameAssembly.dll` on IL2CPP builds (machine code, methods found by name through `global-metadata.dat`, new code in methods the game
never calls). The output is the same for the same input, and a file that would not change is not written, so several game windows
can share one installation.

| Fix | What it changes |
|-----|-----------------|
| `colossus-laser` | wraps `GodzillaLaser.OnUnequip` in a try block, so watching someone leave the Colossus no longer breaks the game |
| `world-switch` | `MVGameControllerBase.TryReauth` cleans up and loads the new world in place instead of restarting, and `HackingToolDetector` is off |
| `tcp` | `MVNetworkGame` opens its Photon connection over TCP |
| `teleport` | the game takes a teleport the server sends to the player's own avatar |
| `mods` | adds `okmod.dll` to the imports of `GameAssembly.dll` with the hooks from `Features.cs`; the launcher asks for it on 3.5 when its mods folder has mods |

## Server content

`client patch <data.unity3d or sharedassets0.assets> <content.json> <output>` puts what a server offers into the game data: its sounds
into the Sound Emitter list of 3.5, and its themes into the theme menu from 1.88, as clones of a theme prefab with their own name,
settings, textures and day and night. The launcher takes the content from the server's `/api/content`:

```json
{
  "sounds": [{ "url": "https://example.com/sound.unity3d", "name": "Wind", "group": "Ambient" }],
  "themes": [{
    "name": "Real Sky", "base": "Cloudy", "title": "Real Sky", "description": "A real day and night",
    "folder": "cloudy", "files": { "clouds": "clouds.unity3d" },
    "textures": { "clouds": "https://example.com/real-clouds.unity3d" },
    "settings": { "useDayNightCycle": true }, "cycle": { "sunrise": 25, "sunset": 75 }
  }]
}
```

`folder` and `files` name the base theme's own bundles, `textures` the bundles that take their place. Settings outside the range the
game allows are clamped before they go in.

## Building

openkogama-server references `patches/` as `..\openkogama-mods\patches`, so clone this repository next to openkogama-server and
openkogama-photon. The core and the mods build with CMake (`cmake -S core -B core/build -A x64`), the patcher with
`dotnet build patcher/OpenKogama.Mods.Patcher.csproj`.

Later: a Lua mod on top of the same API, so servers can send sandboxed client scripts.
