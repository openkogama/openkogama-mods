#include "api.h"
#include "runtime.h"
#include "game.h"
#include "images.h"
#include "lua.h"
#include "lauxlib.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>

namespace api {
    namespace {
        constexpr float SmallestText = 6.0f;
        constexpr float LargestText = 160.0f;
        constexpr size_t FontLimit = 16;
        constexpr size_t InputLimit = 1024;
        constexpr size_t QueueLimit = 256;
        constexpr size_t MessageLimit = 64u << 10;
        constexpr size_t WindowLimit = 32;

        struct Held {
            std::string name;
            bool down = false;
        };

        struct StyleVar {
            const char* name;
            ImGuiStyleVar index;
            bool pair;
            float low;
            float high;
        };

        const StyleVar StyleVars[] = {
            {"Alpha", ImGuiStyleVar_Alpha, false, 0, 1},
            {"DisabledAlpha", ImGuiStyleVar_DisabledAlpha, false, 0, 1},
            {"WindowPadding", ImGuiStyleVar_WindowPadding, true, 0, 200},
            {"WindowRounding", ImGuiStyleVar_WindowRounding, false, 0, 200},
            {"WindowBorderSize", ImGuiStyleVar_WindowBorderSize, false, 0, 50},
            {"WindowMinSize", ImGuiStyleVar_WindowMinSize, true, 1, 4000},
            {"WindowTitleAlign", ImGuiStyleVar_WindowTitleAlign, true, 0, 1},
            {"ChildRounding", ImGuiStyleVar_ChildRounding, false, 0, 200},
            {"ChildBorderSize", ImGuiStyleVar_ChildBorderSize, false, 0, 50},
            {"PopupRounding", ImGuiStyleVar_PopupRounding, false, 0, 200},
            {"PopupBorderSize", ImGuiStyleVar_PopupBorderSize, false, 0, 50},
            {"FramePadding", ImGuiStyleVar_FramePadding, true, 0, 200},
            {"FrameRounding", ImGuiStyleVar_FrameRounding, false, 0, 200},
            {"FrameBorderSize", ImGuiStyleVar_FrameBorderSize, false, 0, 50},
            {"ItemSpacing", ImGuiStyleVar_ItemSpacing, true, 0, 200},
            {"ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing, true, 0, 200},
            {"IndentSpacing", ImGuiStyleVar_IndentSpacing, false, 0, 500},
            {"CellPadding", ImGuiStyleVar_CellPadding, true, 0, 200},
            {"ScrollbarSize", ImGuiStyleVar_ScrollbarSize, false, 1, 200},
            {"ScrollbarRounding", ImGuiStyleVar_ScrollbarRounding, false, 0, 200},
            {"ScrollbarPadding", ImGuiStyleVar_ScrollbarPadding, false, 0, 200},
            {"GrabMinSize", ImGuiStyleVar_GrabMinSize, false, 1, 200},
            {"GrabRounding", ImGuiStyleVar_GrabRounding, false, 0, 200},
            {"ImageRounding", ImGuiStyleVar_ImageRounding, false, 0, 200},
            {"ImageBorderSize", ImGuiStyleVar_ImageBorderSize, false, 0, 50},
            {"TabRounding", ImGuiStyleVar_TabRounding, false, 0, 200},
            {"TabBorderSize", ImGuiStyleVar_TabBorderSize, false, 0, 50},
            {"TabMinWidthBase", ImGuiStyleVar_TabMinWidthBase, false, 0, 1000},
            {"TabMinWidthShrink", ImGuiStyleVar_TabMinWidthShrink, false, 0, 1000},
            {"TabBarBorderSize", ImGuiStyleVar_TabBarBorderSize, false, 0, 50},
            {"TabBarOverlineSize", ImGuiStyleVar_TabBarOverlineSize, false, 0, 50},
            {"TableAngledHeadersAngle", ImGuiStyleVar_TableAngledHeadersAngle, false, -0.87f, 0.87f},
            {"TableAngledHeadersTextAlign", ImGuiStyleVar_TableAngledHeadersTextAlign, true, 0, 1},
            {"TreeLinesSize", ImGuiStyleVar_TreeLinesSize, false, 0, 50},
            {"TreeLinesRounding", ImGuiStyleVar_TreeLinesRounding, false, 0, 200},
            {"ButtonTextAlign", ImGuiStyleVar_ButtonTextAlign, true, 0, 1},
            {"SelectableTextAlign", ImGuiStyleVar_SelectableTextAlign, true, 0, 1},
            {"SeparatorTextBorderSize", ImGuiStyleVar_SeparatorTextBorderSize, false, 0, 50},
            {"SeparatorTextAlign", ImGuiStyleVar_SeparatorTextAlign, true, 0, 1},
            {"SeparatorTextPadding", ImGuiStyleVar_SeparatorTextPadding, true, 0, 200},
            {"DockingSeparatorSize", ImGuiStyleVar_DockingSeparatorSize, false, 0, 50},
        };

        struct Change {
            enum Kind { Var, Color } kind;
            ImGuiStyleVar var;
            bool pair;
            ImVec2 value;
            ImGuiCol color;
            ImU32 rgba;
        };

        struct Look {
            std::vector<Change> changes;
            ImFont* font = nullptr;
            float size = 0.0f;
        };

        struct Scope {
            int vars = 0;
            int colors = 0;
            bool font = false;
        };

        const char* const StyleKey = "scripts.style";

        Sender sender = nullptr;
        bool drawing = false;
        int windows = 0;
        ImFont* fallback = nullptr;
        std::map<std::string, ImFont*> fonts;
        std::set<std::string> wanted;
        std::map<std::string, Held> keys;
        std::vector<KeyEvent> queue;
        std::map<std::string, std::set<std::string>> windowsOf;

        std::string Lower(const std::string& text) {
            std::string lower = text;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return lower;
        }

        ImU32 Pack(unsigned value) {
            return IM_COL32((value >> 24) & 255, (value >> 16) & 255, (value >> 8) & 255, value & 255);
        }

        int Hex(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            c = (char)std::tolower((unsigned char)c);
            return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        }

        bool Parse(lua_State* L, int index, ImU32& color) {
            if (lua_type(L, index) == LUA_TNUMBER) {
                if (!lua_isinteger(L, index)) return false;
                color = Pack((unsigned)(lua_tointeger(L, index) & 0xFFFFFFFF));
                return true;
            }
            if (lua_type(L, index) != LUA_TSTRING) return false;
            const char* text = lua_tostring(L, index);
            if (*text == '#') text++;
            size_t length = std::strlen(text);
            if (length != 6 && length != 8) return false;
            unsigned value = 0;
            for (size_t i = 0; i < length; i++) {
                int digit = Hex(text[i]);
                if (digit < 0) return false;
                value = value * 16 + (unsigned)digit;
            }
            if (length == 6) value = (value << 8) | 0xFF;
            color = Pack(value);
            return true;
        }

        ImU32 Color(lua_State* L, int index, ImU32 fallback) {
            if (lua_isnoneornil(L, index)) return fallback;
            ImU32 color = fallback;
            if (!Parse(L, index, color)) luaL_argerror(L, index, "expected a color like \"#ff8800\", \"#ff880080\" or rgb(255, 136, 0)");
            return color;
        }

        bool Field(lua_State* L, int options, const char* name) {
            if (!lua_istable(L, options)) return false;
            if (lua_getfield(L, options, name) != LUA_TNIL) return true;
            lua_pop(L, 1);
            return false;
        }

        float Number(lua_State* L, int options, const char* name, float fallback) {
            if (!Field(L, options, name)) return fallback;
            float value = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : fallback;
            lua_pop(L, 1);
            return value;
        }

        bool Flag(lua_State* L, int options, const char* name, bool fallback) {
            if (!Field(L, options, name)) return fallback;
            bool value = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
            return value;
        }

        std::string Text(lua_State* L, int options, const char* name) {
            if (!Field(L, options, name)) return {};
            const char* text = lua_tostring(L, -1);
            std::string value = text ? text : "";
            lua_pop(L, 1);
            return value;
        }

        ImU32 ColorOption(lua_State* L, int options, const char* name, ImU32 fallback) {
            if (!Field(L, options, name)) return fallback;
            ImU32 color = fallback;
            if (!Parse(L, -1, color)) luaL_error(L, "option '%s' expects a color like \"#ff8800\"", name);
            lua_pop(L, 1);
            return color;
        }

        float Coordinate(lua_State* L, int index) { return (float)luaL_checknumber(L, index); }

        ImFont* Default() { return fallback ? fallback : ImGui::GetFont(); }

        ImFont* FontFor(lua_State* L, const std::string& name) {
            std::string key = Lower(name);
            if (key.empty() || key == "default") return Default();
            bool valid = key.size() <= 64 && (key.size() > 4 && (key.compare(key.size() - 4, 4, ".ttf") == 0 || key.compare(key.size() - 4, 4, ".otf") == 0));
            for (char c : key) valid = valid && (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.');
            if (!valid) luaL_error(L, "a font is \"default\" or a file of the Windows Fonts folder such as \"segoeui.ttf\"");
            auto found = fonts.find(key);
            if (found != fonts.end()) return found->second ? found->second : Default();
            if (fonts.size() + wanted.size() < FontLimit) wanted.insert(key);
            return Default();
        }

        float TextSize(lua_State* L, int index, ImFont* font) {
            float size = lua_isnoneornil(L, index) ? font->LegacySize : (float)luaL_checknumber(L, index);
            return std::round(std::clamp(size, SmallestText, LargestText));
        }

        const StyleVar* FindVar(const char* name) {
            for (const StyleVar& var : StyleVars)
                if (std::strcmp(var.name, name) == 0) return &var;
            return nullptr;
        }

        float Clamped(lua_State* L, int index, const StyleVar& var) {
            if (!lua_isnumber(L, index)) luaL_error(L, "style '%s' takes a number", var.name);
            float value = (float)lua_tonumber(L, index);
            if (!std::isfinite(value)) luaL_error(L, "style '%s' takes a finite number", var.name);
            return std::clamp(value, var.low, var.high);
        }

        void ReadColors(lua_State* L, int table, Look& look) {
            if (!lua_istable(L, table)) luaL_error(L, "style colors are a table like { Button = \"#2e8ce8\" }");
            lua_pushnil(L);
            while (lua_next(L, table)) {
                if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "style colors are named like Button or WindowBg");
                const char* name = lua_tostring(L, -2);
                ImGuiCol index = -1;
                for (int i = 0; i < ImGuiCol_COUNT && index < 0; i++)
                    if (std::strcmp(ImGui::GetStyleColorName(i), name) == 0) index = i;
                if (index < 0) luaL_error(L, "imgui has no color named '%s'", name);
                Change change{Change::Color, 0, false, ImVec2(0, 0), index, 0};
                if (!Parse(L, -1, change.rgba)) luaL_error(L, "color '%s' takes a color like \"#ff8800\"", name);
                look.changes.push_back(change);
                lua_pop(L, 1);
            }
        }

        Look ReadLook(lua_State* L, int table) {
            Look look;
            if (lua_isnoneornil(L, table)) return look;
            if (!lua_istable(L, table)) luaL_error(L, "a style is a table like { WindowRounding = 6 }");
            table = lua_absindex(L, table);
            lua_pushnil(L);
            while (lua_next(L, table)) {
                if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "style keys are names like WindowRounding");
                const char* name = lua_tostring(L, -2);
                if (std::strcmp(name, "font") == 0) look.font = FontFor(L, luaL_checkstring(L, -1));
                else if (std::strcmp(name, "fontSize") == 0) look.size = TextSize(L, -1, Default());
                else if (std::strcmp(name, "colors") == 0) ReadColors(L, lua_gettop(L), look);
                else if (const StyleVar* var = FindVar(name)) {
                    Change change{Change::Var, var->index, var->pair, ImVec2(0, 0), 0, 0};
                    if (var->pair && lua_istable(L, -1)) {
                        lua_rawgeti(L, -1, 1);
                        lua_rawgeti(L, -2, 2);
                        change.value = ImVec2(Clamped(L, -2, *var), Clamped(L, -1, *var));
                        lua_pop(L, 2);
                    }
                    else change.value.x = change.value.y = Clamped(L, -1, *var);
                    look.changes.push_back(change);
                }
                else luaL_error(L, "imgui has no style named '%s'", name);
                lua_pop(L, 1);
            }
            return look;
        }

