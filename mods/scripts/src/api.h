#pragma once
#include <string>
#include <vector>

struct lua_State;

namespace api {
    struct KeyEvent {
        std::string name;
        bool down;
    };

    using Sender = bool (*)(const std::string& name, const std::string& data);

    void SetSender(Sender sender);
    void Register(lua_State* L);
    void Style();
    void LoadFonts();
    void BeginDraw();
    void EndDraw();
    void Forget(const std::string& script);
    void Key(const std::string& name, bool down);
    void ReleaseKeys();
    std::vector<KeyEvent> TakeKeys();
}
