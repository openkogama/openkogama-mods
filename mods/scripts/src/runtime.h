#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct lua_State;

namespace runtime {
    struct Arg {
        enum Kind { Nil, Number, Text, Flag } kind = Nil;
        double number = 0;
        std::string text;
        bool flag = false;

        static Arg Of(double value) { Arg arg; arg.kind = Number; arg.number = value; return arg; }
        static Arg Of(const std::string& value) { Arg arg; arg.kind = Text; arg.text = value; return arg; }
        static Arg Of(bool value) { Arg arg; arg.kind = Flag; arg.flag = value; return arg; }
    };

    struct Source {
        std::filesystem::path folder;
        std::string prefix;
    };

    using Logger = void (*)(const std::string& line);
    void SetLogger(Logger logger);
    void Log(const std::string& line);

    void Open(const std::vector<Source>& sources);
    void Close();
    void Watch();
    void Tick(double dt);
    void Draw();
    void Emit(const char* event, const std::vector<Arg>& args = {});
    void Keep(const char* event);
    int Count();

    const std::string& Name(lua_State* L);
    const std::filesystem::path& Root(lua_State* L);
    int Traceback(lua_State* L);
    void Check(lua_State* L);
}
