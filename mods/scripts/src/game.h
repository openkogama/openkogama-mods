#pragma once
#include <map>
#include <string>
#include <vector>

struct OkMod;

namespace game {
    void Attach(const OkMod* host);
    bool PlayerPosition(float* value);
    bool PlayerHealth(float& health, float& most);
    std::string PlayerName();
    bool CameraPosition(float* value);
    std::string GraphicsVersion();
    bool NativeTexture(bool (*use)(void* native));

    struct Part {
        int id;
        bool shown;
        bool active;
    };

    struct Saved {
        bool canvas = false;
        bool active = true;
        float scale[3] = {1, 1, 1};
    };

    std::vector<Part> Parts(const std::string& element);
    void Hide(const std::string& element, std::map<int, Saved>& saved);
    void Restore(const std::string& element, const std::map<int, Saved>& saved);
    bool Click(const std::string& element);
    bool Fullscreen();
    void SetFullscreen(bool on);
    bool Muted();
    void SetMuted(bool on);
    bool Texts(const std::string& element, std::vector<std::string>& texts);
    bool Children(const std::string& element, std::vector<std::string>& names);
    bool CursorLocked();
    void FreeCursor();
    void ReturnCursor(bool wasLocked);
    void SuppressInput();
}
