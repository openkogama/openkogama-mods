#pragma once
#include <cstdint>
#include <string>

struct OkMod;

namespace editor {
    void Attach(const OkMod* host);
    void Tick(double dt);
    void Reset(const std::string& setting);

    bool MultiSelect();
    void SetMultiSelect(bool on);
    bool CtrlSelect();
    void SetCtrlSelect(bool on);
    bool FastLinks();
    void SetFastLinks(bool on);
    bool ObjectLinks();
    void SetObjectLinks(bool on);
    bool LinkFix();
    void SetLinkFix(bool on);
    bool Flags(uint64_t& mask, bool& override);
    void SetFlags(bool on, uint64_t mask, bool override);

    bool Pick(int& material, bool& unlocked);
    bool EnterGroup(int id);
    bool InGroup();
    bool Focus(int id);
    bool Owns(int id);
    bool DeselectAll();
}
