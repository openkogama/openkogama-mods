#include "api.h"
#include "runtime.h"
#include "game.h"
#include "build.h"
#include "tools.h"
#include "editor.h"
#include "files.h"
#include "images.h"
#include "frame.h"
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
        constexpr size_t TextLimit = 16u << 10;
        constexpr size_t QueueLimit = 256;
        constexpr size_t MessageLimit = 64u << 10;
        constexpr size_t WindowLimit = 32;
        constexpr size_t ElementLimit = 256;
        constexpr size_t PolyLimit = 256;
        constexpr size_t PolysLimit = 100000;
        constexpr size_t PointsLimit = 400000;
        constexpr double KeepSeconds = 0.1;

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

        struct Hiding {
            std::set<std::string> holders;
            std::map<int, game::Saved> saved;
        };

        std::map<std::string, Hiding> hidden;
        std::set<std::string> blockers;
        std::set<std::string> cursorHolders;
        bool cursorWasLocked = false;
        std::map<std::string, std::set<std::string>> consumedBy;
        std::map<std::string, std::string> buildOwners;
        std::map<std::string, std::string> toolOwners;
        std::map<std::string, std::string> editorOwners;
        std::map<std::string, std::string> flagOwners;
        const OkMod* host = nullptr;
        bool antiAfk = false;
        int tabBars = 0;
        int tables = 0;
        constexpr size_t ComboLimit = 1024;
        constexpr size_t ClipboardLimit = 1u << 20;
        constexpr size_t TableRows = 1024;
        constexpr int TableColumns = 16;
        std::set<std::string> gods;
        double wheelPending = 0;
        double wheelFrame = 0;
        double keepClock = 0;

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
            size_t length = 0;
            const char* text = lua_tolstring(L, -1, &length);
            std::string value = text ? std::string(text, std::min(length, TextLimit)) : "";
            lua_pop(L, 1);
            return value;
        }

        const char* Printed(lua_State* L, int index, size_t* length) {
            const char* text = luaL_tolstring(L, index, length);
            if (*length > TextLimit) luaL_argerror(L, index, "text is up to 16384 bytes");
            return text;
        }

        const char* Label(lua_State* L, int index) {
            size_t length = 0;
            const char* text = luaL_checklstring(L, index, &length);
            if (length > InputLimit) luaL_argerror(L, index, "labels are up to 1024 bytes");
            return text;
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
            const char* text = Printed(L, 1, &length);
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
            const char* text = Printed(L, 1, &length);
            ImFont* font = FontFor(L, luaL_optstring(L, 3, ""));
            ImVec2 extent = font->CalcTextSizeA(TextSize(L, 2, font), FLT_MAX, 0.0f, text, text + length);
            lua_pushnumber(L, extent.x);
            lua_pushnumber(L, extent.y);
            return 2;
        }

        bool Nearest(lua_State* L, int options) {
            std::string filter = Text(L, options, "filter");
            if (filter.empty() || filter == "linear") return false;
            if (filter != "nearest") luaL_error(L, "option 'filter' is \"linear\" or \"nearest\"");
            return true;
        }

        void Filtered(ImDrawList* list, bool nearest) {
            if (nearest) list->AddCallback(frame::Sampler, &frame::Nearest);
        }

        void Unfiltered(ImDrawList* list, bool nearest) {
            if (nearest) list->AddCallback(frame::Sampler, nullptr);
        }

        int HudImage(lua_State* L) {
            NeedDraw(L);
            images::Image image = images::Get(L, luaL_checkstring(L, 1));
            float x = Coordinate(L, 2), y = Coordinate(L, 3);
            float w = (float)luaL_optnumber(L, 4, image.width), h = (float)luaL_optnumber(L, 5, image.height);
            ImU32 tint = ColorOption(L, 6, "color", IM_COL32_WHITE);
            float rounding = Number(L, 6, "rounding", 0.0f);
            bool nearest = Nearest(L, 6);
            if (!image.texture) return 0;
            ImTextureRef texture((ImTextureID)image.texture);
            Filtered(Hud(), nearest);
            if (rounding > 0) Hud()->AddImageRounded(texture, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint, rounding);
            else Hud()->AddImage(texture, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
            Unfiltered(Hud(), nearest);
            return 0;
        }

        std::vector<ImVec2> Points(lua_State* L, int index, size_t first, size_t count) {
            std::vector<ImVec2> points(count);
            for (size_t i = 0; i < count; i++) {
                lua_rawgeti(L, index, (lua_Integer)(first + i * 2 + 1));
                lua_rawgeti(L, index, (lua_Integer)(first + i * 2 + 2));
                if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1)) luaL_error(L, "points are numbers x1, y1, x2, y2, ...");
                points[i] = ImVec2((float)lua_tonumber(L, -2), (float)lua_tonumber(L, -1));
                lua_pop(L, 2);
            }
            return points;
        }

        void Fill(const std::vector<ImVec2>& points, size_t first, size_t count, ImU32 color, bool smooth) {
            ImDrawList* list = Hud();
            ImDrawListFlags flags = list->Flags;
            if (!smooth) list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            list->AddConvexPolyFilled(points.data() + first, (int)count, color);
            list->Flags = flags;
        }

        int HudPoly(lua_State* L) {
            NeedDraw(L);
            luaL_checktype(L, 1, LUA_TTABLE);
            size_t length = lua_rawlen(L, 1);
            if (length % 2 != 0 || length < 6 || length > PolyLimit * 2) return luaL_argerror(L, 1, "a polygon is 3 to 256 points as x1, y1, x2, y2, ...");
            ImU32 color = Color(L, 2, IM_COL32_WHITE);
            bool smooth = Flag(L, 3, "smooth", true);
            std::vector<ImVec2> points = Points(L, 1, 0, length / 2);
            Fill(points, 0, points.size(), color, smooth);
            return 0;
        }

        int HudPolys(lua_State* L) {
            NeedDraw(L);
            luaL_checktype(L, 1, LUA_TTABLE);
            luaL_checktype(L, 3, LUA_TTABLE);
            size_t polygons = lua_rawlen(L, 3), length = lua_rawlen(L, 1);
            if (polygons > PolysLimit) return luaL_argerror(L, 3, "up to 100000 polygons a call");
            if (length % 2 != 0 || length > PointsLimit * 2) return luaL_argerror(L, 1, "up to 400000 points as x1, y1, x2, y2, ...");
            bool shared = !lua_istable(L, 2);
            ImU32 color = shared ? Color(L, 2, IM_COL32_WHITE) : IM_COL32_WHITE;
            if (!shared && lua_rawlen(L, 2) < polygons) return luaL_argerror(L, 2, "one color for each polygon, or one color for all");
            bool smooth = Flag(L, 4, "smooth", false);
            std::vector<size_t> counts(polygons);
            size_t total = 0;
            for (size_t i = 0; i < polygons; i++) {
                lua_rawgeti(L, 3, (lua_Integer)i + 1);
                lua_Integer count = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
                lua_pop(L, 1);
                if (count < 3 || count > (lua_Integer)PolyLimit) return luaL_argerror(L, 3, "every polygon has 3 to 256 points");
                counts[i] = (size_t)count;
                total += counts[i];
            }
            if (total * 2 != length) return luaL_argerror(L, 1, "the points do not add up to the counts");
            std::vector<ImVec2> points = Points(L, 1, 0, total);
            size_t first = 0;
            for (size_t i = 0; i < polygons; i++) {
                if (!shared) {
                    lua_rawgeti(L, 2, (lua_Integer)i + 1);
                    color = Color(L, lua_gettop(L), IM_COL32_WHITE);
                    lua_pop(L, 1);
                }
                Fill(points, first, counts[i], color, smooth);
                first += counts[i];
            }
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
            std::string id = WindowId(L, Label(L, 1));
            luaL_checktype(L, 2, LUA_TFUNCTION);
            std::set<std::string>& known = windowsOf[runtime::Name(L)];
            if (!known.count(id)) {
                if (known.size() >= WindowLimit) return luaL_error(L, "a script can have up to %d windows", (int)WindowLimit);
                known.insert(id);
            }
            float x = Number(L, 3, "x", NAN), y = Number(L, 3, "y", NAN);
            float width = Number(L, 3, "width", 0.0f), height = Number(L, 3, "height", 0.0f);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
            if (!Flag(L, 3, "collapsible", false)) flags |= ImGuiWindowFlags_NoCollapse;
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
            const char* text = Printed(L, 1, &length);
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
            const char* label = Label(L, 1);
            float width = Number(L, 2, "width", 0.0f), height = Number(L, 2, "height", 0.0f);
            lua_pushboolean(L, ImGui::Button(label, ImVec2(width, height)));
            return 1;
        }

        int UiCheckbox(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            bool value = lua_toboolean(L, 2) != 0;
            bool changed = ImGui::Checkbox(label, &value);
            lua_pushboolean(L, value);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiSlider(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
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
            const char* label = Label(L, 1);
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
            bool nearest = Nearest(L, 4);
            if (!image.texture) {
                ImGui::Dummy(ImVec2(w, h));
                return 0;
            }
            Filtered(ImGui::GetWindowDrawList(), nearest);
            ImGui::Image(ImTextureRef((ImTextureID)image.texture), ImVec2(w, h));
            Unfiltered(ImGui::GetWindowDrawList(), nearest);
            return 0;
        }

        int UiSeparator(lua_State* L) {
            NeedWindow(L);
            ImGui::Separator();
            return 0;
        }

        int UiSameLine(lua_State* L) {
            NeedWindow(L);
            ImGui::SameLine((float)luaL_optnumber(L, 1, 0));
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

        int PlayerHealth(lua_State* L) {
            float health = 0, most = 0;
            if (!game::PlayerHealth(health, most)) return 0;
            lua_pushnumber(L, health);
            lua_pushnumber(L, most);
            return 2;
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

        int ServerFile(lua_State* L) {
            std::string id = luaL_checkstring(L, 1);
            if (!images::Served(id)) return luaL_argerror(L, 1, "a file id is the 64 hex digits the server gives");
            images::State state = images::Want(id);
            lua_pushstring(L, state == images::State::Ready ? "ready" : state == images::State::Loading ? "loading" : "failed");
            return 1;
        }

        std::string Element(lua_State* L, int index) {
            size_t length = 0;
            const char* text = luaL_optlstring(L, index, "", &length);
            if (length > ElementLimit) luaL_argerror(L, index, "names are up to 256 characters");
            for (size_t i = 0; i < length; i++)
                if ((unsigned char)text[i] < 32) luaL_argerror(L, index, "names are plain text");
            return std::string(text, length);
        }

        bool Visible(const std::vector<game::Part>& parts) {
            return std::any_of(parts.begin(), parts.end(), [](const game::Part& part) { return part.shown; });
        }

        bool Enabled(const std::vector<game::Part>& parts) {
            return std::any_of(parts.begin(), parts.end(), [](const game::Part& part) { return part.active; });
        }

        void Release(const std::string& element, const std::string& script) {
            auto found = hidden.find(element);
            if (found == hidden.end() || !found->second.holders.erase(script) || !found->second.holders.empty()) return;
            std::map<int, game::Saved> saved = std::move(found->second.saved);
            hidden.erase(found);
            game::Restore(element, saved);
        }

        int HudHide(lua_State* L) {
            std::string element = Element(L, 1);
            std::vector<game::Part> parts = game::Parts(element);
            if (parts.empty()) {
                lua_pushboolean(L, 0);
                return 1;
            }
            Hiding& hiding = hidden[element];
            hiding.holders.insert(runtime::Name(L));
            game::Hide(element, hiding.saved);
            lua_pushboolean(L, 1);
            return 1;
        }

        int HudShow(lua_State* L) {
            std::string element = Element(L, 1);
            auto found = hidden.find(element);
            if (found != hidden.end()) {
                found->second.holders.erase(runtime::Name(L));
                if (!found->second.holders.empty()) {
                    lua_pushboolean(L, 0);
                    return 1;
                }
                std::map<int, game::Saved> saved = std::move(found->second.saved);
                hidden.erase(found);
                game::Restore(element, saved);
            }
            lua_pushboolean(L, !game::Parts(element).empty());
            return 1;
        }

        int HudShown(lua_State* L) {
            std::vector<game::Part> parts = game::Parts(Element(L, 1));
            if (parts.empty()) lua_pushnil(L);
            else lua_pushboolean(L, Visible(parts));
            return 1;
        }

        int HudActive(lua_State* L) {
            std::vector<game::Part> parts = game::Parts(Element(L, 1));
            if (parts.empty()) lua_pushnil(L);
            else lua_pushboolean(L, Enabled(parts));
            return 1;
        }

        int InputBlock(lua_State* L) {
            if (lua_toboolean(L, 1)) blockers.insert(runtime::Name(L));
            else blockers.erase(runtime::Name(L));
            return 0;
        }

        void ReleaseCursor(const std::string& script) {
            if (cursorHolders.erase(script) && cursorHolders.empty()) game::ReturnCursor(cursorWasLocked);
        }

        int InputCursor(lua_State* L) {
            if (!lua_toboolean(L, 1)) {
                ReleaseCursor(runtime::Name(L));
                return 0;
            }
            if (cursorHolders.empty()) cursorWasLocked = game::CursorLocked();
            cursorHolders.insert(runtime::Name(L));
            game::FreeCursor();
            return 0;
        }

        int InputWheel(lua_State* L) {
            lua_pushnumber(L, wheelFrame);
            return 1;
        }

        int InputConsume(lua_State* L) {
            std::string key = Lower(luaL_checkstring(L, 1));
            bool on = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
            std::set<std::string>& keysOf = consumedBy[runtime::Name(L)];
            if (on) keysOf.insert(key);
            else keysOf.erase(key);
            return 0;
        }

        int InputLocked(lua_State* L) {
            lua_pushboolean(L, game::CursorLocked());
            return 1;
        }

        int InterfaceClick(lua_State* L) {
            lua_pushboolean(L, game::Click(Element(L, 1)));
            return 1;
        }

        int ScreenFullscreen(lua_State* L) {
            if (!lua_isnoneornil(L, 1)) game::SetFullscreen(lua_toboolean(L, 1));
            lua_pushboolean(L, game::Fullscreen());
            return 1;
        }

        int SoundMuted(lua_State* L) {
            if (!lua_isnoneornil(L, 1)) game::SetMuted(lua_toboolean(L, 1));
            lua_pushboolean(L, game::Muted());
            return 1;
        }

        int InterfaceText(lua_State* L) {
            std::vector<std::string> texts;
            if (!game::Texts(Element(L, 1), texts)) {
                lua_pushnil(L);
                return 1;
            }
            lua_createtable(L, (int)texts.size(), 0);
            for (size_t i = 0; i < texts.size(); i++) {
                lua_pushlstring(L, texts[i].data(), texts[i].size());
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            return 1;
        }

        int HudList(lua_State* L) {
            std::vector<std::string> names;
            if (!game::Children(Element(L, 1), names)) {
                lua_pushnil(L);
                return 1;
            }
            lua_createtable(L, (int)names.size(), 0);
            for (size_t i = 0; i < names.size(); i++) {
                lua_pushlstring(L, names[i].data(), names[i].size());
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            return 1;
        }

        bool Off(lua_State* L, int index) { return lua_isnoneornil(L, index) || (lua_isboolean(L, index) && !lua_toboolean(L, index)); }

        int Toggle(lua_State* L, const char* setting, bool (*get)(), void (*set)(bool)) {
            if (lua_gettop(L) > 0) {
                set(lua_toboolean(L, 1));
                buildOwners[setting] = runtime::Name(L);
            }
            lua_pushboolean(L, get());
            return 1;
        }

        int Amount(lua_State* L, const char* setting, bool (*get)(double&), void (*set)(bool, double), double least, double most) {
            if (lua_gettop(L) > 0) {
                if (Off(L, 1)) set(false, 0);
                else {
                    double value = luaL_checknumber(L, 1);
                    luaL_argcheck(L, value >= least && value <= most, 1, "out of range");
                    set(true, value);
                }
                buildOwners[setting] = runtime::Name(L);
            }
            double value = 0;
            if (!get(value)) return 0;
            lua_pushnumber(L, value);
            return 1;
        }

        int BuildLimit(lua_State* L) { return Toggle(L, "limit", build::Limit, build::SetLimit); }
        int BuildMaterials(lua_State* L) { return Toggle(L, "materials", build::Materials, build::SetMaterials); }
        int BuildMuzzle(lua_State* L) { return Toggle(L, "muzzle", build::Muzzle, build::SetMuzzle); }
        int BuildFace(lua_State* L) { return Toggle(L, "face", build::Face, build::SetFace); }
        int BuildBlue(lua_State* L) { return Toggle(L, "blue", build::Blue, build::SetBlue); }
        int BuildGrid(lua_State* L) { return Amount(L, "grid", build::Grid, build::SetGrid, 0.001, 1000); }
        int BuildScale(lua_State* L) { return Amount(L, "scale", build::Scale, build::SetScale, 0.001, 1000); }
        int BuildStep(lua_State* L) { return Amount(L, "step", build::Step, build::SetStep, 0.001, 360); }
        int BuildSpeed(lua_State* L) { return Amount(L, "speed", build::Speed, build::SetSpeed, 0.001, 1000); }

        int BuildSliders(lua_State* L) {
            if (lua_gettop(L) > 0) {
                if (Off(L, 1)) build::SetSliders(false, 0, 1);
                else {
                    double low = luaL_checknumber(L, 1), high = luaL_checknumber(L, 2);
                    luaL_argcheck(L, std::isfinite(low) && std::isfinite(high) && low <= high && std::fabs(low) <= 1e6 && std::fabs(high) <= 1e6, 2, "bad range");
                    build::SetSliders(true, low, high);
                    if (!lua_isnoneornil(L, 3)) {
                        build::SetSlidersClamp(lua_toboolean(L, 3) != 0);
                        buildOwners["clamp"] = runtime::Name(L);
                    }
                }
                buildOwners["sliders"] = runtime::Name(L);
            }
            double low, high;
            if (!build::Sliders(low, high)) return 0;
            lua_pushnumber(L, low);
            lua_pushnumber(L, high);
            return 2;
        }

        int PlayerGod(lua_State* L) {
            if (lua_gettop(L) > 0) {
                if (lua_toboolean(L, 1)) gods.insert(runtime::Name(L));
                else gods.erase(runtime::Name(L));
            }
            lua_pushboolean(L, !gods.empty());
            return 1;
        }

        int PlayerTeleport(lua_State* L) {
            float value[3];
            for (int i = 0; i < 3; i++) {
                value[i] = (float)luaL_checknumber(L, i + 1);
                luaL_argcheck(L, std::isfinite(value[i]) && std::fabs(value[i]) <= 1e6, i + 1, "bad coordinate");
            }
            lua_pushboolean(L, game::SetPlayerPosition(value));
            return 1;
        }

        std::string Shown(const char* label) {
            std::string text = label;
            size_t at = text.find("##");
            return at == std::string::npos ? text : text.substr(0, at);
        }

        int UiTabs(lua_State* L) {
            NeedWindow(L);
            const char* id = Label(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            int status = LUA_OK;
            if (ImGui::BeginTabBar(id)) {
                tabBars++;
                status = Guarded(L, 2);
                tabBars--;
                ImGui::EndTabBar();
            }
            if (status != LUA_OK) return lua_error(L);
            return 0;
        }

        int UiTab(lua_State* L) {
            NeedWindow(L);
            if (tabBars == 0) return luaL_error(L, "ui.tab works inside ui.tabs");
            const char* label = Label(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            bool open = ImGui::BeginTabItem(label);
            int status = LUA_OK;
            if (open) {
                status = Guarded(L, 2);
                ImGui::EndTabItem();
            }
            if (status != LUA_OK) return lua_error(L);
            lua_pushboolean(L, open);
            return 1;
        }

        int UiCombo(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            int current = (int)luaL_checkinteger(L, 2) - 1;
            luaL_checktype(L, 3, LUA_TTABLE);
            size_t count = lua_rawlen(L, 3);
            if (count > ComboLimit) return luaL_error(L, "a combo takes up to %d items", (int)ComboLimit);
            std::vector<std::string> items;
            for (size_t i = 1; i <= count; i++) {
                lua_rawgeti(L, 3, (lua_Integer)i);
                size_t length = 0;
                const char* text = luaL_tolstring(L, -1, &length);
                items.emplace_back(text, length);
                lua_pop(L, 2);
            }
            std::vector<const char*> pointers;
            for (const std::string& item : items) pointers.push_back(item.c_str());
            bool changed = false;
            if (!items.empty()) {
                current = std::clamp(current, 0, (int)items.size() - 1);
                changed = ImGui::Combo(label, &current, pointers.data(), (int)pointers.size());
            }
            lua_pushinteger(L, items.empty() ? 0 : current + 1);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiColor(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            ImVec4 color = ImGui::ColorConvertU32ToFloat4(Color(L, 2, IM_COL32_WHITE));
            float values[4] = {color.x, color.y, color.z, color.w};
            bool changed = ImGui::ColorEdit4(label, values);
            char text[16];
            auto byte = [](float value) { return (int)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f); };
            std::snprintf(text, sizeof(text), "#%02x%02x%02x%02x", byte(values[0]), byte(values[1]), byte(values[2]), byte(values[3]));
            lua_pushstring(L, text);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiNumber(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            float value = (float)luaL_checknumber(L, 2);
            float step = Number(L, 3, "step", 0.0f);
            std::string shown = Shown(label);
            ImGui::PushID(label);
            ImGui::TextUnformatted(shown.c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            bool changed = ImGui::InputFloat("", &value, step);
            ImGui::PopID();
            if (!std::isfinite(value)) value = 0;
            lua_pushnumber(L, value);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiInteger(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            int value = (int)std::clamp<lua_Integer>(luaL_checkinteger(L, 2), -1000000000, 1000000000);
            int step = (int)Number(L, 3, "step", 1.0f);
            std::string shown = Shown(label);
            ImGui::PushID(label);
            ImGui::TextUnformatted(shown.c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            bool changed = ImGui::InputInt("", &value, step);
            ImGui::PopID();
            lua_pushinteger(L, value);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiTree(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            bool open = ImGui::TreeNode(label);
            int status = LUA_OK;
            if (open) {
                status = Guarded(L, 2);
                ImGui::TreePop();
            }
            if (status != LUA_OK) return lua_error(L);
            lua_pushboolean(L, open);
            return 1;
        }

        int UiHeading(lua_State* L) {
            NeedWindow(L);
            ImGui::SeparatorText(luaL_checkstring(L, 1));
            return 0;
        }

        int UiTable(lua_State* L) {
            NeedWindow(L);
            const char* id = Label(L, 1);
            luaL_checktype(L, 2, LUA_TTABLE);
            luaL_checktype(L, 3, LUA_TTABLE);
            int columns = (int)lua_rawlen(L, 2);
            if (columns < 1 || columns > TableColumns) return luaL_error(L, "a table has 1 to %d columns", TableColumns);
            size_t rows = lua_rawlen(L, 3);
            if (rows > TableRows) return luaL_error(L, "a table shows up to %d rows", (int)TableRows);
            if (!ImGui::BeginTable(id, columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) return 0;
            for (int column = 1; column <= columns; column++) {
                lua_rawgeti(L, 2, column);
                ImGui::TableSetupColumn(luaL_tolstring(L, -1, nullptr));
                lua_pop(L, 2);
            }
            ImGui::TableHeadersRow();
            for (size_t row = 1; row <= rows; row++) {
                lua_rawgeti(L, 3, (lua_Integer)row);
                ImGui::TableNextRow();
                if (lua_istable(L, -1)) {
                    for (int column = 1; column <= columns; column++) {
                        ImGui::TableSetColumnIndex(column - 1);
                        lua_rawgeti(L, -1, column);
                        if (!lua_isnil(L, -1)) {
                            size_t length = 0;
                            const char* text = luaL_tolstring(L, -1, &length);
                            ImGui::TextUnformatted(text, text + length);
                            lua_pop(L, 1);
                        }
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            ImGui::EndTable();
            return 0;
        }

        int UiSmall(lua_State* L) {
            NeedWindow(L);
            lua_pushboolean(L, ImGui::SmallButton(luaL_checkstring(L, 1)));
            return 1;
        }

        int UiWidth(lua_State* L) {
            NeedWindow(L);
            ImGui::SetNextItemWidth((float)luaL_checknumber(L, 1));
            return 0;
        }

        void OwnTool(lua_State* L, const char* setting) { toolOwners[setting] = runtime::Name(L); }

        void DropTool(const char* setting) {
            tools::Clear(setting);
            toolOwners.erase(setting);
        }

        int EditorTarget(lua_State* L) {
            int id = 0;
            if (!tools::Target(id)) return 0;
            lua_pushinteger(L, id);
            return 1;
        }

        int EditorSelected(lua_State* L) {
            std::vector<int> ids;
            if (!tools::Selected(ids)) return 0;
            lua_createtable(L, (int)ids.size(), 0);
            for (size_t i = 0; i < ids.size(); i++) {
                lua_pushinteger(L, ids[i]);
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            return 1;
        }

        int EditorMaterial(lua_State* L) {
            if (!lua_isnoneornil(L, 1)) {
                lua_Integer id = luaL_checkinteger(L, 1);
                luaL_argcheck(L, id >= 0 && id <= 255, 1, "a material is a number from 0 to 255");
                tools::SetMaterial((int)id);
            }
            int id = 0;
            if (!tools::Material(id)) return 0;
            lua_pushinteger(L, id);
            return 1;
        }

        int HeldNumber(lua_State* L, const char* setting, void (*hold)(float), bool (*read)(float&), double least, double most) {
            if (lua_gettop(L) > 0) {
                if (Off(L, 1)) DropTool(setting);
                else {
                    double value = luaL_checknumber(L, 1);
                    luaL_argcheck(L, value >= least && value <= most, 1, "out of range");
                    hold((float)value);
                    OwnTool(L, setting);
                }
            }
            float value = 0;
            if (!read(value)) return 0;
            lua_pushnumber(L, value);
            return 1;
        }

        int CameraFov(lua_State* L) { return HeldNumber(L, "fov", tools::HoldFov, tools::Fov, 1, 179); }
        int CameraDistance(lua_State* L) { return HeldNumber(L, "distance", tools::HoldDistance, tools::Distance, 0.1, 1000); }

        int CameraFar(lua_State* L) {
            if (!lua_isnoneornil(L, 1)) {
                double value = luaL_checknumber(L, 1);
                luaL_argcheck(L, value >= 1 && value <= 100000, 1, "out of range");
                tools::SetFar((float)value);
            }
            float value = 0;
            if (!tools::Far(value)) return 0;
            lua_pushnumber(L, value);
            return 1;
        }

        int CameraOrthographic(lua_State* L) {
            bool on = lua_toboolean(L, 1) != 0;
            double size = luaL_optnumber(L, 2, 10);
            luaL_argcheck(L, size >= 0.1 && size <= 10000, 2, "out of range");
            lua_pushboolean(L, tools::SetOrthographic(on, (float)size));
            return 1;
        }

        int CameraShadows(lua_State* L) {
            if (lua_isnoneornil(L, 1)) {
                float current = 0;
                if (!tools::Shadows(current)) return 0;
                lua_pushnumber(L, current);
                return 1;
            }
            double value = luaL_checknumber(L, 1);
            luaL_argcheck(L, value >= 0 && value <= 100000, 1, "out of range");
            lua_pushboolean(L, tools::SetShadows((float)value));
            return 1;
        }

        int GraphicsFog(lua_State* L) {
            if (lua_gettop(L) > 0) {
                if (lua_isnil(L, 1)) DropTool("fog");
                else {
                    bool on = lua_toboolean(L, 1) != 0;
                    bool withDensity = !lua_isnoneornil(L, 2);
                    double density = withDensity ? luaL_checknumber(L, 2) : 0;
                    luaL_argcheck(L, density >= 0 && density <= 1, 2, "a density is a number from 0 to 1");
                    tools::HoldFog(on, withDensity, (float)density);
                    OwnTool(L, "fog");
                }
            }
            bool on = false;
            if (!tools::Fog(on)) return 0;
            lua_pushboolean(L, on);
            return 1;
        }

        int GraphicsWater(lua_State* L) {
            if (!lua_isnoneornil(L, 1)) tools::SetWater(lua_toboolean(L, 1) != 0);
            bool reflective = false;
            if (!tools::Water(reflective)) return 0;
            lua_pushboolean(L, reflective);
            return 1;
        }

        int ScreenResolution(lua_State* L) {
            lua_Integer width = luaL_checkinteger(L, 1), height = luaL_checkinteger(L, 2);
            luaL_argcheck(L, width >= 320 && width <= 16384, 1, "a width is a number from 320 to 16384");
            luaL_argcheck(L, height >= 200 && height <= 16384, 2, "a height is a number from 200 to 16384");
            int mode = lua_isboolean(L, 3) || lua_isnoneornil(L, 3) ? (lua_toboolean(L, 3) ? 1 : 3) : (int)luaL_checkinteger(L, 3);
            luaL_argcheck(L, mode >= 0 && mode <= 3, 3, "a window mode is a number from 0 to 3");
            lua_pushboolean(L, tools::SetResolution((int)width, (int)height, mode));
            return 1;
        }

        int PlayerRespawn(lua_State* L) {
            lua_pushboolean(L, tools::Respawn(lua_toboolean(L, 1) != 0));
            return 1;
        }

        int GameAntiAfk(lua_State* L) {
            if (lua_gettop(L) > 0) {
                antiAfk = lua_toboolean(L, 1) != 0;
                tools::HoldAntiAfk(antiAfk);
                if (antiAfk) OwnTool(L, "antiafk");
                else toolOwners.erase("antiafk");
            }
            lua_pushboolean(L, antiAfk);
            return 1;
        }

        int GamePing(lua_State* L) {
            int milliseconds = 0;
            if (!tools::Ping(milliseconds)) return 0;
            lua_pushinteger(L, milliseconds);
            return 1;
        }

        int GameMode(lua_State* L) {
            std::string mode = tools::Mode();
            if (mode.empty()) return 0;
            lua_pushstring(L, mode.c_str());
            return 1;
        }

        int HudColor(lua_State* L) {
            std::string element = Lower(luaL_checkstring(L, 1));
            luaL_argcheck(L, element == "crosshair", 1, "only the crosshair takes a color");
            if (Off(L, 2)) {
                DropTool("crosshair");
                lua_pushboolean(L, false);
                return 1;
            }
            ImVec4 color = ImGui::ColorConvertU32ToFloat4(Color(L, 2, IM_COL32_WHITE));
            float rgba[4] = {color.x, color.y, color.z, color.w};
            tools::HoldCrosshair(true, rgba);
            OwnTool(L, "crosshair");
            lua_pushboolean(L, game::TintCrosshair(rgba));
            return 1;
        }

        float ButtonWidth(const char* label) {
            std::string shown = Shown(label);
            return ImGui::CalcTextSize(shown.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        }

        int UiReserved(lua_State* L) {
            NeedWindow(L);
            int count = lua_gettop(L);
            float total = 0;
            for (int i = 1; i <= count; i++) total += ButtonWidth(luaL_checkstring(L, i));
            if (count > 0) total += ImGui::GetStyle().ItemSpacing.x * count;
            lua_pushnumber(L, total);
            return 1;
        }

        int UiLabelWidth(lua_State* L) {
            NeedWindow(L);
            std::string shown = Shown(luaL_checkstring(L, 1));
            lua_pushnumber(L, ImGui::CalcTextSize(shown.c_str()).x + ImGui::GetStyle().FramePadding.x);
            return 1;
        }

        int UiShared(lua_State* L) {
            NeedWindow(L);
            int count = (int)luaL_checkinteger(L, 1);
            float content = ImGui::GetContentRegionAvail().x - (float)luaL_optnumber(L, 2, 0);
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            lua_pushnumber(L, count > 0 ? (content - spacing * (count - 1)) / count : 0);
            return 1;
        }

        int UiAvail(lua_State* L) {
            NeedWindow(L);
            lua_pushnumber(L, ImGui::GetContentRegionAvail().x);
            return 1;
        }

        int UiDrag(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            int value = (int)std::clamp<lua_Integer>(luaL_checkinteger(L, 2), -1000000000, 1000000000);
            bool changed = ImGui::DragInt(label, &value);
            lua_pushinteger(L, value);
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiNumbers(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            luaL_checktype(L, 2, LUA_TTABLE);
            int count = (int)lua_rawlen(L, 2);
            luaL_argcheck(L, count >= 2 && count <= 4, 2, "2 to 4 numbers");
            float values[4] = {};
            for (int i = 0; i < count; i++) {
                lua_rawgeti(L, 2, i + 1);
                values[i] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
                lua_pop(L, 1);
            }
            bool changed = count == 2 ? ImGui::InputFloat2(label, values) : count == 3 ? ImGui::InputFloat3(label, values) : ImGui::InputFloat4(label, values);
            lua_createtable(L, count, 0);
            for (int i = 0; i < count; i++) {
                lua_pushnumber(L, std::isfinite(values[i]) ? values[i] : 0.0f);
                lua_rawseti(L, -2, i + 1);
            }
            lua_pushboolean(L, changed);
            return 2;
        }

        int UiColumns(lua_State* L) {
            NeedWindow(L);
            const char* id = Label(L, 1);
            int count = (int)luaL_checkinteger(L, 2);
            luaL_argcheck(L, count >= 1 && count <= TableColumns, 2, "a table has 1 to 16 columns");
            luaL_checktype(L, 3, LUA_TFUNCTION);
            ImGuiTableFlags flags = 0;
            if (Flag(L, 4, "borders", true)) flags |= ImGuiTableFlags_Borders;
            if (Flag(L, 4, "rowbg", false)) flags |= ImGuiTableFlags_RowBg;
            if (Flag(L, 4, "resizable", false)) flags |= ImGuiTableFlags_Resizable;
            std::vector<std::string> headers;
            if (Field(L, 4, "headers")) {
                if (lua_istable(L, -1))
                    for (int c = 1; c <= count; c++) {
                        lua_rawgeti(L, -1, c);
                        headers.emplace_back(lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
                        lua_pop(L, 1);
                    }
                lua_pop(L, 1);
            }
            if (!ImGui::BeginTable(id, count, flags)) return 0;
            for (const std::string& header : headers) ImGui::TableSetupColumn(header.c_str());
            if (!headers.empty()) ImGui::TableHeadersRow();
            tables++;
            int status = Guarded(L, 3);
            tables--;
            ImGui::EndTable();
            if (status != LUA_OK) return lua_error(L);
            return 0;
        }

        int UiCell(lua_State* L) {
            NeedWindow(L);
            if (!tables) return luaL_error(L, "ui.cell works inside ui.columns");
            if (lua_isinteger(L, 1)) ImGui::TableSetColumnIndex((int)lua_tointeger(L, 1) - 1);
            else ImGui::TableNextColumn();
            return 0;
        }

        int UiRow(lua_State* L) {
            NeedWindow(L);
            if (!tables) return luaL_error(L, "ui.row works inside ui.columns");
            ImGui::TableNextRow();
            return 0;
        }

        struct RichRun {
            std::string text;
            ImU32 color;
            bool bold;
            float size;
        };

        bool RichColor(std::string value, ImU32& color) {
            static const std::map<std::string, unsigned> named = {
                {"aqua", 0x00ffffff}, {"black", 0x000000ff}, {"blue", 0x0000ffff}, {"brown", 0xa52a2aff}, {"cyan", 0x00ffffff},
                {"darkblue", 0x0000a0ff}, {"fuchsia", 0xff00ffff}, {"green", 0x008000ff}, {"grey", 0x808080ff}, {"gray", 0x808080ff},
                {"lightblue", 0xadd8e6ff}, {"lime", 0x00ff00ff}, {"magenta", 0xff00ffff}, {"maroon", 0x800000ff}, {"navy", 0x000080ff},
                {"olive", 0x808000ff}, {"orange", 0xffa500ff}, {"purple", 0x800080ff}, {"red", 0xff0000ff}, {"silver", 0xc0c0c0ff},
                {"teal", 0x008080ff}, {"white", 0xffffffff}, {"yellow", 0xffff00ff},
            };
            if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) value = value.substr(1, value.size() - 2);
            std::string lower = Lower(value);
            auto found = named.find(lower);
            if (found != named.end()) {
                color = Pack(found->second);
                return true;
            }
            if (lower.empty() || lower[0] != '#') return false;
            std::string hex = lower.substr(1);
            if (hex.size() == 3 || hex.size() == 4) {
                std::string longer;
                for (char c : hex) longer += std::string(2, c);
                hex = longer;
            }
            if (hex.size() != 6 && hex.size() != 8) return false;
            unsigned packed = 0;
            for (char c : hex) {
                int digit = Hex(c);
                if (digit < 0) return false;
                packed = packed * 16 + (unsigned)digit;
            }
            if (hex.size() == 6) packed = (packed << 8) | 0xFF;
            color = Pack(packed);
            return true;
        }

        std::vector<std::vector<RichRun>> ParseRich(const std::string& text, ImU32 base, float size) {
            std::vector<std::vector<RichRun>> lines(1);
            std::vector<ImU32> colors{base};
            std::vector<float> sizes{size};
            int bold = 0;
            std::string current;
            auto flush = [&] {
                if (!current.empty()) lines.back().push_back({current, colors.back(), bold > 0, sizes.back()});
                current.clear();
            };
            for (size_t i = 0; i < text.size();) {
                char c = text[i];
                if (c == '\n') {
                    flush();
                    lines.emplace_back();
                    i++;
                    continue;
                }
                if (c == '<') {
                    size_t close = text.find('>', i);
                    if (close != std::string::npos && close - i <= 40) {
                        std::string tag = text.substr(i + 1, close - i - 1);
                        std::string lower = Lower(tag);
                        bool known = true;
                        ImU32 color = 0;
                        if (lower == "b") { flush(); bold++; }
                        else if (lower == "/b") { flush(); if (bold > 0) bold--; }
                        else if (lower == "i" || lower == "/i" || lower == "/material" || (lower == "/size" && sizes.size() == 1)) flush();
                        else if (lower.rfind("color=", 0) == 0 && RichColor(tag.substr(6), color)) { flush(); colors.push_back(color); }
                        else if (lower == "/color") { flush(); if (colors.size() > 1) colors.pop_back(); }
                        else if (lower.rfind("size=", 0) == 0 && std::atof(tag.c_str() + 5) > 0) {
                            flush();
                            sizes.push_back(std::clamp((float)std::atof(tag.c_str() + 5), SmallestText, LargestText));
                        }
                        else if (lower == "/size") { flush(); sizes.pop_back(); }
                        else if (lower.rfind("material=", 0) == 0 || lower.rfind("quad", 0) == 0) flush();
                        else known = false;
                        if (known) {
                            i = close + 1;
                            continue;
                        }
                    }
                }
                current += c;
                i++;
            }
            flush();
            return lines;
        }

        int UiRich(lua_State* L) {
            NeedWindow(L);
            size_t length = 0;
            const char* text = luaL_checklstring(L, 1, &length);
            bool wrap = Flag(L, 2, "wrap", true);
            ImU32 base = ColorOption(L, 2, "color", ImGui::GetColorU32(ImGuiCol_Text));
            float limit = wrap ? std::max(1.0f, ImGui::GetContentRegionAvail().x) : FLT_MAX;
            ImDrawList* list = ImGui::GetWindowDrawList();
            ImGui::BeginGroup();
            for (const auto& line : ParseRich(std::string(text, length), base, ImGui::GetFontSize())) {
                if (line.empty()) {
                    ImGui::TextUnformatted("");
                    continue;
                }
                float x = 0;
                bool first = true;
                for (const RichRun& run : line) {
                    ImGui::PushFont(nullptr, run.size);
                    for (size_t start = 0; start < run.text.size();) {
                        size_t space = run.text.find(' ', start);
                        size_t end = space == std::string::npos ? run.text.size() : run.text.find_first_not_of(' ', space);
                        if (end == std::string::npos) end = run.text.size();
                        const char* from = run.text.data() + start;
                        const char* to = run.text.data() + end;
                        float width = ImGui::CalcTextSize(from, to).x;
                        if (!first) {
                            if (x + width > limit && x > 0) x = 0;
                            else ImGui::SameLine(0, 0);
                        }
                        ImGui::PushStyleColor(ImGuiCol_Text, run.color);
                        ImGui::TextUnformatted(from, to);
                        ImGui::PopStyleColor();
                        if (run.bold) {
                            ImVec2 at = ImGui::GetItemRectMin();
                            list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + 1, at.y), run.color, from, to);
                        }
                        x += width;
                        first = false;
                        start = end;
                    }
                    ImGui::PopFont();
                }
            }
            ImGui::EndGroup();
            bool hovered = ImGui::IsItemHovered();
            lua_pushboolean(L, hovered && ImGui::IsMouseClicked(0));
            lua_pushboolean(L, hovered && ImGui::IsMouseClicked(1));
            return 2;
        }

        int UiCopy(lua_State* L) {
            NeedDraw(L);
            size_t length = 0;
            const char* text = luaL_checklstring(L, 1, &length);
            luaL_argcheck(L, length <= ClipboardLimit, 1, "too long");
            ImGuiIO& io = ImGui::GetIO();
            bool gesture = io.MouseClicked[0] || io.MouseClicked[1] || io.MouseReleased[0] || io.MouseReleased[1] || io.KeyCtrl;
            if (!gesture) {
                lua_pushboolean(L, false);
                return 1;
            }
            ImGui::SetClipboardText(std::string(text, length).c_str());
            lua_pushboolean(L, true);
            return 1;
        }

        int UiTextBox(lua_State* L) {
            NeedWindow(L);
            const char* label = Label(L, 1);
            size_t length = 0;
            const char* text = luaL_checklstring(L, 2, &length);
            float width = Number(L, 3, "width", 0.0f), height = Number(L, 3, "height", 0.0f);
            std::string buffer(text, length);
            ImVec2 size(width < 0 ? -FLT_MIN : width, height < 0 ? -FLT_MIN : height);
            ImGui::InputTextMultiline(label, buffer.data(), buffer.size() + 1, size, ImGuiInputTextFlags_ReadOnly);
            return 0;
        }

        int UiChild(lua_State* L) {
            NeedWindow(L);
            const char* id = Label(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            float width = Number(L, 3, "width", 0.0f), height = Number(L, 3, "height", 0.0f);
            ImVec2 size(width < 0 ? -FLT_MIN : width, height < 0 ? -FLT_MIN : height);
            int status = LUA_OK;
            if (ImGui::BeginChild(id, size, Flag(L, 3, "border", true) ? ImGuiChildFlags_Borders : ImGuiChildFlags_None)) status = Guarded(L, 2);
            ImGui::EndChild();
            if (status != LUA_OK) return lua_error(L);
            return 0;
        }

        int UiFollow(lua_State* L) {
            NeedWindow(L);
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
            return 0;
        }

        int EditorDeselect(lua_State* L) {
            lua_pushboolean(L, editor::DeselectAll());
            return 1;
        }

        int EditorOwns(lua_State* L) {
            lua_pushboolean(L, editor::Owns((int)luaL_checkinteger(L, 1)));
            return 1;
        }

        int EditorToggle(lua_State* L, const char* setting, bool (*get)(), void (*set)(bool)) {
            if (lua_gettop(L) > 0) {
                set(lua_toboolean(L, 1) != 0);
                editorOwners[setting] = runtime::Name(L);
            }
            lua_pushboolean(L, get());
            return 1;
        }

        int BuildConstraint(lua_State* L) { return Toggle(L, "constraint", build::Constraint, build::SetConstraint); }
        int BuildMultiSelect(lua_State* L) { return EditorToggle(L, "multiselect", editor::MultiSelect, editor::SetMultiSelect); }
        int BuildCtrlSelect(lua_State* L) { return EditorToggle(L, "ctrlselect", editor::CtrlSelect, editor::SetCtrlSelect); }
        int BuildFastLinks(lua_State* L) { return EditorToggle(L, "fastlinks", editor::FastLinks, editor::SetFastLinks); }
        int BuildObjectLinks(lua_State* L) { return EditorToggle(L, "objectlinks", editor::ObjectLinks, editor::SetObjectLinks); }
        int BuildLinkFix(lua_State* L) { return EditorToggle(L, "linkfix", editor::LinkFix, editor::SetLinkFix); }

        int BuildFlags(lua_State* L) {
            if (lua_gettop(L) > 0) {
                if (Off(L, 1)) editor::SetFlags(false, 0, false);
                else editor::SetFlags(true, (uint64_t)luaL_checkinteger(L, 1), lua_toboolean(L, 2) != 0);
                editorOwners["flags"] = runtime::Name(L);
            }
            uint64_t mask = 0;
            bool override = false;
            if (!editor::Flags(mask, override)) return 0;
            lua_pushinteger(L, (lua_Integer)mask);
            lua_pushboolean(L, override);
            return 2;
        }

        int EditorPick(lua_State* L) {
            int material = 0;
            bool unlocked = false;
            if (!editor::Pick(material, unlocked)) return 0;
            lua_pushinteger(L, material);
            lua_pushboolean(L, unlocked);
            return 2;
        }

        int EditorGroup(lua_State* L) {
            lua_pushboolean(L, editor::EnterGroup((int)luaL_checkinteger(L, 1)));
            return 1;
        }

        int EditorInGroup(lua_State* L) {
            lua_pushboolean(L, editor::InGroup());
            return 1;
        }

        int EditorFocus(lua_State* L) {
            lua_pushboolean(L, editor::Focus((int)luaL_checkinteger(L, 1)));
            return 1;
        }

        int CameraNear(lua_State* L) {
            if (!lua_isnoneornil(L, 1)) {
                double value = luaL_checknumber(L, 1);
                luaL_argcheck(L, value >= 0.001 && value <= 10000, 1, "out of range");
                tools::SetNear((float)value);
            }
            float value = 0;
            if (!tools::Near(value)) return 0;
            lua_pushnumber(L, value);
            return 1;
        }

        int CameraSensitivity(lua_State* L) {
            if (Off(L, 1)) {
                DropTool("sensitivity");
                return 0;
            }
            double value = luaL_checknumber(L, 1);
            luaL_argcheck(L, value > 0 && value <= 100, 1, "out of range");
            tools::HoldSensitivity((float)value);
            OwnTool(L, "sensitivity");
            return 0;
        }

        int ScreenCapture(lua_State* L) {
            double size = luaL_optnumber(L, 1, 1);
            luaL_argcheck(L, size > 0 && size <= 8, 1, "a super size is a number from 0 to 8");
            std::string path;
            if (!tools::Capture(size, path)) return 0;
            lua_pushstring(L, path.c_str());
            return 1;
        }

        int GraphicsThemes(lua_State* L) {
            if (lua_gettop(L) > 0) tools::SetThemes(lua_toboolean(L, 1) != 0);
            bool on = false;
            if (!tools::ThemesEnabled(on)) return 0;
            lua_pushboolean(L, on);
            return 1;
        }

        int GraphicsPreview(lua_State* L) {
            std::string name = Off(L, 1) ? std::string() : std::string(luaL_checkstring(L, 1));
            luaL_argcheck(L, name.size() <= 64, 1, "too long");
            bool done = tools::Preview(name);
            if (!name.empty()) OwnTool(L, "preview");
            else toolOwners.erase("preview");
            lua_pushboolean(L, done);
            return 1;
        }

        int GraphicsTheme(lua_State* L) {
            std::string theme = tools::CurrentTheme();
            if (theme.empty()) return 0;
            lua_pushstring(L, theme.c_str());
            return 1;
        }

        void SetGameFlag(lua_State* L, const char* flag, bool on) {
            if (!host) return;
            host->flag_set(flag, on);
            if (on) flagOwners[flag] = runtime::Name(L);
            else flagOwners.erase(flag);
        }

        int FlagToggle(lua_State* L, const char* flag, bool inverted) {
            if (lua_gettop(L) > 0) SetGameFlag(L, flag, (lua_toboolean(L, 1) != 0) != inverted);
            lua_pushboolean(L, host && ((host->flag_get(flag) != 0) != inverted));
            return 1;
        }

        int GraphicsNotifications(lua_State* L) { return FlagToggle(L, "hide_notifications", true); }
        int GameElite(lua_State* L) { return FlagToggle(L, "free_elite", false); }
        int GameThirdPerson(lua_State* L) { return FlagToggle(L, "third_person", false); }
        int GameFastRespawn(lua_State* L) {
            if (lua_gettop(L) > 0) {
                bool on = lua_toboolean(L, 1) != 0;
                tools::SetFastRespawn(on, lua_toboolean(L, 2) != 0);
                if (on) OwnTool(L, "fastrespawn");
                else toolOwners.erase("fastrespawn");
            }
            return FlagToggle(L, "fast_respawn", false);
        }
        int GameRailgun(lua_State* L) { return FlagToggle(L, "railgun_override", false); }

        int GameAntiBan(lua_State* L) {
            if (lua_gettop(L) > 0) {
                bool on = lua_toboolean(L, 1) != 0;
                SetGameFlag(L, "antiban", on);
                SetGameFlag(L, "anticheat_off", on);
            }
            lua_pushboolean(L, host && host->flag_get("antiban") != 0);
            return 1;
        }

        int GameInfo(lua_State* L) {
            tools::Counts counts;
            if (!tools::Info(counts)) return 0;
            lua_createtable(L, 0, 6);
            lua_pushinteger(L, counts.objects);
            lua_setfield(L, -2, "objects");
            lua_pushinteger(L, counts.logic);
            lua_setfield(L, -2, "logic");
            lua_pushinteger(L, counts.links);
            lua_setfield(L, -2, "links");
            lua_pushinteger(L, counts.objectLinks);
            lua_setfield(L, -2, "objectLinks");
            lua_pushinteger(L, counts.unique);
            lua_setfield(L, -2, "unique");
            lua_pushinteger(L, counts.models);
            lua_setfield(L, -2, "models");
            return 1;
        }

        int GameNotify(lua_State* L) {
            size_t length = 0;
            const char* text = luaL_checklstring(L, 1, &length);
            luaL_argcheck(L, length <= MessageLimit, 1, "too long");
            lua_pushboolean(L, tools::Notify(std::string(text, length)));
            return 1;
        }

        int GamePlaying(lua_State* L) {
            lua_pushboolean(L, tools::Playing());
            return 1;
        }

        int HudTexture(lua_State* L) {
            std::string element = Lower(luaL_checkstring(L, 1));
            luaL_argcheck(L, element == "crosshair", 1, "only the crosshair takes a texture");
            lua_pushboolean(L, tools::CrosshairTexture(luaL_checkstring(L, 2)));
            return 1;
        }

        int KeyArgument(lua_State* L, int index) {
            if (lua_isinteger(L, index)) return (int)lua_tointeger(L, index);
            std::string wanted = luaL_checkstring(L, index);
            for (const auto& code : tools::KeyCodes())
                if (code.second == wanted) return code.first;
            return luaL_argerror(L, index, "unknown key");
        }

        int InputBinding(lua_State* L) {
            int key = tools::Binding((int)luaL_checkinteger(L, 1));
            if (key < 0) return 0;
            std::string name = tools::KeyName(key);
            if (name.empty()) lua_pushinteger(L, key);
            else lua_pushstring(L, name.c_str());
            return 1;
        }

        int InputRebind(lua_State* L) {
            int control = (int)luaL_checkinteger(L, 1);
            lua_pushboolean(L, tools::Rebind(control, KeyArgument(L, 2)));
            return 1;
        }

        int InputResetBinding(lua_State* L) {
            lua_pushboolean(L, tools::ResetBinding((int)luaL_checkinteger(L, 1)));
            return 1;
        }

        int InputKeyCodes(lua_State* L) {
            const auto& codes = tools::KeyCodes();
            lua_createtable(L, (int)codes.size(), 0);
            for (size_t i = 0; i < codes.size(); i++) {
                lua_pushstring(L, codes[i].second.c_str());
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            return 1;
        }

        int FilesList(lua_State* L) {
            std::string kind = luaL_checkstring(L, 1);
            luaL_argcheck(L, files::Known(kind), 1, "unknown kind of file");
            std::vector<std::string> names = files::List(kind);
            lua_createtable(L, (int)names.size(), 0);
            for (size_t i = 0; i < names.size(); i++) {
                lua_pushstring(L, names[i].c_str());
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            return 1;
        }

        int FilesFolder(lua_State* L) {
            std::string kind = luaL_checkstring(L, 1);
            luaL_argcheck(L, files::Known(kind), 1, "unknown kind of file");
            lua_pushstring(L, files::Folder(kind).c_str());
            return 1;
        }

        int FilesRead(lua_State* L) {
            std::string kind = luaL_checkstring(L, 1), name = luaL_checkstring(L, 2);
            luaL_argcheck(L, files::Readable(kind), 1, "this kind of file cannot be read");
            luaL_argcheck(L, files::ValidName(kind, name), 2, "bad file name");
            std::string data;
            if (!files::Read(kind, name, data)) return 0;
            lua_pushlstring(L, data.data(), data.size());
            return 1;
        }

        int FilesWrite(lua_State* L) {
            std::string kind = luaL_checkstring(L, 1), name = luaL_checkstring(L, 2);
            size_t length = 0;
            const char* data = luaL_checklstring(L, 3, &length);
            luaL_argcheck(L, files::Writable(kind), 1, "this kind of file cannot be written");
            luaL_argcheck(L, files::ValidName(kind, name), 2, "bad file name");
            std::string path;
            if (!files::Write(kind, name, std::string(data, length), path)) return 0;
            lua_pushstring(L, path.c_str());
            return 1;
        }

        int Base64Encode(lua_State* L) {
            size_t length = 0;
            const char* data = luaL_checklstring(L, 1, &length);
            std::string text = files::Encode(std::string(data, length));
            lua_pushlstring(L, text.data(), text.size());
            return 1;
        }

        int Base64Decode(lua_State* L) {
            size_t length = 0;
            const char* text = luaL_checklstring(L, 1, &length);
            std::string data;
            if (!files::Decode(std::string(text, length), data)) return 0;
            lua_pushlstring(L, data.data(), data.size());
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
            {"measure", HudMeasure}, {"image", HudImage}, {"poly", HudPoly}, {"polys", HudPolys}, {nullptr, nullptr},
        };
        static const luaL_Reg ui[] = {
            {"window", UiWindow}, {"style", UiStyle}, {"with", UiWith}, {"text", UiText}, {"button", UiButton}, {"checkbox", UiCheckbox},
            {"slider", UiSlider}, {"input", UiInput}, {"progress", UiProgress}, {"image", UiImage},
            {"separator", UiSeparator}, {"sameline", UiSameLine}, {"spacing", UiSpacing},
            {"tabs", UiTabs}, {"tab", UiTab}, {"combo", UiCombo}, {"color", UiColor}, {"number", UiNumber}, {"integer", UiInteger},
            {"tree", UiTree}, {"heading", UiHeading}, {"table", UiTable}, {"small", UiSmall}, {"width", UiWidth},
            {"reserved", UiReserved}, {"labelwidth", UiLabelWidth}, {"shared", UiShared}, {"avail", UiAvail}, {"drag", UiDrag}, {"numbers", UiNumbers},
            {"columns", UiColumns}, {"cell", UiCell}, {"row", UiRow}, {"rich", UiRich}, {"copy", UiCopy}, {"textbox", UiTextBox},
            {"child", UiChild}, {"follow", UiFollow}, {nullptr, nullptr},
        };
        static const luaL_Reg screen[] = {{"size", ScreenSize}, {"fullscreen", ScreenFullscreen}, {"resolution", ScreenResolution}, {"capture", ScreenCapture}, {nullptr, nullptr}};
        static const luaL_Reg sound[] = {{"muted", SoundMuted}, {nullptr, nullptr}};
        static const luaL_Reg input[] = {{"down", InputDown}, {"mouse", InputMouse}, {"locked", InputLocked}, {"block", InputBlock}, {"cursor", InputCursor}, {"wheel", InputWheel}, {"consume", InputConsume}, {"binding", InputBinding}, {"rebind", InputRebind}, {"resetbinding", InputResetBinding}, {"keycodes", InputKeyCodes}, {nullptr, nullptr}};
        static const luaL_Reg player[] = {{"position", PlayerPosition}, {"name", PlayerName}, {"health", PlayerHealth}, {"god", PlayerGod}, {"teleport", PlayerTeleport}, {"respawn", PlayerRespawn}, {nullptr, nullptr}};
        static const luaL_Reg build[] = {
            {"limit", BuildLimit}, {"materials", BuildMaterials}, {"muzzle", BuildMuzzle}, {"face", BuildFace}, {"blue", BuildBlue},
            {"grid", BuildGrid}, {"scale", BuildScale}, {"step", BuildStep}, {"speed", BuildSpeed}, {"sliders", BuildSliders},
            {"constraint", BuildConstraint}, {"multiselect", BuildMultiSelect}, {"ctrlselect", BuildCtrlSelect}, {"fastlinks", BuildFastLinks},
            {"objectlinks", BuildObjectLinks}, {"linkfix", BuildLinkFix}, {"flags", BuildFlags}, {nullptr, nullptr},
        };
        static const luaL_Reg camera[] = {
            {"position", CameraPosition}, {"fov", CameraFov}, {"distance", CameraDistance}, {"far", CameraFar}, {"near", CameraNear},
            {"orthographic", CameraOrthographic}, {"shadows", CameraShadows}, {"sensitivity", CameraSensitivity}, {nullptr, nullptr},
        };
        static const luaL_Reg editor[] = {
            {"target", EditorTarget}, {"selected", EditorSelected}, {"material", EditorMaterial}, {"pick", EditorPick},
            {"group", EditorGroup}, {"ingroup", EditorInGroup}, {"focus", EditorFocus}, {"owns", EditorOwns}, {"deselect", EditorDeselect}, {nullptr, nullptr},
        };
        static const luaL_Reg graphics[] = {
            {"fog", GraphicsFog}, {"water", GraphicsWater}, {"themes", GraphicsThemes}, {"preview", GraphicsPreview}, {"theme", GraphicsTheme},
            {"notifications", GraphicsNotifications}, {nullptr, nullptr},
        };
        static const luaL_Reg game[] = {
            {"mode", GameMode}, {"ping", GamePing}, {"antiafk", GameAntiAfk}, {"info", GameInfo}, {"notify", GameNotify}, {"playing", GamePlaying},
            {"elite", GameElite}, {"antiban", GameAntiBan}, {"thirdperson", GameThirdPerson}, {"fastrespawn", GameFastRespawn}, {"railgun", GameRailgun},
            {nullptr, nullptr},
        };
        static const luaL_Reg filesLibrary[] = {{"list", FilesList}, {"folder", FilesFolder}, {"read", FilesRead}, {"write", FilesWrite}, {nullptr, nullptr}};
        static const luaL_Reg base64[] = {{"encode", Base64Encode}, {"decode", Base64Decode}, {nullptr, nullptr}};
        static const luaL_Reg server[] = {{"send", ServerSend}, {"file", ServerFile}, {nullptr, nullptr}};
        static const luaL_Reg hud[] = {{"hide", HudHide}, {"show", HudShow}, {"shown", HudShown}, {"active", HudActive}, {"list", HudList}, {"text", InterfaceText}, {"click", InterfaceClick}, {"color", HudColor}, {"texture", HudTexture}, {nullptr, nullptr}};
        Library(L, "draw", draw);
        Library(L, "ui", ui);
        Library(L, "screen", screen);
        Library(L, "input", input);
        Library(L, "player", player);
        Library(L, "camera", camera);
        Library(L, "server", server);
        Library(L, "hud", hud);
        Library(L, "sound", sound);
        Library(L, "build", build);
        Library(L, "editor", editor);
        Library(L, "graphics", graphics);
        Library(L, "game", game);
        Library(L, "files", filesLibrary);
        Library(L, "base64", base64);
        lua_pushcfunction(L, Rgb);
        lua_setglobal(L, "rgb");
    }

    void SetSender(Sender value) { sender = value; }

    void SetHost(const OkMod* value) { host = value; }

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

    void Forget(const std::string& script) {
        windowsOf.erase(script);
        blockers.erase(script);
        ReleaseCursor(script);
        consumedBy.erase(script);
        gods.erase(script);
        for (auto entry = buildOwners.begin(); entry != buildOwners.end();) {
            if (entry->second != script) {
                ++entry;
                continue;
            }
            build::Reset(entry->first);
            entry = buildOwners.erase(entry);
        }
        for (auto entry = toolOwners.begin(); entry != toolOwners.end();) {
            if (entry->second != script) {
                ++entry;
                continue;
            }
            if (entry->first == "antiafk") antiAfk = false;
            tools::Clear(entry->first);
            entry = toolOwners.erase(entry);
        }
        for (auto entry = editorOwners.begin(); entry != editorOwners.end();) {
            if (entry->second != script) {
                ++entry;
                continue;
            }
            editor::Reset(entry->first);
            entry = editorOwners.erase(entry);
        }
        for (auto entry = flagOwners.begin(); entry != flagOwners.end();) {
            if (entry->second != script) {
                ++entry;
                continue;
            }
            if (host) host->flag_set(entry->first.c_str(), 0);
            entry = flagOwners.erase(entry);
        }
        std::vector<std::string> mine;
        for (const auto& entry : hidden)
            if (entry.second.holders.count(script)) mine.push_back(entry.first);
        for (const std::string& element : mine) Release(element, script);
    }

    bool Blocking() { return !blockers.empty(); }

    bool God() { return !gods.empty(); }

    void Unblock() {
        blockers.clear();
        if (!cursorHolders.empty()) game::ReturnCursor(cursorWasLocked);
        cursorHolders.clear();
    }

    void Wheel(double notches) { wheelPending += notches; }

    double TakeWheel() {
        wheelFrame = wheelPending;
        wheelPending = 0;
        return wheelFrame;
    }

    bool Consumed(const std::string& name) {
        std::string key = Lower(name);
        for (const auto& entry : consumedBy)
            if (entry.second.count(key)) return true;
        return false;
    }

    void HoldCursor() {
        if (!cursorHolders.empty()) game::FreeCursor();
    }

    void Maintain(double dt) {
        if (!gods.empty()) game::RestoreHealth();
        keepClock += dt;
        if (keepClock < KeepSeconds) return;
        keepClock = 0;
        for (auto& entry : hidden) game::Hide(entry.first, entry.second.saved);
    }

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
