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

## Scripts

The `scripts` mod runs Lua 5.5 in the game. A folder in `scripts` next to the game with a `main.lua` is a script, which is how scripts are made
and tried; the scripts of the server the game was started for come from its `/api/scripts` (a `scripts` folder next to the server, or
`Scripts.Add` from a plugin), run as `server:<name>` and follow the server's changes while the game runs. Every script gets its own Lua state
with the base, string, table, math, utf8 and coroutine libraries and `os.time`, `os.clock`, `os.date` and `os.difftime`; there is no `io`,
`package`, `debug`, `dofile`, `loadfile` or `string.dump`, `load` takes text only, `require` loads files of the script's own folder and
`setmetatable` refuses `__gc`. A script has 64 MB, a call 50 ms (slow string patterns included) and loading 2 s; a script that fails 10 times
in 10 seconds stops until its files change. A script reloads when its files change, and what scripts print or fail with goes to `scripts.log`
next to the game. What a script draws goes into the game's own Direct3D 11 frame.

```lua
local clicks = 0

on("draw", function()
  draw.text("clicks: " .. clicks, 20, 20, { size = 20, bold = true })
  ui.window("Shop", function()
    if ui.button("Click") then clicks = clicks + 1 end
  end, { x = 20, y = 60, width = 200 })
end)
```

