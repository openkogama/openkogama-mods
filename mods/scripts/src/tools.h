#pragma once
#include <string>
#include <utility>
#include <vector>

struct OkMod;

namespace tools {
    struct Counts {
        int objects = 0;
        int logic = 0;
        int links = 0;
        int objectLinks = 0;
        int unique = 0;
        int models = 0;
    };

    void Attach(const OkMod* host);
    void Apply();
    void Clear(const std::string& setting);

    std::string Mode();
    bool Playing();
    bool Target(int& id);
    bool Selected(std::vector<int>& ids);
    bool Material(int& id);
    bool SetMaterial(int id);

    bool Fov(float& value);
    void HoldFov(float value);
    bool Distance(float& value);
    void HoldDistance(float value);
    bool Far(float& value);
    bool SetFar(float value);
    bool Near(float& value);
    bool SetNear(float value);
    bool SetOrthographic(bool on, float size);
    bool SetShadows(float distance);
    bool Shadows(float& distance);
    void HoldSensitivity(float multiplier);
    bool Fog(bool& on);
    void HoldFog(bool on, bool withDensity, float density);
    bool Water(bool& reflective);
    bool SetWater(bool reflective);
    bool SetResolution(int width, int height, int mode);
    bool Capture(double superSize, std::string& path);

    bool ThemesEnabled(bool& on);
    bool SetThemes(bool on);
    bool Preview(const std::string& name);
    std::string CurrentTheme();

    bool Respawn(bool safe);
    void SetFastRespawn(bool on, bool safe);
    void HoldAntiAfk(bool on);
    bool Ping(int& milliseconds);
    bool Info(Counts& counts);
    bool Notify(const std::string& text);
    void HoldCrosshair(bool on, const float* rgba);
    bool CrosshairTexture(const std::string& name);

    int Binding(int control);
    bool Rebind(int control, int key);
    bool ResetBinding(int control);
    std::string KeyName(int key);
    const std::vector<std::pair<int, std::string>>& KeyCodes();
}