        Scope Apply(const Look& look) {
            Scope scope;
            for (const Change& change : look.changes) {
                if (change.kind == Change::Color) {
                    ImGui::PushStyleColor(change.color, change.rgba);
                    scope.colors++;
                }
                else {
                    if (change.pair) ImGui::PushStyleVar(change.var, change.value);
                    else ImGui::PushStyleVar(change.var, change.value.x);
                    scope.vars++;
                }
            }
            if (look.font || look.size > 0) {
                ImGui::PushFont(look.font, look.size);
                scope.font = true;
            }
            return scope;
        }

        void Undo(const Scope& scope) {
            if (scope.font) ImGui::PopFont();
            if (scope.colors) ImGui::PopStyleColor(scope.colors);
            if (scope.vars) ImGui::PopStyleVar(scope.vars);
        }

        Look ScriptLook(lua_State* L) {
            lua_getfield(L, LUA_REGISTRYINDEX, StyleKey);
            Look look = ReadLook(L, -1);
            lua_pop(L, 1);
            return look;
        }

        int Guarded(lua_State* L, int function) {
            lua_pushcfunction(L, runtime::Traceback);
            int handler = lua_gettop(L);
            lua_pushvalue(L, function);
            int status = lua_pcall(L, 0, 0, handler);
            lua_remove(L, handler);
            return status;
        }

