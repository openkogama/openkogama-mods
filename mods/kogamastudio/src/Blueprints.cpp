#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include "Blueprints.h"
#include "MenuHelpers.h"
#include "Explorer.h"
#include "GameInfo.h"
#include "IconsFontAwesome6.h"
#include "ui.h"
#include "imnodes/imnodes.h"
#include "json.hpp"
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Blueprints {

    // -------------------------------------------------------
    // Types
    // -------------------------------------------------------
    enum class NodeType {
        LogicAND,
        LogicNOT,
        LogicTOGGLE,
        Button,
        Light,
        CubeModelTransparency,
        TriggerCube,
        PulseCube,
        PressurePlate,
        TargetCube,
        CountingCube,
        RandomCube,
        CubeModelHider,
        DelayCube,
        Custom,
        _EditorInput,
        _EditorOutput,
    };

    struct Pin {
        int id;
        std::string name;
        std::string dataType;
        bool isOutput;
    };

    struct Node {
        int id;
        NodeType type;
        std::string title;
        std::vector<Pin> pins;
        ImVec2 position;
        bool positionSet;
        bool posIsEditorSpace = false; // true = position is editor-space (loaded from config)
        int customDefIndex = -1;
        std::string customDefName;   // persistent key — resolved to customDefIndex at runtime
        int value = 0;
        bool multiLink = false; // allow multiple links on input pins
        double timerStart     = -1.0; // Pulse/Delay: when current timer phase started (-1 = idle)
        int    prevInputValue =  0;   // Toggle/Delay/Random: previous combined input (rising-edge detection)
        std::vector<int> outputValues; // per-output-pin values (used by RandomCube; empty = all use node.value)
    };

    struct Link {
        int id;
        int startPinId;
        int endPinId;
    };

    struct CustomNodeDef {
        std::string name;
        std::vector<Node> innerNodes;
        std::vector<Link> innerLinks;
        int innerNextId;
        ImVec4 color = ImVec4(0.314f, 0.314f, 0.314f, 1.0f);
        std::vector<std::string> inputPinNames   = { "In" };
        std::vector<std::string> outputPinNames  = { "Out" };
        std::vector<bool>        inputPinMulti   = { false };
        std::vector<bool>        outputPinMulti  = { false };
    };

    // -------------------------------------------------------
    // Palette structs (functions defined after state)
    // -------------------------------------------------------
    struct PaletteEntry {
        bool        isCustom       = false;
        NodeType    builtinType    = NodeType::LogicAND;
        int         customDefIndex = -1;
        std::string customDefName;   // persistent key
    };
    struct PaletteCategory {
        std::string               name;
        std::vector<PaletteEntry> entries;
        bool                      open = true;
        bool                      isBuiltin = false;
    };

    // -------------------------------------------------------
    // Drag state
    // -------------------------------------------------------
    struct DragPayload {
        bool active = false;
        bool isCustom = false;
        NodeType builtinType = NodeType::LogicAND;
        int customIndex = -1;
        std::string customDefName;
        std::string label;
    };
    static DragPayload drag;

    // -------------------------------------------------------
    // Per-circuit storage
    // -------------------------------------------------------
    struct LogicCircuit {
        std::string groupId;          // Explorer group ID
        std::string name;             // display name
        std::vector<Node> circuitNodes;
        std::vector<Link> circuitLinks;
        int circuitNextId = 1;
    };
    static std::vector<LogicCircuit> circuits;
    static std::string activeCircuitId;   // Explorer group ID of the active circuit ("" = none)
    static std::unordered_set<std::string> logicCircuitGroupIds; // set of Explorer group IDs that are logic circuits

    static LogicCircuit* FindCircuit(const std::string& groupId) {
        for (auto& c : circuits)
            if (c.groupId == groupId) return &c;
        return nullptr;
    }

    // Forward declarations
    static int ResolveCustomDefIndex(const std::string& name);
    static void ResolveAllCustomDefIndices();

    // Save current working nodes/links back to the active circuit
    static void SaveActiveCircuit();
    // Load a circuit's data into working nodes/links
    static void LoadCircuit(const std::string& groupId);

    // -------------------------------------------------------
    // State (working copies for the currently active circuit)
    // -------------------------------------------------------
    static int nextId = 1;
    static int AllocId() { return nextId++; }

    static ImNodesEditorContext* editorCtx = nullptr;
    static std::vector<Node> nodes;
    static std::vector<Link> links;

    static std::vector<CustomNodeDef> customDefs;

    static bool editingCustom = false;
    static ImNodesEditorContext* customEditorCtx = nullptr;
    static std::vector<Node> customNodes;
    static std::vector<Link> customLinks;
    static int customNextId = 1;
    static char customName[64] = {};
    static int editingDefIndex = -1;

    static int CustomAllocId() { return customNextId++; }
    static ImVec2 sCustomCanvasCenter = ImVec2(400.0f, 300.0f); // updated each frame
    static ImVec4 customColor = ImVec4(0.314f, 0.314f, 0.314f, 1.0f);

    static std::vector<std::string> customInputPinNames;
    static std::vector<std::string> customOutputPinNames;
    static std::vector<bool>        customInputMulti;
    static std::vector<bool>        customOutputMulti;
    static std::vector<int>         customInputNodeIds;
    static std::vector<int>         customOutputNodeIds;

    // Palette state (after customDefs so EntryLabel can reference it)
    static std::vector<PaletteCategory> paletteCategories;
    static std::vector<PaletteEntry> sUncategorized;  // items outside any category
    static float sPaletteWidth = 160.0f;       // resizable palette width (main editor)
    static float sCustomPaletteWidth = 130.0f;  // resizable palette width (custom editor)
    static std::unordered_map<int, int> sLinkValueOverride; // linkId → forced value (used by RandomCube)

    // Vertical splitter: invisible button between palette and canvas
    static bool VerticalSplitter(const char* id, float* width, float minW, float maxW, float height) {
        ImGui::SameLine(0, 0);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(8.0f, height));
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();
        if (hovered || held)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (held) {
            float delta = ImGui::GetIO().MouseDelta.x;
            *width += delta;
            if (*width < minW) *width = minW;
            if (*width > maxW) *width = maxW;
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        float cx = pos.x + 4.0f;
        ImU32 col = held ? IM_COL32(200, 200, 200, 255) : hovered ? IM_COL32(150, 150, 150, 200) : IM_COL32(80, 80, 80, 120);
        dl->AddRectFilled(ImVec2(cx - 1.5f, pos.y + 4), ImVec2(cx + 1.5f, pos.y + height - 4), col);
        ImGui::SameLine(0, 0);
        return held;
    }

    // -------------------------------------------------------
    // Name resolver (definitions after state)
    // -------------------------------------------------------
    static int ResolveCustomDefIndex(const std::string& name) {
        if (name.empty()) return -1;
        for (int i = 0; i < (int)customDefs.size(); i++)
            if (customDefs[i].name == name) return i;
        return -1;
    }

    static void ResolveAllCustomDefIndices() {
        auto resolveNodes = [](std::vector<Node>& nodeList) {
            for (auto& n : nodeList) {
                if (n.type == NodeType::Custom && !n.customDefName.empty())
                    n.customDefIndex = ResolveCustomDefIndex(n.customDefName);
            }
        };
        resolveNodes(nodes);
        for (auto& c : circuits) resolveNodes(c.circuitNodes);
        for (auto& def : customDefs) resolveNodes(def.innerNodes);
        for (auto& cat : paletteCategories)
            for (auto& e : cat.entries)
                if (e.isCustom && !e.customDefName.empty())
                    e.customDefIndex = ResolveCustomDefIndex(e.customDefName);
        for (auto& e : sUncategorized)
            if (e.isCustom && !e.customDefName.empty())
                e.customDefIndex = ResolveCustomDefIndex(e.customDefName);
    }

    // -------------------------------------------------------
    // Circuit switching
    // -------------------------------------------------------
    static void SaveActiveCircuit() {
        if (activeCircuitId.empty()) return;
        LogicCircuit* lc = FindCircuit(activeCircuitId);
        if (!lc) return;
        // Positions are already up-to-date (tracked every frame in RenderNodes)
        lc->circuitNodes = nodes;
        lc->circuitLinks = links;
        lc->circuitNextId = nextId;
    }

    static void LoadCircuit(const std::string& groupId) {
        SaveActiveCircuit();
        activeCircuitId = groupId;
        sLinkValueOverride.clear();

        // Reset imnodes editor context -clears internal node position cache
        if (editorCtx) {
            ImNodes::EditorContextFree(editorCtx);
        }
        editorCtx = ImNodes::EditorContextCreate();

        if (groupId.empty()) {
            nodes.clear();
            links.clear();
            nextId = 1;
            return;
        }
        LogicCircuit* lc = FindCircuit(groupId);
        if (!lc) {
            LogicCircuit newLc;
            newLc.groupId = groupId;
            const Explorer::ObjectEntry* obj = Explorer::FindById(groupId);
            newLc.name = obj ? obj->name : ("Circuit " + groupId);
            circuits.push_back(newLc);
            lc = &circuits.back();
        }
        nodes = lc->circuitNodes;
        links = lc->circuitLinks;
        nextId = lc->circuitNextId;
        for (auto& node : nodes) {
            node.positionSet = false;
            node.posIsEditorSpace = true;
        }
    }

    // -------------------------------------------------------
    // Paths
    // -------------------------------------------------------
    static std::string GetLocalAppData() {
        char* buffer = nullptr;
        size_t len;
        _dupenv_s(&buffer, &len, u8"APPDATA");
        std::string path = std::string(buffer) + u8"\\..\\Local";
        free(buffer);
        return path;
    }

    // Palette config -global, per user
    static std::string GetPaletteConfigPath() {
        return GetLocalAppData() + u8"\\KogamaStudio\\Config\\blueprints_palette.json";
    }

    // Global custom node definitions
    static std::string GetCustomNodesConfigPath() {
        return GetLocalAppData() + u8"\\KogamaStudio\\Config\\custom_nodes.json";
    }

    // Project data -per planet (world)
    static std::string GetProjectPath() {
        int pid = GameInfo::planetID;
        if (pid <= 0) return "";
        return GetLocalAppData() + u8"\\KogamaStudio\\Projects\\" + std::to_string(pid);
    }

    static std::string GetProjectBlueprintsPath() {
        std::string proj = GetProjectPath();
        if (proj.empty()) return "";
        return proj + u8"\\blueprints.json";
    }

    // -------------------------------------------------------
    // Helpers
    // -------------------------------------------------------
    static int NodeTypeToInt(NodeType t) { return (int)t; }
    static NodeType IntToNodeType(int i) { return (NodeType)i; }

    static std::vector<Node> ParseNodes(const json& jNodes) {
        std::vector<Node> result;
        for (auto& jn : jNodes) {
            Node nd;
            nd.id = jn.value("id", 0);
            nd.type = IntToNodeType(jn.value("type", 0));
            nd.title = jn.value("title", "");
            nd.customDefName = jn.value("customDefName", "");
            nd.customDefIndex = jn.value("customDefIndex", -1);
            // Backward compat: if name missing but index present, resolve from current defs
            if (nd.customDefName.empty() && nd.type == NodeType::Custom
                && nd.customDefIndex >= 0 && nd.customDefIndex < (int)customDefs.size())
                nd.customDefName = customDefs[nd.customDefIndex].name;
            nd.multiLink = jn.value("multiLink", false);
            nd.value = jn.value("value", 0);
            if (jn.contains("pos") && jn["pos"].size() >= 2)
                nd.position = ImVec2(jn["pos"][0].get<float>(), jn["pos"][1].get<float>());
            nd.positionSet = false;
            nd.posIsEditorSpace = true;
            if (jn.contains("pins")) {
                for (auto& jp : jn["pins"]) {
                    Pin p;
                    p.id = jp.value("id", 0);
                    p.name = jp.value("name", "");
                    p.dataType = jp.value("dataType", "Bit");
                    p.isOutput = jp.value("isOutput", false);
                    nd.pins.push_back(p);
                }
            }
            result.push_back(nd);
        }
        return result;
    }

    static std::vector<Link> ParseLinks(const json& jLinks) {
        std::vector<Link> result;
        for (auto& jl : jLinks) {
            Link lk;
            lk.id = jl.value("id", 0);
            lk.startPinId = jl.value("start", 0);
            lk.endPinId = jl.value("end", 0);
            result.push_back(lk);
        }
        return result;
    }

    static json SerializeNodes(const std::vector<Node>& nodeList) {
        json jNodes = json::array();
        for (auto& nd : nodeList) {
            json jn;
            jn["id"] = nd.id;
            jn["type"] = NodeTypeToInt(nd.type);
            jn["title"] = nd.title;
            if (nd.type == NodeType::Custom && !nd.customDefName.empty())
                jn["customDefName"] = nd.customDefName;
            jn["multiLink"] = nd.multiLink;
            jn["value"] = nd.value;
            jn["pos"] = {nd.position.x, nd.position.y};
            json jPins = json::array();
            for (auto& p : nd.pins)
                jPins.push_back({{"id", p.id}, {"name", p.name}, {"dataType", p.dataType}, {"isOutput", p.isOutput}});
            jn["pins"] = jPins;
            jNodes.push_back(jn);
        }
        return jNodes;
    }

    static json SerializeLinks(const std::vector<Link>& linkList) {
        json jLinks = json::array();
        for (auto& lk : linkList)
            jLinks.push_back({{"id", lk.id}, {"start", lk.startPinId}, {"end", lk.endPinId}});
        return jLinks;
    }

    // -------------------------------------------------------
    // Palette save/load (global config)
    // -------------------------------------------------------
    static json SerializePaletteEntry(const PaletteEntry& e) {
        json je;
        je["isCustom"] = e.isCustom;
        je["builtinType"] = NodeTypeToInt(e.builtinType);
        if (e.isCustom && !e.customDefName.empty())
            je["customDefName"] = e.customDefName;
        return je;
    }

    static PaletteEntry ParsePaletteEntry(const json& je) {
        PaletteEntry e;
        e.isCustom = je.value("isCustom", false);
        e.builtinType = IntToNodeType(je.value("builtinType", 0));
        e.customDefName = je.value("customDefName", "");
        e.customDefIndex = je.value("customDefIndex", -1);
        if (e.isCustom && e.customDefName.empty()
            && e.customDefIndex >= 0 && e.customDefIndex < (int)customDefs.size())
            e.customDefName = customDefs[e.customDefIndex].name;
        return e;
    }

    static void SavePaletteConfig() {
        json config;
        json jPalette = json::array();
        for (auto& cat : paletteCategories) {
            if (cat.isBuiltin) continue;
            json jCat;
            jCat["name"] = cat.name;
            jCat["open"] = cat.open;
            json jEntries = json::array();
            for (auto& e : cat.entries)
                jEntries.push_back(SerializePaletteEntry(e));
            jCat["entries"] = jEntries;
            jPalette.push_back(jCat);
        }
        config["palette"] = jPalette;

        json jUncat = json::array();
        for (auto& e : sUncategorized)
            jUncat.push_back(SerializePaletteEntry(e));
        config["uncategorized"] = jUncat;

        std::string path = GetPaletteConfigPath();
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream file(path);
        file << config.dump(2);
    }

    static void LoadPaletteConfig() {
        std::string path = GetPaletteConfigPath();
        if (!fs::exists(path)) return;
        try {
            json config = json::parse(std::ifstream(path));
            if (config.contains("palette")) {
                for (auto& jCat : config["palette"]) {
                    PaletteCategory cat;
                    cat.name = jCat.value("name", "Category");
                    cat.open = jCat.value("open", true);
                    cat.isBuiltin = false;
                    if (jCat.contains("entries"))
                        for (auto& je : jCat["entries"])
                            cat.entries.push_back(ParsePaletteEntry(je));
                    paletteCategories.push_back(cat);
                }
            }
            if (config.contains("uncategorized")) {
                for (auto& je : config["uncategorized"])
                    sUncategorized.push_back(ParsePaletteEntry(je));
            }
        } catch (...) {}
    }

    // Forward declarations
    static void MarkDirty();
    static void SaveConfig();

    // -------------------------------------------------------
    // Global custom node definitions (Config/custom_nodes.json)
    // -------------------------------------------------------
    static bool sCustomDefsLoaded = false;

    static json SerializeSingleDef(const CustomNodeDef& def) {
        json jd;
        jd["name"] = def.name;
        jd["color"] = {def.color.x, def.color.y, def.color.z, def.color.w};
        jd["innerNextId"] = def.innerNextId;
        json jIN = json::array(); for (auto& n : def.inputPinNames)  jIN.push_back(n);  jd["inputPinNames"]  = jIN;
        json jON = json::array(); for (auto& n : def.outputPinNames) jON.push_back(n);  jd["outputPinNames"] = jON;
        json jIM = json::array(); for (auto b : def.inputPinMulti)   jIM.push_back(b);  jd["inputPinMulti"]  = jIM;
        json jOM = json::array(); for (auto b : def.outputPinMulti)  jOM.push_back(b);  jd["outputPinMulti"] = jOM;
        jd["innerNodes"] = SerializeNodes(def.innerNodes);
        jd["innerLinks"] = SerializeLinks(def.innerLinks);
        return jd;
    }

    static CustomNodeDef ParseSingleDef(const json& jd) {
        CustomNodeDef def;
        def.name = jd.value("name", "Custom");
        if (jd.contains("color") && jd["color"].size() >= 4)
            def.color = ImVec4(jd["color"][0], jd["color"][1], jd["color"][2], jd["color"][3]);
        def.innerNextId = jd.value("innerNextId", 1);
        def.inputPinNames.clear();
        if (jd.contains("inputPinNames"))
            for (auto& n : jd["inputPinNames"]) def.inputPinNames.push_back(n.get<std::string>());
        def.outputPinNames.clear();
        if (jd.contains("outputPinNames"))
            for (auto& n : jd["outputPinNames"]) def.outputPinNames.push_back(n.get<std::string>());
        def.inputPinMulti.clear();
        if (jd.contains("inputPinMulti"))
            for (auto& b : jd["inputPinMulti"]) def.inputPinMulti.push_back(b.get<bool>());
        def.outputPinMulti.clear();
        if (jd.contains("outputPinMulti"))
            for (auto& b : jd["outputPinMulti"]) def.outputPinMulti.push_back(b.get<bool>());
        if (jd.contains("innerNodes"))
            def.innerNodes = ParseNodes(jd["innerNodes"]);
        if (jd.contains("innerLinks"))
            def.innerLinks = ParseLinks(jd["innerLinks"]);
        return def;
    }

    static void SaveCustomDefsGlobal() {
        json root;
        json jDefs = json::array();
        for (auto& def : customDefs)
            jDefs.push_back(SerializeSingleDef(def));
        root["customDefs"] = jDefs;

        std::string path = GetCustomNodesConfigPath();
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream file(path);
        file << root.dump(2);
    }

    static void LoadCustomDefsGlobal() {
        if (sCustomDefsLoaded) return;
        sCustomDefsLoaded = true;
        customDefs.clear();

        std::string path = GetCustomNodesConfigPath();
        if (!fs::exists(path)) return;
        try {
            json root = json::parse(std::ifstream(path));
            if (root.contains("customDefs")) {
                for (auto& jd : root["customDefs"])
                    customDefs.push_back(ParseSingleDef(jd));
            }
        } catch (...) {}
    }

    // Merge custom defs from a JSON array into global customDefs (skip duplicates by name)
    static void MergeCustomDefsFromJson(const json& jDefs) {
        for (auto& jd : jDefs) {
            CustomNodeDef def = ParseSingleDef(jd);
            bool exists = false;
            for (auto& existing : customDefs)
                if (existing.name == def.name) { exists = true; break; }
            if (!exists)
                customDefs.push_back(def);
        }
    }

    // Rename helper: update all references when a custom def is renamed
    static void RenameCustomDef(int defIndex, const std::string& newName) {
        if (defIndex < 0 || defIndex >= (int)customDefs.size()) return;
        std::string oldName = customDefs[defIndex].name;
        if (oldName == newName) return;
        customDefs[defIndex].name = newName;
        auto updateNodes = [&](std::vector<Node>& nodeList) {
            for (auto& n : nodeList) {
                if (n.type == NodeType::Custom && n.customDefName == oldName) {
                    n.customDefName = newName;
                    n.title = newName;
                }
            }
        };
        updateNodes(nodes);
        for (auto& c : circuits) updateNodes(c.circuitNodes);
        for (auto& d : customDefs) updateNodes(d.innerNodes);
        for (auto& cat : paletteCategories)
            for (auto& e : cat.entries)
                if (e.isCustom && e.customDefName == oldName)
                    e.customDefName = newName;
    }

    // -------------------------------------------------------
    // Project save/load (per planet -circuits, custom defs, LC IDs)
    // -------------------------------------------------------
    static int sLoadedPlanetID = -1; // track which planet we loaded for

    static void SaveProjectData() {
        std::string path = GetProjectBlueprintsPath();
        if (path.empty()) return;

        SaveActiveCircuit();

        json proj;

        json jCircuitIds = json::array();
        for (auto& id : logicCircuitGroupIds) jCircuitIds.push_back(id);
        proj["logicCircuitGroupIds"] = jCircuitIds;

        json jCircuits = json::array();
        for (auto& c : circuits) {
            json jc;
            jc["groupId"] = c.groupId;
            jc["name"] = c.name;
            jc["nextId"] = c.circuitNextId;
            jc["nodes"] = SerializeNodes(c.circuitNodes);
            jc["links"] = SerializeLinks(c.circuitLinks);
            jCircuits.push_back(jc);
        }
        proj["circuits"] = jCircuits;

        fs::create_directories(fs::path(path).parent_path());
        std::ofstream file(path);
        file << proj.dump(2);
    }

    static void LoadProjectData() {
        // Clear previous project data (customDefs is global, not cleared here)
        circuits.clear();
        logicCircuitGroupIds.clear();
        activeCircuitId.clear();
        nodes.clear();
        links.clear();
        nextId = 1;
        sLinkValueOverride.clear();

        std::string path = GetProjectBlueprintsPath();
        if (path.empty() || !fs::exists(path)) return;
        try {
            json proj = json::parse(std::ifstream(path));

            // Migration: if old per-world customDefs exist, merge into global config
            if (proj.contains("customDefs")) {
                MergeCustomDefsFromJson(proj["customDefs"]);
                SaveCustomDefsGlobal();
            }

            if (proj.contains("logicCircuitGroupIds")) {
                for (auto& id : proj["logicCircuitGroupIds"])
                    logicCircuitGroupIds.insert(id.get<std::string>());
            }

            if (proj.contains("circuits")) {
                for (auto& jc : proj["circuits"]) {
                    LogicCircuit lc;
                    lc.groupId = jc.value("groupId", "");
                    lc.name = jc.value("name", "");
                    lc.circuitNextId = jc.value("nextId", 1);
                    if (jc.contains("nodes"))
                        lc.circuitNodes = ParseNodes(jc["nodes"]);
                    if (jc.contains("links"))
                        lc.circuitLinks = ParseLinks(jc["links"]);
                    circuits.push_back(lc);
                }
            }
        } catch (...) {}
        // Resolve name-based references now that circuits and global defs are loaded
        ResolveAllCustomDefIndices();
    }

    // Reload project data if planet changed
    static void EnsureProjectLoaded() {
        int pid = GameInfo::planetID;
        if (pid != sLoadedPlanetID) {
            if (sLoadedPlanetID > 0)
                SaveProjectData(); // save previous planet first
            sLoadedPlanetID = pid;
            if (pid > 0)
                LoadProjectData();
        }
    }

    // -------------------------------------------------------
    // Combined save/load + auto-save
    // -------------------------------------------------------
    static bool sPaletteLoaded = false;

    static void SaveConfig() {
        SaveCustomDefsGlobal();
        SavePaletteConfig();
        SaveProjectData();
    }

    static void LoadConfig() {
        LoadCustomDefsGlobal(); // global custom defs must load before palette and project
        if (!sPaletteLoaded) {
            sPaletteLoaded = true;
            LoadPaletteConfig();
        }
        EnsureProjectLoaded();
    }

    // Auto-save timer
    static double sLastSaveTime = -1.0;
    static bool sConfigDirty = false;
    static void MarkDirty() { sConfigDirty = true; }
    static void AutoSave() {
        if (!sConfigDirty) return;
        double now = ImGui::GetTime();
        if (sLastSaveTime < 0) sLastSaveTime = now;
        if (now - sLastSaveTime < 2.0) return;
        sLastSaveTime = now;
        sConfigDirty = false;
        SaveConfig();
    }

    // -------------------------------------------------------
    // Clipboard
    // -------------------------------------------------------
    struct ClipboardNode {
        NodeType type;
        std::string title;
        int customDefIndex;
        std::string customDefName;
        ImVec2 relPos;          // relative to selection center
        bool multiLink;
        std::vector<Pin> pins;  // original pin IDs kept for link remapping
        int value;
    };
    struct ClipboardLink {
        int startPinId;         // original pin IDs (remapped on paste)
        int endPinId;
    };
    static std::vector<ClipboardNode> sClipboardNodes;
    static std::vector<ClipboardLink> sClipboardLinks;

    static void CopySelected(const std::vector<Node>& nodeList, const std::vector<Link>& linkList) {
        sClipboardNodes.clear();
        sClipboardLinks.clear();

        int numSel = ImNodes::NumSelectedNodes();
        if (numSel <= 0) return;
        std::vector<int> selIds(numSel);
        ImNodes::GetSelectedNodes(selIds.data());

        // Collect selected node IDs into a set
        std::unordered_map<int, int> nodeIdSet; // nodeId -> index in selIds
        for (int i = 0; i < numSel; i++) nodeIdSet[selIds[i]] = i;

        // Compute center of selection
        ImVec2 center = {0, 0};
        int count = 0;
        for (auto& node : nodeList) {
            if (nodeIdSet.count(node.id)) {
                ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id);
                center.x += pos.x;
                center.y += pos.y;
                count++;
            }
        }
        if (count > 0) { center.x /= count; center.y /= count; }

        // Collect pin IDs belonging to selected nodes
        std::unordered_map<int, bool> selPinIds;
        for (auto& node : nodeList) {
            if (!nodeIdSet.count(node.id)) continue;
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id);
            ClipboardNode cn;
            cn.type = node.type;
            cn.title = node.title;
            cn.customDefIndex = node.customDefIndex;
            cn.customDefName = node.customDefName;
            cn.relPos = ImVec2(pos.x - center.x, pos.y - center.y);
            cn.multiLink = node.multiLink;
            cn.pins = node.pins;
            cn.value = node.value;
            sClipboardNodes.push_back(cn);
            for (auto& pin : node.pins) selPinIds[pin.id] = true;
        }

        // Copy links where both endpoints are in selected nodes
        for (auto& link : linkList) {
            if (selPinIds.count(link.startPinId) && selPinIds.count(link.endPinId))
                sClipboardLinks.push_back({link.startPinId, link.endPinId});
        }
    }

    static void PasteClipboard(std::vector<Node>& nodeList, std::vector<Link>& linkList,
                                int (*allocFn)(), ImVec2 pasteCenter) {
        if (sClipboardNodes.empty()) return;

        // Map old pin IDs to new pin IDs
        std::unordered_map<int, int> pinRemap;

        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();

        for (auto& cn : sClipboardNodes) {
            Node n;
            n.id = allocFn();
            n.type = cn.type;
            n.title = cn.title;
            n.customDefIndex = cn.customDefIndex;
            n.customDefName = cn.customDefName;
            n.position = ImVec2(pasteCenter.x + cn.relPos.x, pasteCenter.y + cn.relPos.y);
            n.positionSet = false;
            n.multiLink = cn.multiLink;
            n.value = 0;
            for (auto& oldPin : cn.pins) {
                int newPinId = allocFn();
                pinRemap[oldPin.id] = newPinId;
                n.pins.push_back({newPinId, oldPin.name, oldPin.dataType, oldPin.isOutput});
            }
            nodeList.push_back(n);
        }

        for (auto& cl : sClipboardLinks) {
            auto itStart = pinRemap.find(cl.startPinId);
            auto itEnd   = pinRemap.find(cl.endPinId);
            if (itStart != pinRemap.end() && itEnd != pinRemap.end())
                linkList.push_back({allocFn(), itStart->second, itEnd->second});
        }
    }

    static void DuplicateSelected(std::vector<Node>& nodeList, std::vector<Link>& linkList,
                                   int (*allocFn)()) {
        CopySelected(nodeList, linkList);
        // Paste offset slightly from center of selection
        int numSel = ImNodes::NumSelectedNodes();
        if (numSel <= 0) return;
        std::vector<int> selIds(numSel);
        ImNodes::GetSelectedNodes(selIds.data());
        ImVec2 center = {0, 0};
        int count = 0;
        for (auto& node : nodeList) {
            for (int i = 0; i < numSel; i++) {
                if (node.id == selIds[i]) {
                    ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id);
                    center.x += pos.x; center.y += pos.y; count++;
                    break;
                }
            }
        }
        if (count > 0) { center.x /= count; center.y /= count; }
        PasteClipboard(nodeList, linkList, allocFn, ImVec2(center.x + 30.0f, center.y + 30.0f));
    }

    static void DeleteSelected(std::vector<Node>& nodeList, std::vector<Link>& linkList,
                                bool protectSpecial) {
        int numNodes = ImNodes::NumSelectedNodes();
        if (numNodes > 0) {
            std::vector<int> sel(numNodes);
            ImNodes::GetSelectedNodes(sel.data());
            for (int nodeId : sel) {
                auto it = std::find_if(nodeList.begin(), nodeList.end(),
                    [nodeId](const Node& n) { return n.id == nodeId; });
                if (it != nodeList.end()) {
                    if (protectSpecial && (it->type == NodeType::_EditorInput || it->type == NodeType::_EditorOutput))
                        continue;
                    for (auto& pin : it->pins)
                        linkList.erase(std::remove_if(linkList.begin(), linkList.end(),
                            [&pin](const Link& l) { return l.startPinId == pin.id || l.endPinId == pin.id; }), linkList.end());
                    nodeList.erase(it);
                }
            }
            ImNodes::ClearNodeSelection();
        }
        int numLinks = ImNodes::NumSelectedLinks();
        if (numLinks > 0) {
            std::vector<int> sel(numLinks);
            ImNodes::GetSelectedLinks(sel.data());
            for (int lid : sel)
                linkList.erase(std::remove_if(linkList.begin(), linkList.end(),
                    [lid](const Link& l) { return l.id == lid; }), linkList.end());
            ImNodes::ClearLinkSelection();
        }
    }

    static void HandleClipboardShortcuts(std::vector<Node>& nodeList, std::vector<Link>& linkList,
                                          int (*allocFn)(), bool protectSpecial) {
        bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            CopySelected(nodeList, linkList);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            ImVec2 mp = ImGui::GetMousePos();
            // Convert screen pos to editor space approximately
            PasteClipboard(nodeList, linkList, allocFn, mp);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            DuplicateSelected(nodeList, linkList, allocFn);
    }

    static const char* EntryLabel(const PaletteEntry& e) {
        if (e.isCustom) {
            if (e.customDefIndex >= 0 && e.customDefIndex < (int)customDefs.size())
                return customDefs[e.customDefIndex].name.c_str();
            return "?";
        }
        switch (e.builtinType) {
        case NodeType::LogicAND:              return u8"AND CUBE";
        case NodeType::LogicNOT:              return u8"NEGATE CUBE";
        case NodeType::LogicTOGGLE:           return u8"TOGGLE CUBE";
        case NodeType::Button:                return u8"LEVER";
        case NodeType::Light:                 return u8"LIGHT CUBE";
        case NodeType::CubeModelTransparency: return u8"CUBE MODEL TRANSPARENCY";
        case NodeType::TriggerCube:           return u8"TRIGGER CUBE";
        case NodeType::PulseCube:             return u8"PULSE CUBE";
        case NodeType::PressurePlate:         return u8"PRESSURE PLATE";
        case NodeType::TargetCube:            return u8"TARGET CUBE";
        case NodeType::CountingCube:          return u8"COUNTING CUBE";
        case NodeType::RandomCube:            return u8"RANDOM CUBE";
        case NodeType::CubeModelHider:        return u8"CUBE MODEL HIDER";
        case NodeType::DelayCube:             return u8"DELAY CUBE";
        default: return "?";
        }
    }

    static void InitPalette() {
        LoadConfig();
        // Always ensure the built-in "Advanced Logic" category exists at front
        bool hasBuiltin = false;
        for (auto& cat : paletteCategories)
            if (cat.isBuiltin) { hasBuiltin = true; break; }
        if (!hasBuiltin) {
            PaletteCategory cat;
            cat.name = u8"Advanced Logic";
            cat.isBuiltin = true;
            cat.entries = {
                {false, NodeType::CubeModelTransparency},
                {false, NodeType::Button},
                {false, NodeType::TriggerCube},
                {false, NodeType::PulseCube},
                {false, NodeType::PressurePlate},
                {false, NodeType::TargetCube},
                {false, NodeType::CountingCube},
                {false, NodeType::RandomCube},
                {false, NodeType::CubeModelHider},
                {false, NodeType::LogicAND},
                {false, NodeType::LogicNOT},
                {false, NodeType::DelayCube},
                {false, NodeType::LogicTOGGLE},
            };
            paletteCategories.insert(paletteCategories.begin(), cat);
        }
    }

    // -------------------------------------------------------
    // Node creation
    // -------------------------------------------------------
    static Node CreateBuiltinNode(NodeType type, ImVec2 pos, int (*allocFn)()) {
        Node n;
        n.id = allocFn();
        n.type = type;
        n.position = pos;
        n.positionSet = false;

        switch (type) {
        case NodeType::LogicAND:
            n.title = u8"AND CUBE";
            n.pins.push_back({ allocFn(), u8"In",  u8"Bit", false });
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true });
            break;
        case NodeType::LogicNOT:
            n.title = u8"NEGATE CUBE";
            n.pins.push_back({ allocFn(), u8"In",  u8"Bit", false });
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true });
            break;
        case NodeType::LogicTOGGLE:
            n.title = u8"TOGGLE CUBE";
            n.pins.push_back({ allocFn(), u8"In",  u8"Bit", false });
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true });
            break;
        case NodeType::Button:
            n.title = u8"LEVER";
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true });
            break;
        case NodeType::Light:
            n.title = u8"LIGHT CUBE";
            n.pins.push_back({ allocFn(), u8"In", u8"Bit", false });
            break;
        case NodeType::CubeModelTransparency:
            n.title = u8"CUBE MODEL TRANSPARENCY";
            n.pins.push_back({ allocFn(), u8"In",     u8"Bit",    false });
            n.pins.push_back({ allocFn(), u8"Object", u8"Object", true  });
            break;
        case NodeType::TriggerCube:
            n.title = u8"TRIGGER CUBE";
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true });
            break;
        case NodeType::PulseCube:
            n.title = u8"PULSE CUBE";
            n.pins.push_back({ allocFn(), u8"In",  u8"Bit", false });
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true  });
            break;
        case NodeType::PressurePlate:
            n.title = u8"PRESSURE PLATE";
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true });
            break;
        case NodeType::TargetCube:
            n.title = u8"TARGET CUBE";
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true });
            break;
        case NodeType::CountingCube:
            n.title = u8"COUNTING CUBE";
            n.pins.push_back({ allocFn(), u8"In",  u8"Bit", false });
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true  });
            break;
        case NodeType::RandomCube:
            n.title = u8"RANDOM CUBE";
            n.pins.push_back({ allocFn(), u8"In",  u8"Bit", false });
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true  });
            break;
        case NodeType::CubeModelHider:
            n.title = u8"CUBE MODEL HIDER";
            n.pins.push_back({ allocFn(), u8"In",     u8"Bit",    false });
            n.pins.push_back({ allocFn(), u8"Object", u8"Object", true  });
            break;
        case NodeType::DelayCube:
            n.title = u8"DELAY CUBE";
            n.pins.push_back({ allocFn(), u8"In",  u8"Bit", false });
            n.pins.push_back({ allocFn(), u8"Out", u8"Bit", true  });
            break;
        case NodeType::_EditorInput:
            n.title = u8"INPUT";
            n.pins.push_back({ allocFn(), u8"0", u8"Bit", true });
            break;
        case NodeType::_EditorOutput:
            n.title = u8"OUTPUT";
            n.pins.push_back({ allocFn(), u8"0", u8"Bit", false });
            break;
        default:
            break;
        }
        return n;
    }

    static Node CreateCustomNode(int defIndex, ImVec2 pos, int (*allocFn)() = nullptr) {
        if (!allocFn) allocFn = &AllocId;
        auto& def = customDefs[defIndex];
        Node n;
        n.id = allocFn();
        n.type = NodeType::Custom;
        n.title = def.name;
        n.position = pos;
        n.positionSet = false;
        n.customDefIndex = defIndex;
        n.customDefName = def.name;
        for (int gi = 0; gi < (int)def.inputPinNames.size(); gi++)
            n.pins.push_back({ allocFn(), def.inputPinNames[gi], u8"Bit", false });
        for (int gi = 0; gi < (int)def.outputPinNames.size(); gi++)
            n.pins.push_back({ allocFn(), def.outputPinNames[gi], u8"Bit", true });
        return n;
    }

    // -------------------------------------------------------
    // Palette item: draggable block
    // -------------------------------------------------------
    static void PaletteItem(const char* label, NodeType type, int customIdx = -1, const std::string& defName = "") {
        // Draw as a styled selectable block
        float w = ImGui::GetContentRegionAvail().x;
        ImVec2 size = ImVec2(w, ImGui::GetFrameHeight());
        ImVec2 cursor = ImGui::GetCursorScreenPos();

        ImGui::PushStyleColor(ImGuiCol_Header,        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

        ImGui::Selectable(label, false, 0, size);

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        // Start drag on mouse down
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            drag.active = true;
            drag.isCustom = (customIdx >= 0);
            drag.builtinType = type;
            drag.customIndex = customIdx;
            drag.customDefName = defName;
            drag.label = label;
        }
    }

    static void DrawDragOverlay() {
        if (!drag.active) return;

        // Draw floating label at cursor
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 textSize = ImGui::CalcTextSize(drag.label.c_str());
        ImVec2 padding = ImVec2(8, 4);
        ImVec2 boxMin = ImVec2(mouse.x + 12, mouse.y - textSize.y * 0.5f - padding.y);
        ImVec2 boxMax = ImVec2(boxMin.x + textSize.x + padding.x * 2, boxMin.y + textSize.y + padding.y * 2);

        fg->AddRectFilled(boxMin, boxMax, IM_COL32(50, 50, 50, 220), 4.0f);
        fg->AddRect(boxMin, boxMax, IM_COL32(150, 150, 150, 255), 4.0f);
        fg->AddText(ImVec2(boxMin.x + padding.x, boxMin.y + padding.y), IM_COL32(255, 255, 255, 255), drag.label.c_str());

        // Drop on mouse release
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            drag.active = false;
        }
    }

    // Check if drag was dropped over a given rect, and create the node
    static void HandleDrop(ImVec2 canvasMin, ImVec2 canvasMax,
                           std::vector<Node>& nodeList, int (*allocFn)(),
                           bool isMain) {
        // Trigger on the frame the mouse was released (drag just ended)
        if (drag.active) return; // still dragging
        if (drag.label.empty()) return; // no pending drop

        ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= canvasMin.x && mouse.x <= canvasMax.x &&
            mouse.y >= canvasMin.y && mouse.y <= canvasMax.y) {
            if (drag.isCustom) {
                nodeList.push_back(CreateCustomNode(drag.customIndex, mouse, allocFn));
            }
            else {
                nodeList.push_back(CreateBuiltinNode(drag.builtinType, mouse, allocFn));
            }
        }
        drag.label.clear();
    }

    // -------------------------------------------------------
    // Shared rendering
    // -------------------------------------------------------
    static void RenderNodes(std::vector<Node>& nodeList, std::vector<Link>& linkList, int (*allocFn)() = nullptr) {
        for (auto& node : nodeList) {
            if (!node.positionSet) {
                if (node.posIsEditorSpace)
                    ImNodes::SetNodeGridSpacePos(node.id, node.position);
                else
                    ImNodes::SetNodeScreenSpacePos(node.id, node.position);
                node.positionSet = true;
            }

            bool hasCustomColor = (node.type == NodeType::Custom && node.customDefIndex >= 0
                                   && node.customDefIndex < (int)customDefs.size());
            bool onlyInput  = !node.pins.empty() && std::all_of(node.pins.begin(), node.pins.end(), [](const Pin& p){ return !p.isOutput; });
            bool onlyOutput = !node.pins.empty() && std::all_of(node.pins.begin(), node.pins.end(), [](const Pin& p){ return  p.isOutput; });
            int colorsPushed = 0;
            if (hasCustomColor) {
                ImVec4 c = customDefs[node.customDefIndex].color;
                ImNodes::PushColorStyle(ImNodesCol_TitleBar,         IM_COL32((int)(c.x*255), (int)(c.y*255), (int)(c.z*255), 255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,  IM_COL32((int)(c.x*220), (int)(c.y*220), (int)(c.z*220), 255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32((int)(c.x*180), (int)(c.y*180), (int)(c.z*180), 255));
                colorsPushed = 3;
            } else if (onlyInput) {
                // green
                ImNodes::PushColorStyle(ImNodesCol_TitleBar,         IM_COL32(40,  110, 40,  255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,  IM_COL32(55,  135, 55,  255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(70,  160, 70,  255));
                colorsPushed = 3;
            } else if (onlyOutput) {
                // blue
                ImNodes::PushColorStyle(ImNodesCol_TitleBar,         IM_COL32(40,  80,  160, 255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,  IM_COL32(55,  100, 185, 255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(70,  120, 210, 255));
                colorsPushed = 3;
            } else {
                // gray for mixed (AND, NOT, TOGGLE, etc.)
                ImNodes::PushColorStyle(ImNodesCol_TitleBar,         IM_COL32(80,  80,  80,  255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,  IM_COL32(100, 100, 100, 255));
                ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(120, 120, 120, 255));
                colorsPushed = 3;
            }

            ImNodes::BeginNode(node.id);
            ImNodes::BeginNodeTitleBar();
            if (node.type == NodeType::_EditorInput) {
                int pinCount = 0;
                for (auto& p : node.pins) if (p.isOutput) pinCount++;
                if (pinCount > 1)
                    ImGui::Text(u8"IN (%d)", pinCount);
                else
                    ImGui::TextUnformatted(u8"IN");
            } else if (node.type == NodeType::_EditorOutput) {
                int pinCount = 0;
                for (auto& p : node.pins) if (!p.isOutput) pinCount++;
                if (pinCount > 1)
                    ImGui::Text(u8"OUT (%d)", pinCount);
                else
                    ImGui::TextUnformatted(u8"OUT");
            } else {
                ImGui::TextUnformatted(node.title.c_str());
            }
            ImNodes::EndNodeTitleBar();

            if (node.type == NodeType::_EditorInput || node.type == NodeType::_EditorOutput) {
                ImGui::PushID(node.id + 5000);
                char buf[32]; strncpy(buf, node.title.c_str(), 31); buf[31] = 0;
                ImGui::SetNextItemWidth(75.0f);
                if (ImGui::InputText("##pn", buf, 32)) {
                    node.title = buf;
                    for (int _i = 0; _i < (int)customInputNodeIds.size(); _i++)
                        if (customInputNodeIds[_i] == node.id) { customInputPinNames[_i] = buf; break; }
                    for (int _i = 0; _i < (int)customOutputNodeIds.size(); _i++)
                        if (customOutputNodeIds[_i] == node.id) { customOutputPinNames[_i] = buf; break; }
                }
                bool ml = node.multiLink;
                if (ImGui::Checkbox(u8"multi", &ml)) {
                    node.multiLink = ml;
                    for (int _i = 0; _i < (int)customInputNodeIds.size(); _i++)
                        if (customInputNodeIds[_i] == node.id) { customInputMulti[_i] = ml; break; }
                    for (int _i = 0; _i < (int)customOutputNodeIds.size(); _i++)
                        if (customOutputNodeIds[_i] == node.id) { customOutputMulti[_i] = ml; break; }
                }
                ImGui::PopID();
            }

            if (onlyOutput && node.type != NodeType::_EditorOutput) {
                ImGui::PushID(node.id);
                bool active = node.value != 0;
                if (ImGui::Checkbox(u8"Active", &active))
                    node.value = active ? 1 : 0;
                ImGui::PopID();
            }

            for (auto& pin : node.pins) {
                ImU32 pinCol, pinHovCol;
                bool isBus = (pin.dataType.size() > 3 && pin.dataType.find(u8"Bit[") == 0);
                if (!pin.isOutput) {
                    if (isBus) {
                        // bus input = purple
                        pinCol    = IM_COL32(160,  60, 200, 255);
                        pinHovCol = IM_COL32(190,  90, 230, 255);
                    } else {
                        // input = orange
                        pinCol    = IM_COL32(210, 120,  20, 255);
                        pinHovCol = IM_COL32(240, 155,  55, 255);
                    }
                } else if (pin.dataType == u8"Object") {
                    // output object = yellow
                    pinCol    = IM_COL32(220, 200,  30, 255);
                    pinHovCol = IM_COL32(255, 235,  70, 255);
                } else if (isBus) {
                    // bus output = purple
                    pinCol    = IM_COL32(160,  60, 200, 255);
                    pinHovCol = IM_COL32(190,  90, 230, 255);
                } else {
                    // output bit = blue
                    pinCol    = IM_COL32( 40, 100, 200, 255);
                    pinHovCol = IM_COL32( 70, 130, 235, 255);
                }
                ImNodes::PushColorStyle(ImNodesCol_Pin,        pinCol);
                ImNodes::PushColorStyle(ImNodesCol_PinHovered, pinHovCol);

                if (pin.isOutput) {
                    ImNodes::BeginOutputAttribute(pin.id);
                    float textWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
                    ImGui::Indent(100.0f - textWidth);
                    ImGui::TextUnformatted(pin.name.c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text(u8"%s: %s", TR(u8"Type"), pin.dataType.c_str());
                        ImGui::EndTooltip();
                    }
                    ImNodes::EndOutputAttribute();
                }
                else {
                    ImNodes::BeginInputAttribute(pin.id);
                    ImGui::TextUnformatted(pin.name.c_str());
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text(u8"%s: %s", TR(u8"Type"), pin.dataType.c_str());
                        ImGui::EndTooltip();
                    }
                    ImNodes::EndInputAttribute();
                }

                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
            ImNodes::EndNode();

            // Track editor-space position every frame so SaveActiveCircuit doesn't need imnodes context
            if (node.positionSet) {
                node.position = ImNodes::GetNodeGridSpacePos(node.id);
                node.posIsEditorSpace = true;
            }

            for (int _c = 0; _c < colorsPushed; _c++)
                ImNodes::PopColorStyle();
        }

        // Build output-pin → value map for link coloring (respects per-pin outputValues)
        std::unordered_map<int, int> outputPinValue;
        for (auto& node : nodeList) {
            int outIdx = 0;
            for (auto& pin : node.pins) {
                if (!pin.isOutput) { continue; }
                int val = (!node.outputValues.empty() && outIdx < (int)node.outputValues.size())
                          ? node.outputValues[outIdx] : node.value;
                outputPinValue[pin.id] = val;
                outIdx++;
            }
        }

        for (auto& link : linkList) {
            bool active;
            auto ovIt = sLinkValueOverride.find(link.id);
            if (ovIt != sLinkValueOverride.end()) {
                active = ovIt->second != 0;
            } else {
                auto it = outputPinValue.find(link.startPinId);
                active = (it != outputPinValue.end() && it->second != 0);
            }
            if (active) {
                ImNodes::PushColorStyle(ImNodesCol_Link,         IM_COL32(220, 200,  30, 255));
                ImNodes::PushColorStyle(ImNodesCol_LinkHovered,  IM_COL32(255, 235,  70, 255));
                ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 235,  70, 255));
            }
            ImNodes::Link(link.id, link.startPinId, link.endPinId);
            if (active) {
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();
            }
        }
    }

    // Simulate one step of blueprint logic (AND, NEGATE only; others ignored)
    // Forward declaration so Custom handling can call recursively
    static void SimulateBlueprintStep(std::vector<Node>& nodeList, const std::vector<Link>& linkList);

    // Helper: collect values arriving at all INPUT pins of a node from a pin-value snapshot
    static std::vector<int> GatherInputs(const Node& node, const std::vector<Link>& linkList,
                                         const std::unordered_map<int, int>& pinVal) {
        std::vector<int> inputs;
        for (auto& pin : node.pins) {
            if (pin.isOutput) continue;
            for (auto& link : linkList) {
                if (link.endPinId != pin.id) continue;
                // Per-link override takes priority (used by RandomCube)
                auto ovIt = sLinkValueOverride.find(link.id);
                if (ovIt != sLinkValueOverride.end()) {
                    inputs.push_back(ovIt->second);
                } else {
                    auto it = pinVal.find(link.startPinId);
                    if (it != pinVal.end())
                        inputs.push_back(it->second);
                }
            }
        }
        return inputs;
    }

    // Helper: build output-pin → value map, respecting per-pin outputValues
    static void BuildPinValMap(const std::vector<Node>& nodeList, std::unordered_map<int, int>& pinVal) {
        pinVal.clear();
        for (auto& node : nodeList) {
            int outIdx = 0;
            for (auto& pin : node.pins) {
                if (!pin.isOutput) continue;
                int val = (!node.outputValues.empty() && outIdx < (int)node.outputValues.size())
                          ? node.outputValues[outIdx] : node.value;
                pinVal[pin.id] = val;
                outIdx++;
            }
        }
    }

    static void SimulateBlueprintStep(std::vector<Node>& nodeList, const std::vector<Link>& linkList) {
        // --- Phase 1: converge combinational logic (AND, NEGATE, Custom, _EditorOutput) ---
        for (int pass = 0; pass < 8; pass++) {
            std::unordered_map<int, int> pinVal;
            BuildPinValMap(nodeList, pinVal);

            bool changed = false;
            for (auto& node : nodeList) {
                int newVal = node.value;

                if (node.type == NodeType::LogicAND) {
                    auto inputs = GatherInputs(node, linkList, pinVal);
                    newVal = (!inputs.empty() &&
                              std::all_of(inputs.begin(), inputs.end(), [](int v){ return v != 0; })) ? 1 : 0;

                } else if (node.type == NodeType::LogicNOT) {
                    auto inputs = GatherInputs(node, linkList, pinVal);
                    newVal = std::none_of(inputs.begin(), inputs.end(), [](int v){ return v != 0; }) ? 1 : 0;

                } else if (node.type == NodeType::_EditorOutput) {
                    auto inputs = GatherInputs(node, linkList, pinVal);
                    newVal = inputs.empty() ? 0 : inputs[0];

                } else if (node.type == NodeType::Custom &&
                           node.customDefIndex >= 0 &&
                           node.customDefIndex < (int)customDefs.size()) {
                    auto& def = customDefs[node.customDefIndex];
                    std::vector<Node> innerCopy = def.innerNodes;

                    // Map external input pins to EditorInput nodes
                    int inputGroupIdx = 0;
                    for (auto& inner : innerCopy) {
                        if (inner.type != NodeType::_EditorInput) continue;
                        int busVal = 0;
                        int extCount = 0;
                        for (auto& ep : node.pins) {
                            if (ep.isOutput) continue;
                            if (extCount == inputGroupIdx) {
                                for (auto& link : linkList) {
                                    if (link.endPinId != ep.id) continue;
                                    auto ovIt = sLinkValueOverride.find(link.id);
                                    if (ovIt != sLinkValueOverride.end()) { busVal = ovIt->second; break; }
                                    auto it = pinVal.find(link.startPinId);
                                    if (it != pinVal.end()) { busVal = it->second; break; }
                                }
                                break;
                            }
                            extCount++;
                        }
                        inner.value = busVal;
                        inputGroupIdx++;
                    }

                    SimulateBlueprintStep(innerCopy, def.innerLinks);

                    // Map EditorOutput values to external output pins
                    node.outputValues.clear();
                    for (auto& inner : innerCopy) {
                        if (inner.type != NodeType::_EditorOutput) continue;
                        node.outputValues.push_back(inner.value);
                    }
                    newVal = node.outputValues.empty() ? 0 : node.outputValues[0];

                } else {
                    continue;
                }

                if (newVal != node.value) { node.value = newVal; changed = true; }
            }
            if (!changed) break;
        }

        // --- Phase 2: timing nodes (run once per frame with converged input values) ---
        std::unordered_map<int, int> pinVal;
        BuildPinValMap(nodeList, pinVal);

        double now = ImGui::GetTime();

        for (auto& node : nodeList) {
            if (node.type == NodeType::PulseCube) {
                auto inputs = GatherInputs(node, linkList, pinVal);
                // Active when no inputs OR at least one input is true
                bool active = inputs.empty() ||
                              std::any_of(inputs.begin(), inputs.end(), [](int v){ return v != 0; });

                if (active) {
                    if (node.timerStart < 0.0) node.timerStart = now;
                    double elapsed = fmod(now - node.timerStart, 1.6);
                    node.value = (elapsed < 0.8) ? 1 : 0;
                } else {
                    node.timerStart = -1.0;
                    node.value = 0;
                }

            } else if (node.type == NodeType::RandomCube) {
                // Gather all output links (needed for both rising and falling edges)
                std::vector<int> outLinkIds;
                for (auto& pin : node.pins)
                    if (pin.isOutput)
                        for (auto& link : linkList)
                            if (link.startPinId == pin.id)
                                outLinkIds.push_back(link.id);

                auto inputs = GatherInputs(node, linkList, pinVal);
                int inputVal = std::any_of(inputs.begin(), inputs.end(), [](int v){ return v != 0; }) ? 1 : 0;

                if (inputVal == 1 && node.prevInputValue == 0) {
                    // Rising edge: pick one random link → 1, others → 0
                    if (!outLinkIds.empty()) {
                        int chosen = outLinkIds[rand() % (int)outLinkIds.size()];
                        for (auto id : outLinkIds)
                            sLinkValueOverride[id] = (id == chosen) ? 1 : 0;
                    }
                    node.value = 1;
                } else if (inputVal == 0 && node.value != 0) {
                    // Input off: force all overrides to 0 (don't erase -prevents stale pinVal leak)
                    for (auto id : outLinkIds)
                        sLinkValueOverride[id] = 0;
                    node.value = 0;
                }
                node.prevInputValue = inputVal;

            } else if (node.type == NodeType::LogicTOGGLE) {
                auto inputs = GatherInputs(node, linkList, pinVal);
                // Combined input: true if at least one input is true
                int inputVal = std::any_of(inputs.begin(), inputs.end(), [](int v){ return v != 0; }) ? 1 : 0;

                // Flip on rising edge (0→1) of the combined input
                if (inputVal == 1 && node.prevInputValue == 0)
                    node.value ^= 1;

                node.prevInputValue = inputVal;

            } else if (node.type == NodeType::DelayCube) {
                auto inputs = GatherInputs(node, linkList, pinVal);
                int inputVal = (!inputs.empty() &&
                                std::any_of(inputs.begin(), inputs.end(), [](int v){ return v != 0; })) ? 1 : 0;

                // Rising edge: (re-)trigger the sequence
                if (inputVal == 1 && node.prevInputValue == 0)
                    node.timerStart = now;

                node.prevInputValue = inputVal;

                if (node.timerStart >= 0.0) {
                    double elapsed = now - node.timerStart;
                    if (elapsed < 2.5) {
                        node.value = 0; // delay phase
                    } else if (elapsed < 5.0) {
                        node.value = 1; // on phase
                    } else {
                        node.value = 0; // done -idle until next rising edge
                        node.timerStart = -1.0;
                    }
                } else {
                    node.value = 0;
                }
            }
        }
    }

    static void ResetSimulation(std::vector<Node>& nodeList) {
        for (auto& node : nodeList) {
            node.value          = 0;
            node.timerStart     = -1.0;
            node.prevInputValue = 0;
            for (auto& v : node.outputValues) v = 0;
        }
        sLinkValueOverride.clear();
    }

    static bool PinAllowsMulti(int pinId, const std::vector<Node>& nodeList) {
        for (auto& node : nodeList) {
            for (int pi = 0; pi < (int)node.pins.size(); pi++) {
                if (node.pins[pi].id != pinId) continue;
                // EditorInput/EditorOutput: controlled per-node by multiLink flag
                if (node.type == NodeType::_EditorInput || node.type == NodeType::_EditorOutput)
                    return node.multiLink;
                // Custom nodes: controlled per-pin by def's inputPinMulti/outputPinMulti
                if (node.type == NodeType::Custom &&
                    node.customDefIndex >= 0 && node.customDefIndex < (int)customDefs.size()) {
                    auto& def = customDefs[node.customDefIndex];
                    int inputCount = (int)def.inputPinNames.size();
                    if (pi < inputCount)
                        return pi < (int)def.inputPinMulti.size() ? def.inputPinMulti[pi] : false;
                    else {
                        int oi = pi - inputCount;
                        return oi < (int)def.outputPinMulti.size() ? def.outputPinMulti[oi] : false;
                    }
                }
                // All other builtin nodes (AND, NOT, etc.) always allow multi-link
                return true;
            }
        }
        return true;
    }

    static void HandleLinks(std::vector<Link>& linkList, int (*allocFn)(),
                            const std::vector<Node>* constraintNodes = nullptr) {
        int startPin, endPin;
        if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
            if (constraintNodes) {
                // Enforce single-link on pins where multiLink is false
                auto enforce = [&](int pinId) {
                    if (!PinAllowsMulti(pinId, *constraintNodes))
                        linkList.erase(std::remove_if(linkList.begin(), linkList.end(),
                            [pinId](const Link& l){ return l.startPinId == pinId || l.endPinId == pinId; }), linkList.end());
                };
                enforce(startPin);
                enforce(endPin);
            }
            linkList.push_back({ allocFn(), startPin, endPin });
        }

        int destroyedLinkId;
        if (ImNodes::IsLinkDestroyed(&destroyedLinkId)) {
            auto it = std::find_if(linkList.begin(), linkList.end(),
                [destroyedLinkId](const Link& l) { return l.id == destroyedLinkId; });
            if (it != linkList.end()) linkList.erase(it);
        }
    }

    static void HandleDeletion(std::vector<Node>& nodeList, std::vector<Link>& linkList,
                               bool protectSpecial) {
        int numSelected = ImNodes::NumSelectedNodes();
        if (numSelected > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            std::vector<int> sel(numSelected);
            ImNodes::GetSelectedNodes(sel.data());
            for (int nodeId : sel) {
                auto it = std::find_if(nodeList.begin(), nodeList.end(),
                    [nodeId](const Node& n) { return n.id == nodeId; });
                if (it != nodeList.end()) {
                    if (protectSpecial && (it->type == NodeType::_EditorInput || it->type == NodeType::_EditorOutput))
                        continue;
                    for (auto& pin : it->pins)
                        linkList.erase(std::remove_if(linkList.begin(), linkList.end(),
                            [&pin](const Link& l) { return l.startPinId == pin.id || l.endPinId == pin.id; }), linkList.end());
                    nodeList.erase(it);
                }
            }
            ImNodes::ClearNodeSelection();
        }

        int numLinks = ImNodes::NumSelectedLinks();
        if (numLinks > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            std::vector<int> sel(numLinks);
            ImNodes::GetSelectedLinks(sel.data());
            for (int lid : sel)
                linkList.erase(std::remove_if(linkList.begin(), linkList.end(),
                    [lid](const Link& l) { return l.id == lid; }), linkList.end());
            ImNodes::ClearLinkSelection();
        }
    }

    // -------------------------------------------------------
    // Custom node editor
    // -------------------------------------------------------
    static void EnterCustomEditor(int defIndex) {
        editingCustom = true;
        editingDefIndex = defIndex;

        if (!customEditorCtx)
            customEditorCtx = ImNodes::EditorContextCreate();

        if (defIndex >= 0) {
            auto& def = customDefs[defIndex];
            strncpy(customName, def.name.c_str(), sizeof(customName) - 1);
            customNodes = def.innerNodes;
            customLinks = def.innerLinks;
            customNextId = def.innerNextId;
            customColor = def.color;
            customInputPinNames  = def.inputPinNames;
            customOutputPinNames = def.outputPinNames;
            customInputMulti     = def.inputPinMulti;
            customOutputMulti    = def.outputPinMulti;
            customInputNodeIds.clear();
            customOutputNodeIds.clear();
            for (auto& n : customNodes) {
                if (n.type == NodeType::_EditorInput)  customInputNodeIds.push_back(n.id);
                else if (n.type == NodeType::_EditorOutput) customOutputNodeIds.push_back(n.id);
            }
        }
        else {
            customName[0] = '\0';
            customColor = ImVec4(0.314f, 0.314f, 0.314f, 1.0f);
            customNodes.clear();
            customLinks.clear();
            customNextId = 1;
            customInputPinNames  = { "In" };
            customOutputPinNames = { "Out" };
            customInputMulti     = { false };
            customOutputMulti    = { false };
            customInputNodeIds.clear();
            customOutputNodeIds.clear();

            ImVec2 ctr = sCustomCanvasCenter;
            Node inNode = CreateBuiltinNode(NodeType::_EditorInput, ImVec2(ctr.x - 180.0f, ctr.y), &CustomAllocId);
            inNode.title = "In"; inNode.pins[0].name = "0";
            customNodes.push_back(inNode);
            customInputNodeIds.push_back(inNode.id);

            Node outNode = CreateBuiltinNode(NodeType::_EditorOutput, ImVec2(ctr.x + 120.0f, ctr.y), &CustomAllocId);
            outNode.title = "Out"; outNode.pins[0].name = "0";
            customNodes.push_back(outNode);
            customOutputNodeIds.push_back(outNode.id);
        }
    }

    static void RenderCustomEditor() {
        bool canSave = customName[0] != '\0';
        bool nameTaken = false;
        if (canSave) {
            for (int i = 0; i < (int)customDefs.size(); i++) {
                if (i == editingDefIndex) continue;
                if (customDefs[i].name == customName) { nameTaken = true; break; }
            }
        }
        if (ui::IconButton(u8"back", ICON_FA_ARROW_LEFT, TR(u8"Back without saving"))) editingCustom = false;
        ImGui::SameLine();
        if (ui::IconButton(u8"reset", ICON_FA_ROTATE_LEFT, TR(u8"Reset simulation"))) ResetSimulation(customNodes);
        ImGui::SameLine();
        ImGui::ColorEdit3(u8"##nodeColor", &customColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SetItemTooltip(u8"%s", TR(u8"Node color"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-(ImGui::CalcTextSize(TR(u8"Save")).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputTextWithHint(u8"##customName", TR(u8"Node name"), customName, sizeof(customName));
        ImGui::SameLine();
        ImGui::BeginDisabled(!canSave || nameTaken);
        if (ui::Button(TR(u8"Save"), ui::Tone::Primary)) {
            if (editingDefIndex >= 0) {
                // Rename all references if name changed
                std::string oldName = customDefs[editingDefIndex].name;
                if (oldName != std::string(customName))
                    RenameCustomDef(editingDefIndex, customName);

                auto& def = customDefs[editingDefIndex];
                def.innerNodes = customNodes;
                def.innerLinks = customLinks;
                def.innerNextId = customNextId;
                def.color = customColor;
                def.inputPinNames  = customInputPinNames;
                def.outputPinNames = customOutputPinNames;
                def.inputPinMulti  = customInputMulti;
                def.outputPinMulti = customOutputMulti;
                // Rebuild pins on existing instances of this custom node
                for (auto& n : nodes) {
                    if (n.type == NodeType::Custom && n.customDefName == def.name) {
                        n.title = def.name;
                        n.customDefIndex = editingDefIndex;
                        // Remove old pins and links, recreate pins
                        for (auto& pin : n.pins)
                            links.erase(std::remove_if(links.begin(), links.end(),
                                [&pin](const Link& l){ return l.startPinId == pin.id || l.endPinId == pin.id; }), links.end());
                        n.pins.clear();
                        for (int gi = 0; gi < (int)def.inputPinNames.size(); gi++)
                            n.pins.push_back({ AllocId(), def.inputPinNames[gi], u8"Bit", false });
                        for (int gi = 0; gi < (int)def.outputPinNames.size(); gi++)
                            n.pins.push_back({ AllocId(), def.outputPinNames[gi], u8"Bit", true });
                    }
                }
            }
            else {
                CustomNodeDef def;
                def.name = customName;
                def.innerNodes = customNodes;
                def.innerLinks = customLinks;
                def.innerNextId = customNextId;
                def.color = customColor;
                def.inputPinNames  = customInputPinNames;
                def.outputPinNames = customOutputPinNames;
                def.inputPinMulti  = customInputMulti;
                def.outputPinMulti = customOutputMulti;
                customDefs.push_back(def);
                // Add to uncategorized palette
                int newIdx = (int)customDefs.size() - 1;
                sUncategorized.push_back({true, NodeType::LogicAND, newIdx, std::string(customName)});
            }
            ResolveAllCustomDefIndices();
            editingCustom = false;
            MarkDirty();
            SaveConfig();
        }
        ImGui::EndDisabled();
        if (nameTaken) ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"%s", TR(u8"Name already in use"));

        ImVec2 avail = ImGui::GetContentRegionAvail();

        // Palette -same categories as main editor
        ImGui::BeginChild(u8"##customPalette", ImVec2(sCustomPaletteWidth, avail.y), true);
        for (auto& cat : paletteCategories) {
            bool hdr = ImGui::CollapsingHeader(cat.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if (hdr) {
                for (auto& e : cat.entries) {
                    if (e.isCustom) {
                        if (e.customDefIndex < 0 || e.customDefIndex >= (int)customDefs.size()) continue;
                        if (e.customDefIndex == editingDefIndex) continue; // skip self
                        ImGui::PushID(&e);
                        PaletteItem(EntryLabel(e), NodeType::Custom, e.customDefIndex, e.customDefName);
                        ImGui::PopID();
                    } else {
                        PaletteItem(EntryLabel(e), e.builtinType);
                    }
                }
            }
        }
        ImGui::EndChild();

        VerticalSplitter("##customPaletteSplit", &sCustomPaletteWidth, 80.0f, 300.0f, avail.y);

        // Canvas
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImGui::BeginChild(u8"##customCanvas", ImVec2(0, avail.y));
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        ImNodes::EditorContextSet(customEditorCtx);
        ImNodes::BeginNodeEditor();

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(u8"##CustomCtxMenu");
        // helper: rebuild pin tracking from current customNodes
        auto SyncPinTracking = []() {
            customInputNodeIds.clear();  customInputPinNames.clear();  customInputMulti.clear();
            customOutputNodeIds.clear(); customOutputPinNames.clear(); customOutputMulti.clear();
            for (auto& n : customNodes) {
                if (n.type == NodeType::_EditorInput) {
                    customInputNodeIds.push_back(n.id);
                    customInputPinNames.push_back(n.title);
                    customInputMulti.push_back(n.multiLink);
                }
                else if (n.type == NodeType::_EditorOutput) {
                    customOutputNodeIds.push_back(n.id);
                    customOutputPinNames.push_back(n.title);
                    customOutputMulti.push_back(n.multiLink);
                }
            }
        };

        if (ImGui::BeginPopup(u8"##CustomCtxMenu")) {
            ImVec2 mp = ImGui::GetMousePosOnOpeningCurrentPopup();
            {
                bool hasSelNodes = ImNodes::NumSelectedNodes() > 0;
                bool hasSelLinks = ImNodes::NumSelectedLinks() > 0;
                bool hasSel = hasSelNodes || hasSelLinks;
                bool hasClip = !sClipboardNodes.empty();
                if (ImGui::MenuItem(TR(u8"Copy"), "Ctrl+C", false, hasSelNodes))
                    CopySelected(customNodes, customLinks);
                if (ImGui::MenuItem(TR(u8"Paste"), "Ctrl+V", false, hasClip))
                    PasteClipboard(customNodes, customLinks, &CustomAllocId, mp);
                if (ImGui::MenuItem(TR(u8"Duplicate"), "Ctrl+D", false, hasSelNodes))
                    DuplicateSelected(customNodes, customLinks, &CustomAllocId);
                if (ImGui::MenuItem(TR(u8"Remove"), "Del", false, hasSel))
                    DeleteSelected(customNodes, customLinks, false);
                ImGui::Separator();
            }

            if (ImGui::MenuItem(TR(u8"Add Input"))) {
                Node n = CreateBuiltinNode(NodeType::_EditorInput, mp, &CustomAllocId);
                n.title = "In";
                customNodes.push_back(n);
                customInputNodeIds.push_back(n.id);
                customInputPinNames.push_back("In");
                customInputMulti.push_back(false);
            }
            if (ImGui::MenuItem(TR(u8"Add Output"))) {
                Node n = CreateBuiltinNode(NodeType::_EditorOutput, mp, &CustomAllocId);
                n.title = "Out";
                customNodes.push_back(n);
                customOutputNodeIds.push_back(n.id);
                customOutputPinNames.push_back("Out");
                customOutputMulti.push_back(false);
            }

            ImGui::Separator();

            if (ImGui::BeginMenu(TR(u8"Advanced Logic"))) {
                if (ImGui::MenuItem(u8"CUBE MODEL TRANSPARENCY")) customNodes.push_back(CreateBuiltinNode(NodeType::CubeModelTransparency, mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"LEVER"))                   customNodes.push_back(CreateBuiltinNode(NodeType::Button,                mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"TRIGGER CUBE"))            customNodes.push_back(CreateBuiltinNode(NodeType::TriggerCube,           mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"PULSE CUBE"))              customNodes.push_back(CreateBuiltinNode(NodeType::PulseCube,             mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"PRESSURE PLATE"))          customNodes.push_back(CreateBuiltinNode(NodeType::PressurePlate,         mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"TARGET CUBE"))             customNodes.push_back(CreateBuiltinNode(NodeType::TargetCube,            mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"COUNTING CUBE"))           customNodes.push_back(CreateBuiltinNode(NodeType::CountingCube,          mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"RANDOM CUBE"))             customNodes.push_back(CreateBuiltinNode(NodeType::RandomCube,            mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"CUBE MODEL HIDER"))        customNodes.push_back(CreateBuiltinNode(NodeType::CubeModelHider,        mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"AND CUBE"))                customNodes.push_back(CreateBuiltinNode(NodeType::LogicAND,              mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"NEGATE CUBE"))             customNodes.push_back(CreateBuiltinNode(NodeType::LogicNOT,              mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"DELAY CUBE"))              customNodes.push_back(CreateBuiltinNode(NodeType::DelayCube,             mp, &CustomAllocId));
                if (ImGui::MenuItem(u8"TOGGLE CUBE"))             customNodes.push_back(CreateBuiltinNode(NodeType::LogicTOGGLE,           mp, &CustomAllocId));
                ImGui::EndMenu();
            }
            bool hasOtherCustom = false;
            for (int i = 0; i < (int)customDefs.size(); i++)
                if (i != editingDefIndex) { hasOtherCustom = true; break; }
            if (hasOtherCustom && ImGui::BeginMenu(TR(u8"Custom"))) {
                for (int i = 0; i < (int)customDefs.size(); i++) {
                    if (i == editingDefIndex) continue;
                    if (ImGui::MenuItem(customDefs[i].name.c_str()))
                        customNodes.push_back(CreateCustomNode(i, mp, &CustomAllocId));
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        sCustomCanvasCenter = ImVec2(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);

        SimulateBlueprintStep(customNodes, customLinks);
        RenderNodes(customNodes, customLinks, &CustomAllocId);
        ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        HandleLinks(customLinks, &CustomAllocId, &customNodes);
        HandleDeletion(customNodes, customLinks, false);
        HandleClipboardShortcuts(customNodes, customLinks, &CustomAllocId, false);
        SyncPinTracking();
        HandleDrop(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                   customNodes, &CustomAllocId, false);

        ImGui::EndChild();
    }

    // -------------------------------------------------------
    // Main editor
    // -------------------------------------------------------
    static void CircuitToolbar() {
        if (ui::IconButton(u8"node", ICON_FA_PUZZLE_PIECE, TR(u8"Create node"))) EnterCustomEditor(-1);
        ImGui::SameLine();
        if (ui::IconButton(u8"category", ICON_FA_FOLDER_PLUS, TR(u8"Add category"))) {
            paletteCategories.push_back({"New", {}});
            MarkDirty();
            SaveConfig();
        }
        ImGui::SameLine();
        if (ui::IconButton(u8"reset", ICON_FA_ROTATE_LEFT, TR(u8"Reset simulation"))) ResetSimulation(nodes);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        const Explorer::ObjectEntry* entry = Explorer::FindById(activeCircuitId);
        LogicCircuit* lc = FindCircuit(activeCircuitId);
        std::string name = entry && !entry->name.empty() ? entry->name : lc ? lc->name : activeCircuitId;
        ImGui::TextDisabled(u8"%s  ·  %d %s  ·  %d %s", name.c_str(), (int)nodes.size(), TR(u8"nodes"), (int)links.size(), TR(u8"links"));
    }

    static void SyncCircuit() {
        const std::set<std::string>& ids = Explorer::GetSelectedIds();
        std::string id = ids.size() == 1 && logicCircuitGroupIds.count(*ids.begin()) ? *ids.begin() : std::string();
        if (id != activeCircuitId) LoadCircuit(id);
    }

    static void RenderMainEditor() {
        SyncCircuit();
        if (activeCircuitId.empty()) {
            ImGui::BeginDisabled(Explorer::RootGroupId() < 0);
            if (ui::EmptyState(TR(u8"Select a logic circuit in the hierarchy to edit it."), TR(u8"New Logic Circuit")))
                CreateLogicCircuit(Explorer::RootGroupId());
            ImGui::EndDisabled();
            AutoSave();
            return;
        }
        InitPalette();
        CircuitToolbar();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float maxPaletteW = avail.x * 0.5f;
        if (sPaletteWidth < 100.0f) sPaletteWidth = 100.0f;
        if (sPaletteWidth > maxPaletteW) sPaletteWidth = maxPaletteW;

        // Palette
        struct PaletteDragData { int catIdx; int entryIdx; bool isBuiltin; };

        ImGui::BeginChild(u8"##palette", ImVec2(sPaletteWidth, avail.y), true);

        // --- Categories ---
        for (int ci = 0; ci < (int)paletteCategories.size(); ci++) {
            auto& cat = paletteCategories[ci];
            ImGui::PushID(ci);

            bool hdr = ImGui::CollapsingHeader(cat.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

            // Category drag reorder (not builtin)
            if (!cat.isBuiltin) {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                    PaletteDragData dd = { ci, -1, false };
                    ImGui::SetDragDropPayload("PAL_CAT", &dd, sizeof(dd));
                    ImGui::TextUnformatted(cat.name.c_str());
                    ImGui::EndDragDropSource();
                }
            }
            // Category drop target: accept other categories, accept non-builtin entries
            if (!cat.isBuiltin && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PAL_CAT")) {
                    auto* src = (PaletteDragData*)p->Data;
                    if (src->catIdx != ci) {
                        auto tmp = paletteCategories[src->catIdx];
                        paletteCategories.erase(paletteCategories.begin() + src->catIdx);
                        int dst = ci > src->catIdx ? ci - 1 : ci;
                        paletteCategories.insert(paletteCategories.begin() + dst, tmp);
                        MarkDirty(); SaveConfig();
                    }
                }
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PAL_ENTRY")) {
                    auto* src = (PaletteDragData*)p->Data;
                    if (!src->isBuiltin && src->entryIdx >= 0) {
                        auto entry = paletteCategories[src->catIdx].entries[src->entryIdx];
                        paletteCategories[src->catIdx].entries.erase(
                            paletteCategories[src->catIdx].entries.begin() + src->entryIdx);
                        int targetCi = ci > src->catIdx ? ci : ci;  // index stable since we only remove from other cat
                        paletteCategories[targetCi].entries.push_back(entry);
                        MarkDirty(); SaveConfig();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Right-click on category header
            if (!cat.isBuiltin && ImGui::BeginPopupContextItem("##catctx")) {
                char catBuf[64]; strncpy(catBuf, cat.name.c_str(), 63); catBuf[63] = 0;
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputText(TR(u8"Name"), catBuf, 64)) { cat.name = catBuf; MarkDirty(); SaveConfig(); }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
                if (ImGui::MenuItem(TR(u8"Delete category")))
                    { paletteCategories.erase(paletteCategories.begin() + ci); MarkDirty(); SaveConfig(); ImGui::PopStyleColor(); ImGui::EndPopup(); ImGui::PopID(); break; }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }

            if (hdr) {
                for (int ei = 0; ei < (int)cat.entries.size(); ei++) {
                    auto& e = cat.entries[ei];
                    ImGui::PushID(ei);

                    // Drag grip handle
                    float h = ImGui::GetFrameHeight();
                    ImVec2 gripPos = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##grip", ImVec2(10.0f, h));
                    {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 gc = ImGui::IsItemHovered() ? IM_COL32(200,200,200,255) : IM_COL32(100,100,100,180);
                        float cx = gripPos.x + 4.0f, cy = gripPos.y + h * 0.5f;
                        for (float dy : {-3.0f, 0.0f, 3.0f})
                            dl->AddCircleFilled(ImVec2(cx, cy + dy), 1.5f, gc);
                    }
                    // Drag source: builtin entries only within their own category
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                        PaletteDragData dd = { ci, ei, cat.isBuiltin };
                        ImGui::SetDragDropPayload("PAL_ENTRY", &dd, sizeof(dd));
                        ImGui::TextUnformatted(EntryLabel(e));
                        ImGui::EndDragDropSource();
                    }

                    ImGui::SameLine(0, 2);

                    // Palette item (for canvas drag)
                    if (e.isCustom) {
                        if (e.customDefIndex < 0 || e.customDefIndex >= (int)customDefs.size()) { ImGui::PopID(); continue; }
                        PaletteItem(EntryLabel(e), NodeType::Custom, e.customDefIndex, e.customDefName);
                        if (ImGui::BeginPopupContextItem("##ectx")) {
                            if (ImGui::MenuItem(TR(u8"Edit")))
                                EnterCustomEditor(e.customDefIndex);
                            ImGui::Separator();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
                            if (ImGui::MenuItem(TR(u8"Delete"))) {
                                // Permanently delete custom def
                                int defIdx = e.customDefIndex;
                                customDefs.erase(customDefs.begin() + defIdx);
                                // Remove all palette entries referencing this def
                                for (auto& pc : paletteCategories)
                                    for (int j = (int)pc.entries.size()-1; j >= 0; j--)
                                        if (pc.entries[j].isCustom && pc.entries[j].customDefName == e.customDefName)
                                            pc.entries.erase(pc.entries.begin() + j);
                                ResolveAllCustomDefIndices();
                                MarkDirty(); SaveConfig();
                                ImGui::PopStyleColor(); ImGui::EndPopup(); ImGui::PopID(); goto next_cat;
                            }
                            ImGui::PopStyleColor();
                            ImGui::EndPopup();
                        }
                    } else {
                        PaletteItem(EntryLabel(e), e.builtinType);
                    }

                    // Drop target: reorder within same category; accept non-builtin cross-category
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PAL_ENTRY")) {
                            auto* src = (PaletteDragData*)p->Data;
                            if (src->entryIdx >= 0) {
                                if (src->catIdx == ci) {
                                    // Same category reorder
                                    if (src->entryIdx != ei) {
                                        auto tmp = cat.entries[src->entryIdx];
                                        cat.entries.erase(cat.entries.begin() + src->entryIdx);
                                        int dst = ei > src->entryIdx ? ei - 1 : ei;
                                        cat.entries.insert(cat.entries.begin() + dst, tmp);
                                        MarkDirty(); SaveConfig();
                                    }
                                } else if (!src->isBuiltin && !cat.isBuiltin) {
                                    // Cross-category move (non-builtin only, not into builtin)
                                    auto entry = paletteCategories[src->catIdx].entries[src->entryIdx];
                                    paletteCategories[src->catIdx].entries.erase(
                                        paletteCategories[src->catIdx].entries.begin() + src->entryIdx);
                                    cat.entries.insert(cat.entries.begin() + ei, entry);
                                    MarkDirty(); SaveConfig();
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::PopID();
                }
            }
            next_cat:
            ImGui::PopID();
        }

        // --- Uncategorized items (outside any category) ---
        for (int ui = 0; ui < (int)sUncategorized.size(); ui++) {
            auto& e = sUncategorized[ui];
            ImGui::PushID(10000 + ui);
            if (e.isCustom) {
                if (e.customDefIndex >= 0 && e.customDefIndex < (int)customDefs.size())
                    PaletteItem(EntryLabel(e), NodeType::Custom, e.customDefIndex, e.customDefName);
            } else {
                PaletteItem(EntryLabel(e), e.builtinType);
            }
            // Right-click to delete custom uncategorized item
            if (e.isCustom && ImGui::BeginPopupContextItem("##uctx")) {
                if (ImGui::MenuItem(TR(u8"Edit")))
                    EnterCustomEditor(e.customDefIndex);
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
                if (ImGui::MenuItem(TR(u8"Delete"))) {
                    int defIdx = e.customDefIndex;
                    std::string defName = e.customDefName;
                    customDefs.erase(customDefs.begin() + defIdx);
                    for (auto& pc : paletteCategories)
                        for (int j = (int)pc.entries.size()-1; j >= 0; j--)
                            if (pc.entries[j].isCustom && pc.entries[j].customDefName == defName)
                                pc.entries.erase(pc.entries.begin() + j);
                    sUncategorized.erase(sUncategorized.begin() + ui);
                    ResolveAllCustomDefIndices();
                    MarkDirty(); SaveConfig();
                    ImGui::PopStyleColor(); ImGui::EndPopup(); ImGui::PopID(); goto end_palette;
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            // Drop target for reordering uncategorized
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PAL_ENTRY")) {
                    auto* src = (PaletteDragData*)p->Data;
                    if (src->entryIdx >= 0 && !src->isBuiltin) {
                        auto entry = paletteCategories[src->catIdx].entries[src->entryIdx];
                        paletteCategories[src->catIdx].entries.erase(
                            paletteCategories[src->catIdx].entries.begin() + src->entryIdx);
                        sUncategorized.insert(sUncategorized.begin() + ui, entry);
                        MarkDirty(); SaveConfig();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }

        end_palette:
        ImGui::EndChild();

        // Resizable splitter
        VerticalSplitter("##paletteSplit", &sPaletteWidth, 100.0f, maxPaletteW, avail.y);

        // Canvas
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImGui::BeginChild(u8"##mainCanvas", ImVec2(0, avail.y));
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        ImNodes::EditorContextSet(editorCtx);
        ImNodes::BeginNodeEditor();

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup(u8"##BlueprintContextMenu");
        if (ImGui::BeginPopup(u8"##BlueprintContextMenu")) {
            ImVec2 mp = ImGui::GetMousePosOnOpeningCurrentPopup();
            bool hasSelNodes = ImNodes::NumSelectedNodes() > 0;
            bool hasSelLinks = ImNodes::NumSelectedLinks() > 0;
            bool hasSel = hasSelNodes || hasSelLinks;
            bool hasClip = !sClipboardNodes.empty();

            if (ImGui::MenuItem(TR(u8"Copy"), "Ctrl+C", false, hasSelNodes))
                CopySelected(nodes, links);
            if (ImGui::MenuItem(TR(u8"Paste"), "Ctrl+V", false, hasClip))
                PasteClipboard(nodes, links, &AllocId, mp);
            if (ImGui::MenuItem(TR(u8"Duplicate"), "Ctrl+D", false, hasSelNodes))
                DuplicateSelected(nodes, links, &AllocId);
            if (ImGui::MenuItem(TR(u8"Remove"), "Del", false, hasSel))
                DeleteSelected(nodes, links, false);

            ImGui::Separator();

            if (ImGui::BeginMenu(TR(u8"Advanced Logic"))) {
                if (ImGui::MenuItem(u8"CUBE MODEL TRANSPARENCY")) nodes.push_back(CreateBuiltinNode(NodeType::CubeModelTransparency, mp, &AllocId));
                if (ImGui::MenuItem(u8"LEVER"))                   nodes.push_back(CreateBuiltinNode(NodeType::Button,                mp, &AllocId));
                if (ImGui::MenuItem(u8"TRIGGER CUBE"))            nodes.push_back(CreateBuiltinNode(NodeType::TriggerCube,           mp, &AllocId));
                if (ImGui::MenuItem(u8"PULSE CUBE"))              nodes.push_back(CreateBuiltinNode(NodeType::PulseCube,             mp, &AllocId));
                if (ImGui::MenuItem(u8"PRESSURE PLATE"))          nodes.push_back(CreateBuiltinNode(NodeType::PressurePlate,         mp, &AllocId));
                if (ImGui::MenuItem(u8"TARGET CUBE"))             nodes.push_back(CreateBuiltinNode(NodeType::TargetCube,            mp, &AllocId));
                if (ImGui::MenuItem(u8"COUNTING CUBE"))           nodes.push_back(CreateBuiltinNode(NodeType::CountingCube,          mp, &AllocId));
                if (ImGui::MenuItem(u8"RANDOM CUBE"))             nodes.push_back(CreateBuiltinNode(NodeType::RandomCube,            mp, &AllocId));
                if (ImGui::MenuItem(u8"CUBE MODEL HIDER"))        nodes.push_back(CreateBuiltinNode(NodeType::CubeModelHider,        mp, &AllocId));
                if (ImGui::MenuItem(u8"AND CUBE"))                nodes.push_back(CreateBuiltinNode(NodeType::LogicAND,              mp, &AllocId));
                if (ImGui::MenuItem(u8"NEGATE CUBE"))             nodes.push_back(CreateBuiltinNode(NodeType::LogicNOT,              mp, &AllocId));
                if (ImGui::MenuItem(u8"DELAY CUBE"))              nodes.push_back(CreateBuiltinNode(NodeType::DelayCube,             mp, &AllocId));
                if (ImGui::MenuItem(u8"TOGGLE CUBE"))             nodes.push_back(CreateBuiltinNode(NodeType::LogicTOGGLE,           mp, &AllocId));
                ImGui::EndMenu();
            }
            if (!customDefs.empty() && ImGui::BeginMenu(TR(u8"Custom"))) {
                for (int i = 0; i < (int)customDefs.size(); i++)
                    if (ImGui::MenuItem(customDefs[i].name.c_str()))
                        nodes.push_back(CreateCustomNode(i, mp));
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        SimulateBlueprintStep(nodes, links);
        RenderNodes(nodes, links);
        ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();

        HandleLinks(links, &AllocId, &nodes);
        HandleDeletion(nodes, links, false);
        HandleClipboardShortcuts(nodes, links, &AllocId, false);
        HandleDrop(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                   nodes, &AllocId, true);

        MarkDirty();

        ImGui::EndChild();
        AutoSave();
    }

    // -------------------------------------------------------
    // Public API
    // -------------------------------------------------------
    static int sLogicCircuitCounter = 0;
    static bool sPendingLogicCircuit = false;
    static int sFocusPanel = 0;
    static std::set<std::string> sGroupsBefore;

    static std::set<std::string> Groups() {
        std::set<std::string> ids;
        for (const Explorer::ObjectEntry& entry : Explorer::GetAllObjects())
            if (entry.type.find("Group") != std::string::npos) ids.insert(entry.id);
        return ids;
    }

    void CreateLogicCircuit(int parentGroupId) {
        sLogicCircuitCounter++;
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "explorer_create_group|%d|0|10|0|0|0|0|1|1|1|0", parentGroupId);
        menu::SendCommand(cmd);
        sGroupsBefore = Groups();
        sPendingLogicCircuit = true;
        menu::ShowPanel(u8"###Blueprints");
    }

    static void ResolvePendingLogicCircuit() {
        if (!sPendingLogicCircuit) return;
        for (const std::string& id : Groups()) {
            if (sGroupsBefore.count(id) || IsLogicCircuit(id)) continue;
            SetLogicCircuit(id, true);
            std::string name = "Logic Circuit " + std::to_string(sLogicCircuitCounter);
            menu::SendCommand(("explorer_rename|" + id + "|" + name).c_str());
            if (LogicCircuit* lc = FindCircuit(id)) lc->name = name;
            Explorer::Select(id);
            sFocusPanel = 3;
            sPendingLogicCircuit = false;
            return;
        }
    }

    bool IsLogicCircuit(const std::string& groupId) {
        return logicCircuitGroupIds.count(groupId) > 0;
    }

    void SetLogicCircuit(const std::string& groupId, bool isCircuit) {
        if (isCircuit) {
            logicCircuitGroupIds.insert(groupId);
            // Create circuit data if it doesn't exist
            if (!FindCircuit(groupId)) {
                LogicCircuit lc;
                lc.groupId = groupId;
                const Explorer::ObjectEntry* obj = Explorer::FindById(groupId);
                lc.name = obj ? obj->name : ("Circuit " + groupId);
                circuits.push_back(lc);
            }
        } else {
            logicCircuitGroupIds.erase(groupId);
            // If this was the active circuit, deselect it
            if (activeCircuitId == groupId)
                LoadCircuit("");
        }
        MarkDirty();
        SaveConfig();
    }

    // -------------------------------------------------------
    // Main Render
    // -------------------------------------------------------
    bool g_wantBlockViewport = false;

    void Render() {
        ResolvePendingLogicCircuit();
        if (sFocusPanel > 0) {
            ImGui::SetNextWindowFocus();
            sFocusPanel--;
        }
        if (!ui::BeginPanel(u8"Blueprints", ICON_FA_NETWORK_WIRED, TR(u8"Logic"), nullptr))
        {
            g_wantBlockViewport = false;
            ui::EndPanel();
            return;
        }

        g_wantBlockViewport = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
                           || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        static bool imnodesInitialized = false;
        if (!imnodesInitialized) {
            ImNodes::CreateContext();
            imnodesInitialized = true;
        }
        if (!editorCtx) {
            editorCtx = ImNodes::EditorContextCreate();
        }
        static bool styleApplied = false;
        if (!styleApplied) {
            styleApplied = true;
            ImNodesStyle& style = ImNodes::GetStyle();
            style.NodeCornerRounding = 6.0f;
            style.PinCircleRadius = 5.0f;
            style.LinkThickness = 3.0f;
            style.NodePadding = ImVec2(10.0f, 6.0f);
            style.Flags |= ImNodesStyleFlags_GridLines;
        }

        if (editingCustom)
            RenderCustomEditor();
        else
            RenderMainEditor();

        // Draw drag overlay on top of everything
        DrawDragOverlay();

        ui::EndPanel();
    }
}
