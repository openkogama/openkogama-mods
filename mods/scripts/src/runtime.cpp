#include "runtime.h"
#include "api.h"
#include "images.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace runtime {
    namespace {
        constexpr size_t MemoryLimit = 64u << 20;
        constexpr int Slice = 1000;
        constexpr int FailureLimit = 10;
        constexpr std::chrono::seconds FailureWindow(10);
        constexpr std::chrono::milliseconds CallBudget(50);
        constexpr std::chrono::milliseconds LoadBudget(2000);
        constexpr std::uintmax_t FileLimit = 1u << 20;
        constexpr lua_Unsigned MoveLimit = 10000000;
        constexpr size_t TimerLimit = 1000;
        constexpr double ShortestInterval = 0.01;
        const char* const HandlersKey = "scripts.handlers";
        const char* const ModulesKey = "scripts.modules";

        struct Timer {
            lua_Integer id;
            double at;
            double every;
            int function;
        };

        struct Script {
            std::string name;
            fs::path root;
            lua_State* L = nullptr;
            size_t memory = 0;
            Clock::time_point deadline;
            Clock::time_point failedAt;
            int failures = 0;
            bool stopped = false;
            fs::file_time_type stamp;
            double clock = 0;
            std::vector<Timer> timers;
            lua_Integer nextTimer = 1;
        };

        struct Event {
            Script* script;
            const char* name;
            const std::vector<Arg>* args;
        };

        Logger logger = nullptr;
        std::vector<Source> sources;
        std::vector<std::unique_ptr<Script>> scripts;

        Script* Of(lua_State* L) {
            void* script = nullptr;
            lua_getallocf(L, &script);
            return static_cast<Script*>(script);
        }

        void* Allocate(void* user, void* block, size_t old, size_t size) {
            Script* script = static_cast<Script*>(user);
            if (!block) old = 0;
            if (size == 0) {
                std::free(block);
                script->memory -= old;
                return nullptr;
            }
            if (size > old && script->memory - old + size > MemoryLimit) return nullptr;
            void* moved = std::realloc(block, size);
            if (moved) script->memory = script->memory - old + size;
            return moved;
        }

        void Watchdog(lua_State* L, lua_Debug*) {
            if (Clock::now() < Of(L)->deadline) {
                if (lua_gethookcount(L) != Slice) lua_sethook(L, Watchdog, LUA_MASKCOUNT, Slice);
                return;
            }
            lua_sethook(L, Watchdog, LUA_MASKCOUNT, 1);
            luaL_error(L, "took too long");
        }

        int Panic(lua_State* L) {
            const char* message = lua_tostring(L, -1);
            Log("[" + Of(L)->name + "] " + (message ? message : "unprotected error"));
            return 0;
        }

        void Fail(Script& script, const char* message) {
            Log("[" + script.name + "] " + (message ? message : "error"));
            Clock::time_point now = Clock::now();
            if (now - script.failedAt > FailureWindow) {
                script.failedAt = now;
                script.failures = 0;
            }
            if (++script.failures < FailureLimit || script.stopped) return;
            script.stopped = true;
            Log("[" + script.name + "] stopped after " + std::to_string(FailureLimit) + " errors");
        }

        bool Call(Script& script, int args, std::chrono::milliseconds budget) {
            lua_State* L = script.L;
            int handler = lua_gettop(L) - args;
            lua_pushcfunction(L, Traceback);
            lua_insert(L, handler);
            script.deadline = Clock::now() + budget;
            int status = lua_pcall(L, args, 0, handler);
            lua_sethook(L, Watchdog, LUA_MASKCOUNT, Slice);
            if (status != LUA_OK) {
                Fail(script, lua_tostring(L, -1));
                lua_pop(L, 1);
            }
            lua_remove(L, handler);
            return status == LUA_OK;
        }

        bool Protected(Script& script, lua_CFunction function, void* data) {
            lua_State* L = script.L;
            lua_pushcfunction(L, function);
            lua_pushlightuserdata(L, data);
            script.deadline = Clock::now() + CallBudget;
            int status = lua_pcall(L, 1, 0, 0);
            lua_sethook(L, Watchdog, LUA_MASKCOUNT, Slice);
            if (status == LUA_OK) return true;
            Fail(script, lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }

        void Push(lua_State* L, const Arg& arg) {
            switch (arg.kind) {
                case Arg::Number: lua_pushnumber(L, arg.number); break;
                case Arg::Text: lua_pushlstring(L, arg.text.data(), arg.text.size()); break;
                case Arg::Flag: lua_pushboolean(L, arg.flag); break;
                default: lua_pushnil(L); break;
            }
        }

        int RunHandlers(lua_State* L) {
            Event* event = static_cast<Event*>(lua_touserdata(L, 1));
            lua_getfield(L, LUA_REGISTRYINDEX, HandlersKey);
            if (lua_getfield(L, -1, event->name) != LUA_TTABLE) return 0;
            lua_Integer count = (lua_Integer)lua_rawlen(L, -1);
            lua_createtable(L, (int)count, 0);
            for (lua_Integer i = 1; i <= count; i++) {
                lua_rawgeti(L, -2, i);
                lua_rawseti(L, -2, i);
            }
            int list = lua_gettop(L);
            for (lua_Integer i = 1; i <= count && !event->script->stopped; i++) {
                lua_rawgeti(L, list, i);
                for (const Arg& arg : *event->args) Push(L, arg);
                Call(*event->script, (int)event->args->size(), CallBudget);
            }
            return 0;
        }

        void Dispatch(Script& script, const char* name, const std::vector<Arg>& args) {
            if (!script.L || script.stopped) return;
            Event event{&script, name, &args};
            Protected(script, RunHandlers, &event);
        }

        int RunTimers(lua_State* L) {
            Script* script = Of(L);
            std::vector<lua_Integer> due;
            for (const Timer& timer : script->timers)
                if (timer.at <= script->clock) due.push_back(timer.id);
            for (lua_Integer id : due) {
                if (script->stopped) break;
                auto found = std::find_if(script->timers.begin(), script->timers.end(), [id](const Timer& timer) { return timer.id == id; });
                if (found == script->timers.end()) continue;
                Timer timer = *found;
                if (timer.every > 0) {
                    found->at += timer.every;
                    if (found->at <= script->clock) found->at = script->clock + timer.every;
                }
                else script->timers.erase(found);
                lua_rawgeti(L, LUA_REGISTRYINDEX, timer.function);
                if (timer.every <= 0) luaL_unref(L, LUA_REGISTRYINDEX, timer.function);
                Call(*script, 0, CallBudget);
            }
            return 0;
        }

        bool ReadFile(const fs::path& path, std::string& text) {
            std::error_code error;
            std::uintmax_t size = fs::file_size(path, error);
            if (error || size > FileLimit) return false;
            std::ifstream file(path, std::ios::binary);
            if (!file) return false;
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return true;
        }

        bool ModuleName(const char* name) {
            char previous = '.';
            for (const char* c = name; *c; c++) {
                bool allowed = std::isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == '.';
                if (!allowed || (*c == '.' && previous == '.')) return false;
                previous = *c;
            }
            return previous != '.';
        }

        int Require(lua_State* L) {
            const char* name = luaL_checkstring(L, 1);
            if (!ModuleName(name)) return luaL_error(L, "module '%s' has to be a file in the script folder", name);
            lua_getfield(L, LUA_REGISTRYINDEX, ModulesKey);
            int modules = lua_gettop(L);
            int type = lua_getfield(L, modules, name);
            if (type == LUA_TBOOLEAN && !lua_toboolean(L, -1)) return luaL_error(L, "module '%s' requires itself", name);
            if (type != LUA_TNIL) return 1;
            lua_pop(L, 1);
            std::string relative = name;
            std::replace(relative.begin(), relative.end(), '.', '/');
            relative += ".lua";
            std::string text;
            if (!ReadFile(Root(L) / fs::u8path(relative), text)) return luaL_error(L, "module '%s' not found", name);
            lua_pushboolean(L, 0);
            lua_setfield(L, modules, name);
            lua_pushcfunction(L, Traceback);
            int handler = lua_gettop(L);
            int status = luaL_loadbufferx(L, text.data(), text.size(), ("@" + relative).c_str(), "t");
            if (status == LUA_OK) {
                lua_pushstring(L, name);
                status = lua_pcall(L, 1, 1, handler);
            }
            if (status != LUA_OK) {
                lua_pushnil(L);
                lua_setfield(L, modules, name);
                return lua_error(L);
            }
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_pushboolean(L, 1);
            }
            lua_pushvalue(L, -1);
            lua_setfield(L, modules, name);
            return 1;
        }

        int Load(lua_State* L) {
            size_t size = 0;
            const char* text = luaL_checklstring(L, 1, &size);
            const char* name = luaL_optstring(L, 2, "=(load)");
            if (luaL_loadbufferx(L, text, size, name, "t") == LUA_OK) return 1;
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }

        int Print(lua_State* L) {
            int count = lua_gettop(L);
            luaL_Buffer buffer;
            luaL_buffinit(L, &buffer);
            for (int i = 1; i <= count; i++) {
                if (i > 1) luaL_addchar(&buffer, '\t');
                luaL_tolstring(L, i, nullptr);
                luaL_addvalue(&buffer);
            }
            luaL_pushresult(&buffer);
            Log("[" + Name(L) + "] " + lua_tostring(L, -1));
            return 0;
        }

        int Garbage(lua_State* L) {
            const char* option = luaL_optstring(L, 1, "count");
            if (std::strcmp(option, "count") != 0) return luaL_error(L, "collectgarbage only answers \"count\" here");
            lua_pushnumber(L, lua_gc(L, LUA_GCCOUNT) + lua_gc(L, LUA_GCCOUNTB) / 1024.0);
            return 1;
        }

        int SetMetatable(lua_State* L) {
            if (lua_type(L, 2) == LUA_TTABLE) {
                lua_pushliteral(L, "__gc");
                if (lua_rawget(L, 2) != LUA_TNIL) return luaL_error(L, "__gc is not available to scripts");
                lua_pop(L, 1);
            }
            lua_pushvalue(L, lua_upvalueindex(1));
            lua_insert(L, 1);
            lua_call(L, lua_gettop(L) - 1, 1);
            return 1;
        }

        int Move(lua_State* L) {
            lua_Integer first = luaL_checkinteger(L, 2), last = luaL_checkinteger(L, 3);
            if (last >= first && (lua_Unsigned)last - (lua_Unsigned)first >= MoveLimit)
                return luaL_error(L, "table.move moves at most %d elements at once", (int)MoveLimit);
            lua_pushvalue(L, lua_upvalueindex(1));
            lua_insert(L, 1);
            lua_call(L, lua_gettop(L) - 1, 1);
            return 1;
        }

        int On(lua_State* L) {
            const char* name = luaL_checkstring(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            lua_getfield(L, LUA_REGISTRYINDEX, HandlersKey);
            if (lua_getfield(L, -1, name) != LUA_TTABLE) {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
                lua_setfield(L, -3, name);
            }
            lua_pushvalue(L, 2);
            lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
            return 0;
        }

        int Off(lua_State* L) {
            const char* name = luaL_checkstring(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            lua_getfield(L, LUA_REGISTRYINDEX, HandlersKey);
            if (lua_getfield(L, -1, name) != LUA_TTABLE) return 0;
            int list = lua_gettop(L);
            lua_Integer count = (lua_Integer)lua_rawlen(L, list);
            for (lua_Integer i = 1; i <= count; i++) {
                lua_rawgeti(L, list, i);
                bool same = lua_rawequal(L, -1, 2);
                lua_pop(L, 1);
                if (!same) continue;
                for (lua_Integer j = i; j < count; j++) {
                    lua_rawgeti(L, list, j + 1);
                    lua_rawseti(L, list, j);
                }
                lua_pushnil(L);
                lua_rawseti(L, list, count);
                break;
            }
            return 0;
        }

        int Schedule(lua_State* L, bool repeat) {
            double seconds = luaL_checknumber(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            Script* script = Of(L);
            if (script->timers.size() >= TimerLimit) return luaL_error(L, "a script can keep at most %d timers", (int)TimerLimit);
            if (repeat) seconds = std::max(seconds, ShortestInterval);
            else seconds = std::max(seconds, 0.0);
            lua_pushvalue(L, 2);
            int function = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_Integer id = script->nextTimer++;
            script->timers.push_back({id, script->clock + seconds, repeat ? seconds : 0.0, function});
            lua_pushinteger(L, id);
            return 1;
        }

        int After(lua_State* L) { return Schedule(L, false); }
        int Every(lua_State* L) { return Schedule(L, true); }

        int Cancel(lua_State* L) {
            lua_Integer id = luaL_checkinteger(L, 1);
            Script* script = Of(L);
            auto found = std::find_if(script->timers.begin(), script->timers.end(), [id](const Timer& timer) { return timer.id == id; });
            if (found == script->timers.end()) return 0;
            luaL_unref(L, LUA_REGISTRYINDEX, found->function);
            script->timers.erase(found);
            return 0;
        }

        void Global(lua_State* L, const char* name, lua_CFunction function) {
            lua_pushcfunction(L, function);
            lua_setglobal(L, name);
        }

        void Wrap(lua_State* L, const char* name, lua_CFunction function) {
            lua_getfield(L, -1, name);
            lua_pushcclosure(L, function, 1);
            lua_setfield(L, -2, name);
        }

        int Prepare(lua_State* L) {
            const luaL_Reg libraries[] = {
                {LUA_GNAME, luaopen_base},
                {LUA_COLIBNAME, luaopen_coroutine},
                {LUA_MATHLIBNAME, luaopen_math},
                {LUA_STRLIBNAME, luaopen_string},
                {LUA_TABLIBNAME, luaopen_table},
                {LUA_UTF8LIBNAME, luaopen_utf8},
            };
            for (const luaL_Reg& library : libraries) {
                luaL_requiref(L, library.name, library.func, 1);
                lua_pop(L, 1);
            }
            luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 0);
            lua_createtable(L, 0, 4);
            for (const char* name : {"clock", "date", "difftime", "time"}) {
                lua_getfield(L, -2, name);
                lua_setfield(L, -2, name);
            }
            lua_setglobal(L, LUA_OSLIBNAME);
            lua_pop(L, 1);

            for (const char* name : {"dofile", "loadfile", "warn"}) {
                lua_pushnil(L);
                lua_setglobal(L, name);
            }
            lua_pushglobaltable(L);
            Wrap(L, "setmetatable", SetMetatable);
            lua_pop(L, 1);
            lua_getglobal(L, LUA_STRLIBNAME);
            lua_pushnil(L);
            lua_setfield(L, -2, "dump");
            lua_pop(L, 1);
            lua_getglobal(L, LUA_TABLIBNAME);
            Wrap(L, "move", Move);
            lua_pop(L, 1);

            Global(L, "print", Print);
            Global(L, "load", Load);
            Global(L, "collectgarbage", Garbage);
            Global(L, "require", Require);
            Global(L, "on", On);
            Global(L, "off", Off);
            Global(L, "after", After);
            Global(L, "every", Every);
            Global(L, "cancel", Cancel);

            lua_newtable(L);
            lua_setfield(L, LUA_REGISTRYINDEX, HandlersKey);
            lua_newtable(L);
            lua_setfield(L, LUA_REGISTRYINDEX, ModulesKey);
            api::Register(L);
            return 0;
        }

        int RunMain(lua_State* L) {
            lua_pushcfunction(L, Require);
            lua_pushliteral(L, "main");
            lua_call(L, 1, 0);
            return 0;
        }

        void Shutdown(Script& script) {
            if (!script.L) return;
            images::Release(script.name);
            api::Forget(script.name);
            lua_close(script.L);
            script.L = nullptr;
            script.timers.clear();
        }

        void Start(Script& script) {
            Shutdown(script);
            script.stopped = false;
            script.failures = 0;
            script.clock = 0;
            script.memory = 0;
            script.nextTimer = 1;
            script.L = lua_newstate(Allocate, &script, luaL_makeseed(nullptr));
            if (!script.L) {
                script.stopped = true;
                Log("[" + script.name + "] could not start");
                return;
            }
            lua_atpanic(script.L, Panic);
            lua_setwarnf(script.L, nullptr, nullptr);
            lua_sethook(script.L, Watchdog, LUA_MASKCOUNT, Slice);
            lua_pushcfunction(script.L, Prepare);
            script.deadline = Clock::now() + LoadBudget;
            if (lua_pcall(script.L, 0, 0, 0) != LUA_OK) {
                const char* message = lua_tostring(script.L, -1);
                Log("[" + script.name + "] could not start: " + (message ? message : "error"));
                script.stopped = true;
                Shutdown(script);
                return;
            }
            lua_pushcfunction(script.L, RunMain);
            if (!Call(script, 0, LoadBudget)) {
                script.stopped = true;
                Shutdown(script);
                return;
            }
            Log("[" + script.name + "] started");
        }

        fs::file_time_type Stamp(const fs::path& path) {
            std::error_code error;
            fs::file_time_type newest = fs::last_write_time(path, error);
            int seen = 0;
            for (fs::recursive_directory_iterator entry(path, error), end; !error && entry != end && seen < 256; entry.increment(error), seen++) {
                fs::file_time_type time = entry->last_write_time(error);
                if (!error && time > newest) newest = time;
            }
            return newest;
        }

        void Sweep() {
            for (auto& script : scripts)
                if (script->stopped && script->L) Shutdown(*script);
        }
    }

    void SetLogger(Logger value) { logger = value; }

    void Log(const std::string& line) {
        if (logger) logger(line);
    }

    void Open(const std::vector<Source>& list) {
        sources = list;
        Watch();
    }

    void Close() {
        for (auto& script : scripts) Shutdown(*script);
        scripts.clear();
    }

    void Watch() {
        std::vector<std::pair<fs::path, std::string>> found;
        for (const Source& source : sources) {
            std::error_code error;
            if (source.folder.empty()) continue;
            for (fs::directory_iterator entry(source.folder, error), end; !error && entry != end; entry.increment(error)) {
                std::string name = entry->path().filename().u8string();
                if (!name.empty() && name[0] != '.' && entry->is_directory(error) && fs::exists(entry->path() / "main.lua", error))
                    found.emplace_back(entry->path(), source.prefix + name);
            }
        }
        for (auto it = scripts.begin(); it != scripts.end();) {
            bool present = std::any_of(found.begin(), found.end(), [&it](const auto& entry) { return entry.first == (*it)->root; });
            if (present) {
                ++it;
                continue;
            }
            Log("[" + (*it)->name + "] removed");
            Shutdown(**it);
            it = scripts.erase(it);
        }
        for (const auto& entry : found) {
            const fs::path& path = entry.first;
            const std::string& name = entry.second;
            fs::file_time_type stamp = Stamp(path);
            auto known = std::find_if(scripts.begin(), scripts.end(), [&path](const std::unique_ptr<Script>& script) { return script->root == path; });
            if (known == scripts.end()) {
                auto script = std::make_unique<Script>();
                script->name = name;
                script->root = path;
                script->stamp = stamp;
                Start(*script);
                scripts.push_back(std::move(script));
            }
            else if ((*known)->stamp != stamp) {
                (*known)->stamp = stamp;
                Log("[" + (*known)->name + "] reloading");
                Start(**known);
            }
        }
    }

    void Tick(double dt) {
        for (const api::KeyEvent& key : api::TakeKeys()) Emit("key", {Arg::Of(key.name), Arg::Of(key.down)});
        std::vector<Arg> args{Arg::Of(dt)};
        for (auto& script : scripts) {
            if (!script->L || script->stopped) continue;
            script->clock += dt;
            if (!script->timers.empty()) Protected(*script, RunTimers, nullptr);
            Dispatch(*script, "update", args);
        }
        Sweep();
    }

    void Draw() {
        std::vector<Arg> none;
        for (auto& script : scripts) {
            if (!script->L || script->stopped) continue;
            ImGui::PushID(script->name.c_str());
            ImGuiErrorRecoveryState state;
            ImGui::ErrorRecoveryStoreState(&state);
            api::BeginDraw();
            Dispatch(*script, "draw", none);
            api::EndDraw();
            ImGui::ErrorRecoveryTryToRecoverState(&state);
            ImGui::PopID();
        }
        Sweep();
    }

    void Emit(const char* event, const std::vector<Arg>& args) {
        for (auto& script : scripts) Dispatch(*script, event, args);
        Sweep();
    }

    int Count() {
        int running = 0;
        for (auto& script : scripts)
            if (script->L && !script->stopped) running++;
        return running;
    }

    const std::string& Name(lua_State* L) { return Of(L)->name; }

    const fs::path& Root(lua_State* L) { return Of(L)->root; }

    int Traceback(lua_State* L) {
        const char* message = lua_tostring(L, 1);
        if (!message) message = luaL_tolstring(L, 1, nullptr);
        if (std::strstr(message, "stack traceback:")) {
            lua_pushstring(L, message);
            return 1;
        }
        luaL_traceback(L, L, message, 1);
        return 1;
    }

    void Check(lua_State* L) {
        if (Clock::now() >= Of(L)->deadline) luaL_error(L, "took too long");
    }
}

void scripts_matchstep(lua_State* L) {
    static unsigned steps = 0;
    if ((++steps & 1023) == 0) runtime::Check(L);
}