        void NeedDraw(lua_State* L) {
            if (!drawing) luaL_error(L, "drawing works inside on(\"draw\", ...)");
        }

        void NeedWindow(lua_State* L) {
            NeedDraw(L);
            if (windows == 0) luaL_error(L, "this works inside ui.window");
        }

        ImDrawList* Hud() { return ImGui::GetBackgroundDrawList(); }

        int Rgb(lua_State* L) {
            auto channel = [L](int index, double fallback) {
                double value = lua_isnoneornil(L, index) ? fallback : luaL_checknumber(L, index);
                return (lua_Integer)std::lround(std::clamp(value, 0.0, 255.0));
            };
            lua_Integer r = channel(1, 0), g = channel(2, 0), b = channel(3, 0), a = channel(4, 255);
            lua_pushinteger(L, (r << 24) | (g << 16) | (b << 8) | a);
            return 1;
        }

        int HudRect(lua_State* L) {
            NeedDraw(L);
            float x = Coordinate(L, 1), y = Coordinate(L, 2), w = Coordinate(L, 3), h = Coordinate(L, 4);
            ImU32 color = Color(L, 5, IM_COL32_WHITE);
            float rounding = Number(L, 6, "rounding", 0.0f), thickness = Number(L, 6, "thickness", 0.0f);
            if (thickness > 0) Hud()->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color, rounding, 0, thickness);
            else Hud()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color, rounding);
            return 0;
        }

        int HudLine(lua_State* L) {
            NeedDraw(L);
            float x1 = Coordinate(L, 1), y1 = Coordinate(L, 2), x2 = Coordinate(L, 3), y2 = Coordinate(L, 4);
            ImU32 color = Color(L, 5, IM_COL32_WHITE);
            Hud()->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), color, (float)luaL_optnumber(L, 6, 1.0));
            return 0;
        }

        int HudCircle(lua_State* L) {
            NeedDraw(L);
            float x = Coordinate(L, 1), y = Coordinate(L, 2), radius = Coordinate(L, 3);
            ImU32 color = Color(L, 4, IM_COL32_WHITE);
            float thickness = Number(L, 5, "thickness", 0.0f);
            if (thickness > 0) Hud()->AddCircle(ImVec2(x, y), radius, color, 0, thickness);
            else Hud()->AddCircleFilled(ImVec2(x, y), radius, color);
            return 0;
        }

        int HudText(lua_State* L) {
            NeedDraw(L);
            lua_settop(L, 4);
            size_t length = 0;
            const char* text = luaL_tolstring(L, 1, &length);
            float x = Coordinate(L, 2), y = Coordinate(L, 3);
            ImFont* font = FontFor(L, Text(L, 4, "font"));
            float size = std::round(std::clamp(Number(L, 4, "size", font->LegacySize), SmallestText, LargestText));
            ImU32 color = ColorOption(L, 4, "color", IM_COL32_WHITE);
            std::string align = Text(L, 4, "align");
            ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text, text + length);
            if (align == "center") x -= extent.x * 0.5f;
            else if (align == "right") x -= extent.x;
            if (Flag(L, 4, "shadow", true)) {
                ImU32 alpha = ((color >> IM_COL32_A_SHIFT) & 255) * 3 / 4;
                Hud()->AddText(font, size, ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, alpha), text, text + length);
            }
            Hud()->AddText(font, size, ImVec2(x, y), color, text, text + length);
            return 0;
        }

        int HudMeasure(lua_State* L) {
            NeedDraw(L);
            lua_settop(L, 3);
            size_t length = 0;
            const char* text = luaL_tolstring(L, 1, &length);
            ImFont* font = FontFor(L, luaL_optstring(L, 3, ""));
            ImVec2 extent = font->CalcTextSizeA(TextSize(L, 2, font), FLT_MAX, 0.0f, text, text + length);
            lua_pushnumber(L, extent.x);
            lua_pushnumber(L, extent.y);
            return 2;
        }

        int HudImage(lua_State* L) {
            NeedDraw(L);
            images::Image image = images::Get(L, luaL_checkstring(L, 1));
            float x = Coordinate(L, 2), y = Coordinate(L, 3);
            float w = (float)luaL_optnumber(L, 4, image.width), h = (float)luaL_optnumber(L, 5, image.height);
            ImU32 tint = ColorOption(L, 6, "color", IM_COL32_WHITE);
            float rounding = Number(L, 6, "rounding", 0.0f);
            if (!image.texture) return 0;
            ImTextureRef texture((ImTextureID)image.texture);
            if (rounding > 0) Hud()->AddImageRounded(texture, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint, rounding);
            else Hud()->AddImage(texture, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
            return 0;
        }

        std::string WindowId(lua_State* L, const char* title) {
            std::string text = title, key = title;
            size_t mark = text.find("###");
            if (mark != std::string::npos) {
                key = text.substr(mark + 3);
                text = text.substr(0, mark);
            }
            return text + "###" + runtime::Name(L) + "/" + key;
        }

        int UiWindow(lua_State* L) {
            NeedDraw(L);
            std::string id = WindowId(L, luaL_checkstring(L, 1));
            luaL_checktype(L, 2, LUA_TFUNCTION);
            std::set<std::string>& known = windowsOf[runtime::Name(L)];
            if (!known.count(id)) {
                if (known.size() >= WindowLimit) return luaL_error(L, "a script can have up to %d windows", (int)WindowLimit);
                known.insert(id);
            }
            float x = Number(L, 3, "x", NAN), y = Number(L, 3, "y", NAN);
            float width = Number(L, 3, "width", 0.0f), height = Number(L, 3, "height", 0.0f);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
            if (Flag(L, 3, "autosize", false)) flags |= ImGuiWindowFlags_AlwaysAutoResize;
            if (Flag(L, 3, "fixed", false)) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
            if (!Flag(L, 3, "title", true)) flags |= ImGuiWindowFlags_NoTitleBar;
            bool closable = Flag(L, 3, "closable", false);
            if (!std::isnan(x) && !std::isnan(y)) ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
            if (width > 0 || height > 0) ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
            Look script = ScriptLook(L);
            Look own;
            if (Field(L, 3, "style")) {
                own = ReadLook(L, -1);
                lua_pop(L, 1);
            }
            bool open = true;
            int status = LUA_OK;
            Scope outer = Apply(script), inner = Apply(own);
            if (ImGui::Begin(id.c_str(), closable ? &open : nullptr, flags)) {
                windows++;
                status = Guarded(L, 2);
                windows--;
            }
            ImGui::End();
            Undo(inner);
            Undo(outer);
            if (status != LUA_OK) return lua_error(L);
            lua_pushboolean(L, open);
            return 1;
        }

        int UiStyle(lua_State* L) {
            if (!lua_isnoneornil(L, 1)) ReadLook(L, 1);
            lua_settop(L, 1);
            lua_setfield(L, LUA_REGISTRYINDEX, StyleKey);
            return 0;
        }

        int UiWith(lua_State* L) {
            NeedWindow(L);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            Scope scope = Apply(ReadLook(L, 1));
            int status = Guarded(L, 2);
            Undo(scope);
            if (status != LUA_OK) return lua_error(L);
            return 0;
        }

        int UiText(lua_State* L) {
            NeedWindow(L);
            lua_settop(L, 2);
            size_t length = 0;
            const char* text = luaL_tolstring(L, 1, &length);
            bool colored = Field(L, 2, "color");
            if (colored) lua_pop(L, 1);
            ImU32 color = ColorOption(L, 2, "color", 0);
            bool wrap = Flag(L, 2, "wrap", false);
            if (colored) ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (wrap) ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text, text + length);
            if (wrap) ImGui::PopTextWrapPos();
            if (colored) ImGui::PopStyleColor();
            return 0;
        }

        int UiButton(lua_State* L) {
            NeedWindow(L);
            const char* label = luaL_checkstring(L, 1);
            float width = Number(L, 2, "width", 0.0f), height = Number(L, 2, "height", 0.0f);
            lua_pushboolean(L, ImGui::Button(label, ImVec2(width, height)));
            return 1;
        }

        int UiCheckbox(lua_State* L) {
            NeedWindow(L);
            const char* label = luaL_checkstring(L, 1);
            bool value = lua_toboolean(L, 2) != 0;
            bool changed = ImGui::Checkbox(label, &value);
            lua_pushboolean(L, value);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiSlider(lua_State* L) {
            NeedWindow(L);
            const char* label = luaL_checkstring(L, 1);
            bool changed = false;
            if (lua_isinteger(L, 2) && lua_isinteger(L, 3) && lua_isinteger(L, 4)) {
                auto whole = [L](int index) { return (int)std::clamp<lua_Integer>(lua_tointeger(L, index), -1000000000, 1000000000); };
                int value = whole(2), low = whole(3), high = whole(4);
                changed = ImGui::SliderInt(label, &value, low, high);
                lua_pushinteger(L, value);
            }
            else {
                float value = (float)luaL_checknumber(L, 2), low = (float)luaL_checknumber(L, 3), high = (float)luaL_checknumber(L, 4);
                changed = ImGui::SliderFloat(label, &value, low, high);
                lua_pushnumber(L, value);
            }
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiInput(lua_State* L) {
            NeedWindow(L);
            const char* label = luaL_checkstring(L, 1);
            size_t length = 0;
            const char* text = luaL_optlstring(L, 2, "", &length);
            std::string hint = Text(L, 3, "hint");
            char buffer[InputLimit];
            size_t used = std::min(length, sizeof(buffer) - 1);
            std::memcpy(buffer, text, used);
            buffer[used] = 0;
            bool changed = hint.empty() ? ImGui::InputText(label, buffer, sizeof(buffer)) : ImGui::InputTextWithHint(label, hint.c_str(), buffer, sizeof(buffer));
            lua_pushstring(L, buffer);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiProgress(lua_State* L) {
            NeedWindow(L);
            float fraction = (float)luaL_checknumber(L, 1);
            std::string text = Text(L, 2, "text");
            float width = Number(L, 2, "width", -FLT_MIN);
            ImGui::ProgressBar(fraction, ImVec2(width, 0.0f), text.empty() ? nullptr : text.c_str());
            return 0;
        }

        int UiImage(lua_State* L) {
            NeedWindow(L);
            images::Image image = images::Get(L, luaL_checkstring(L, 1));
            float w = (float)luaL_optnumber(L, 2, image.width), h = (float)luaL_optnumber(L, 3, image.height);
            if (image.texture) ImGui::Image(ImTextureRef((ImTextureID)image.texture), ImVec2(w, h));
            else ImGui::Dummy(ImVec2(w, h));
            return 0;
        }

        int UiSeparator(lua_State* L) {
            NeedWindow(L);
            ImGui::Separator();
            return 0;
        }

        int UiSameLine(lua_State* L) {
            NeedWindow(L);
            ImGui::SameLine();
            return 0;
        }

        int UiSpacing(lua_State* L) {
            NeedWindow(L);
            ImGui::Spacing();
            return 0;
        }

        int ScreenSize(lua_State* L) {
            ImVec2 size = ImGui::GetCurrentContext() ? ImGui::GetIO().DisplaySize : ImVec2(0, 0);
            lua_pushinteger(L, (lua_Integer)size.x);
            lua_pushinteger(L, (lua_Integer)size.y);
            return 2;
        }

        int InputDown(lua_State* L) {
            auto found = keys.find(Lower(luaL_checkstring(L, 1)));
            lua_pushboolean(L, found != keys.end() && found->second.down);
            return 1;
        }

        int InputMouse(lua_State* L) {
            if (!ImGui::GetCurrentContext()) return 0;
            ImVec2 at = ImGui::GetIO().MousePos;
            if (!ImGui::IsMousePosValid(&at)) return 0;
            lua_pushinteger(L, (lua_Integer)at.x);
            lua_pushinteger(L, (lua_Integer)at.y);
            return 2;
        }

        int PushVector(lua_State* L, bool found, const float* value) {
            if (!found) return 0;
            for (int i = 0; i < 3; i++) lua_pushnumber(L, value[i]);
            return 3;
        }

        int PlayerPosition(lua_State* L) {
            float value[3];
            return PushVector(L, game::PlayerPosition(value), value);
        }

        int PlayerName(lua_State* L) {
            std::string name = game::PlayerName();
            if (name.empty()) return 0;
            lua_pushlstring(L, name.data(), name.size());
            return 1;
        }

        int CameraPosition(lua_State* L) {
            float value[3];
            return PushVector(L, game::CameraPosition(value), value);
        }

        int ServerSend(lua_State* L) {
            size_t nameLength = 0, dataLength = 0;
            const char* name = luaL_checklstring(L, 1, &nameLength);
            const char* data = luaL_optlstring(L, 2, "", &dataLength);
            bool valid = nameLength > 0 && nameLength <= 64;
            for (size_t i = 0; i < nameLength && valid; i++)
                valid = std::isalnum((unsigned char)name[i]) || std::strchr("_.:-", name[i]) != nullptr;
            if (!valid) return luaL_argerror(L, 1, "names are 1 to 64 letters, digits and _ . : -");
            if (dataLength > MessageLimit) return luaL_argerror(L, 2, "messages carry up to 64 KB");
            lua_pushboolean(L, sender && sender(std::string(name, nameLength), std::string(data, dataLength)));
            return 1;
        }

        void Library(lua_State* L, const char* name, const luaL_Reg* functions) {
            lua_newtable(L);
            luaL_setfuncs(L, functions, 0);
            lua_setglobal(L, name);
        }

        ImFont* LoadFont(const std::string& file) {
            const char* windows = std::getenv("WINDIR");
            std::filesystem::path path = std::filesystem::path(windows ? windows : "C:\\Windows") / "Fonts" / file;
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) return nullptr;
            return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), Default()->LegacySize);
        }
    }

    void Register(lua_State* L) {
        static const luaL_Reg draw[] = {
            {"rect", HudRect}, {"line", HudLine}, {"circle", HudCircle}, {"text", HudText},
            {"measure", HudMeasure}, {"image", HudImage}, {nullptr, nullptr},
        };
        static const luaL_Reg ui[] = {
            {"window", UiWindow}, {"style", UiStyle}, {"with", UiWith}, {"text", UiText}, {"button", UiButton}, {"checkbox", UiCheckbox},
            {"slider", UiSlider}, {"input", UiInput}, {"progress", UiProgress}, {"image", UiImage},
            {"separator", UiSeparator}, {"sameline", UiSameLine}, {"spacing", UiSpacing}, {nullptr, nullptr},
        };
        static const luaL_Reg screen[] = {{"size", ScreenSize}, {nullptr, nullptr}};
        static const luaL_Reg input[] = {{"down", InputDown}, {"mouse", InputMouse}, {nullptr, nullptr}};
        static const luaL_Reg player[] = {{"position", PlayerPosition}, {"name", PlayerName}, {nullptr, nullptr}};
        static const luaL_Reg camera[] = {{"position", CameraPosition}, {nullptr, nullptr}};
        static const luaL_Reg server[] = {{"send", ServerSend}, {nullptr, nullptr}};
        Library(L, "draw", draw);
        Library(L, "ui", ui);
        Library(L, "screen", screen);
        Library(L, "input", input);
        Library(L, "player", player);
        Library(L, "camera", camera);
        Library(L, "server", server);
        lua_pushcfunction(L, Rgb);
        lua_setglobal(L, "rgb");
    }

    void SetSender(Sender value) { sender = value; }

    void Style() {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        atlas->TexDesiredFormat = ImTextureFormat_Alpha8;
        atlas->TexMaxWidth = 4096;
        atlas->TexMaxHeight = 4096;
        fallback = atlas->AddFontDefault();
        ImGui::StyleColorsDark();
    }

    void LoadFonts() {
        for (const std::string& name : wanted) {
            ImFont* font = LoadFont(name);
            fonts[name] = font;
            if (!font) runtime::Log("there is no font " + name + " in the Windows Fonts folder");
        }
        wanted.clear();
    }

    void BeginDraw() {
        drawing = true;
        windows = 0;
    }

    void EndDraw() {
        drawing = false;
        windows = 0;
    }

    void Forget(const std::string& script) { windowsOf.erase(script); }

    void Key(const std::string& name, bool down) {
        Held& held = keys[Lower(name)];
        if (held.down == down) return;
        held.name = name;
        held.down = down;
        if (queue.size() < QueueLimit) queue.push_back({name, down});
    }

    void ReleaseKeys() {
        for (auto& entry : keys) {
            if (!entry.second.down) continue;
            entry.second.down = false;
            if (queue.size() < QueueLimit) queue.push_back({entry.second.name, false});
        }
    }

    std::vector<KeyEvent> TakeKeys() {
        std::vector<KeyEvent> taken;
        taken.swap(queue);
        return taken;
    }
}
