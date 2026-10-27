#pragma once
#include <string>
#include <vector>
#include <set>

namespace Explorer {
    struct ObjectEntry {
        std::string id;
        std::string type;
        std::string name;
        int   itemId  = -1;
        int   order   = 0;
        int   groupId = -1;
        bool  isGroup = false;
        float x = 0.f, y = 0.f, z = 0.f;
    };

    void LoadFromFile(const std::string& path);
    void SetSelected(const std::string& id);
    void SetRootGroupId(int id);
    void SetCameraPos(float x, float y, float z);
    void GetCameraPos(float& x, float& y, float& z);
    void Render();
    const std::set<std::string>& GetSelectedIds();
    const ObjectEntry* FindById(const std::string& id);
    const std::vector<ObjectEntry>& GetAllObjects();
    void DeselectAll();

    extern bool advGroupingWarnSkip;
    extern bool advancedGrouping;
    extern bool advGroupingWarnPending;
}
