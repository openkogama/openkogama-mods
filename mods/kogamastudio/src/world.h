#pragma once
#include "okmod.h"
#include <string>
#include <vector>

namespace world {
    void Attach(const OkMod* host);
    bool Handle(const std::string& command, const std::string& value);
    void Tick();
    void Selected(ok_object controller, int id);
    void Changed();
    std::string Report();
    void TranslateStart(ok_object machine);
    void TranslateEnd();
    void RotationStart(ok_object helper);
    void LinkAdded(ok_object link);
    void LinkRemoved(ok_object output, ok_object input);
    void ObjectLinkAdded(ok_object link);
    void ObjectLinkRemoved(ok_object connector, ok_object object);
    ok_object SelectedModel();
    int SelectedId();
    ok_object ModelById(int id);
    std::vector<ok_object> DictionaryValues(ok_object dictionary, const char* type);
    ok_object DictionaryKeys(ok_object dictionary, const char* type);
}