| Area | What a script gets |
|------|--------------------|
| Events | `on(event, fn)` and `off(event, fn)` with `update` (the seconds since the last frame), `draw` (the only place to draw), `key` (name and whether it went down: letters, digits, `F1` to `F24`, `Space`, `Enter`, `Escape`, `Tab`, `Backspace`, `Shift`, `Control`, `Alt`, `Left`, `Right`, `Up`, `Down`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `MouseLeft`, `MouseRight`, `MouseMiddle`), `world` (once a world has loaded after the loading screen, so not when other players join, and right after a script starts inside a loaded world) and `message` (a name and a text from the server's plugins) |
| Timers | `after(seconds, fn)` and `every(seconds, fn)` give an id for `cancel(id)`; `every` runs at most every 0.01 s |
| Draw | `draw.rect(x, y, w, h, color, { rounding, thickness })` (an outline with a thickness), `draw.line(x1, y1, x2, y2, color, thickness)`, `draw.circle(x, y, radius, color, { thickness })`, `draw.text(text, x, y, { size, color, font, align, shadow })` with `align` `left`, `center` or `right` and a shadow unless `shadow = false`, `draw.measure(text, size, font)` (width and height) and `draw.image(file, x, y, w, h, { color, rounding, filter })` with a png or jpg of the script's folder or a file id from the server (`filter = "nearest"` keeps pixels sharp when scaled up), `draw.poly(points, color, { smooth })` for a filled convex polygon of 3 to 256 points given as `{ x1, y1, x2, y2, ... }`, and `draw.polys(points, colors, counts, { smooth })` for many of them in one call: all the points in one flat table, `counts` how many points each polygon takes and `colors` one color for each or a single one for all, up to 100000 polygons and 400000 points; `smooth` (antialiased edges) is on for `draw.poly` and off for `draw.polys`, so neighbouring tiles leave no seams. Positions are pixels of the game window |
| Windows | `ui.window(title, fn, { x, y, width, height, closable, autosize, fixed, title, style })` gives false once the player closes it, and inside it `ui.with(style, fn)` (a style for part of a window), `ui.text(text, { color, wrap })`, `ui.button(label, { width, height })`, `ui.checkbox(label, value)`, `ui.slider(label, value, min, max)` (whole numbers make a whole slider), `ui.input(label, text, { hint })`, `ui.progress(fraction, { text, width })`, `ui.image(file, w, h, { filter })`, `ui.separator()`, `ui.sameline()` and `ui.spacing()`; the controls give their value and whether it changed, and their labels name them, so `"Buy##sword"` shows Buy next to another Buy |
| Styles | Windows look like plain ImGui, its dark colors and the ProggyClean font, until a script changes that. A style is a table with ImGui's own names: any of its style variables (`WindowRounding = 8`, `FramePadding = { 8, 5 }`, `Alpha`, `ItemSpacing` and the rest of `ImGuiStyleVar`), `colors` by ImGui's color names (`{ WindowBg = "#101318e6", Button = "#2e8ce8" }`), `font` and `fontSize`. `ui.style(style)` sets one for every window of the script, `style` in `ui.window` for one window; numbers out of range are clamped and unknown names are errors |
| Fonts | `"default"` (ProggyClean) or a file of the Windows Fonts folder such as `"segoeui.ttf"` or `"consola.ttf"`; a script cannot bring font files of its own, because ImGui's font rasterizer is not made for untrusted files. A font is ready from the next frame, up to 16 of them |
| Colors | `"#ff8800"`, `"#ff880080"` with alpha, or `rgb(255, 136, 0, 128)` |
| Game | `screen.size()`, `input.down(key)`, `input.mouse()`, `input.locked()` (the game holds the cursor, so the player is playing and not in a menu, the chat or the inventory), `input.block(on)` (the game gets no keys, mouse buttons, wheel or mouse movement, so nothing walks, jumps, shoots, turns the camera or opens the menu, while the script still gets its `key` events; releases still reach the game and the game's own input stays suspended meanwhile, so nothing sticks and a key held down from before does not keep the player walking, Alt+F4 still works, and the block ends when the script stops or reloads and when a world loads), `input.cursor(on)` (true frees and shows the cursor without taking the game out of play mode and keeps it so while the game would lock it, false gives it back as it was: locked and hidden again only when it was locked before and the game is still playing, so the mouse turns the camera again and a menu keeps its cursor; it ends like the block), `input.wheel()` (the wheel's notches in the last frame, positive away from the player, also as the `wheel` event with the same number) and `input.consume(key, on)` (the game never sees that key or mouse button while the script holds it, so a script's own shortcut does nothing else, though shortcuts with Alt such as Alt+F4 still reach Windows; `on` defaults to true and it ends with the script), `player.position()`, `player.name()` and `camera.position()` |
| Server | `server.send(name, text)` reaches the plugins' `Server.ScriptMessage`, and `Player.SendScriptMessage(name, text)` comes back as `message`; names are up to 64 letters, digits and `_.:-`, texts up to 64 KB. A file a plugin serves with `Scripts.AddFile` has an id of 64 hex digits that `draw.image` and `ui.image` take in place of a file name: the game fetches it from the server the first time it is drawn (4 at once, the rest wait), draws nothing until it is there, keeps it on disk for a week and lets go of an image from the server that has not been drawn for 30 seconds; `server.file(id)` tells whether it is `"ready"`, `"loading"` or `"failed"` and starts fetching it, so a script can fetch ahead |
| Interface | `hud.hide(name)`, `hud.show(name)`, `hud.shown(name)` and `hud.list(name)` work on the game's own interface and nothing else: `name` is a path under its root (the play mode UI, else the edit mode UI, `""` for the whole of it, `"Canvas/Panel"` for a child), one of `hp`, `xp`, `level`, `coins`, `points`, `shield`, `time`, `crosshair`, `chat`, `hotbar`, `boosts`, `edit` (the button from play to edit mode) and `play` (the button back), or `"@ClassName"` for a component of the game's own classes. A name of a part or a class means every part of the game's interface (anything under a canvas) with that component, wherever the game keeps it. `hud.list` gives the names of a part's children to find paths with, `hud.shown` true while any of the parts shows, false or nil when there is no such part, and `hud.active` whether the game itself has any of them on, hidden by a script or not (the crosshair is on while the player holds a weapon). A hidden part gets a scale of zero (a canvas of its own is switched off instead), so the game turning it on again does not show it; the crosshair takes its ammo counter with it. What a script hides stays hidden while the game rebuilds its interface and comes back when the script stops or reloads or the player goes to another server, unless another script still hides it; `hud.show` only undoes `hud.hide`, so whether the part is on stays up to the game (the crosshair only with a weapon), and it is refused while another script hides the part |

```lua
on("world", function()
  hud.hide("hp")
  hud.hide("xp")
  for _, name in ipairs(hud.list("") or {}) do print(name) end
end)

on("key", function(key, down)
  if key == "F6" and down then
    if hud.shown("chat") then hud.hide("chat") else hud.show("chat") end
  end
end)
```

## Building

openkogama-server references `patches/` as `..\openkogama-mods\patches`, so clone this repository next to openkogama-server and
openkogama-photon. The core and the mods build with CMake (`cmake -S core -B core/build -A x64`), the patcher with
`dotnet build patcher/OpenKogama.Mods.Patcher.csproj`.
