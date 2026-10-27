#include "stdafx.h"
#include <algorithm>
#include "Inventory.h"
#include "MenuHelpers.h"
#include "UILocale.h"
#include "Properties.h"

using namespace menu;

namespace Inventory {

    static std::map<std::string, std::vector<InventoryItem>> itemsByCategory;
    static char filterBuf[128] = "";
    static std::string selectedId;
    static InventoryItem selectedItemCopy;
    static bool hasSelection = false;
    static std::string activeCategoryName;

    void Clear()
    {
        itemsByCategory.clear();
        hasSelection = false;
        selectedId.clear();
        activeCategoryName.clear();
    }

    void ClearSelection()
    {
        hasSelection = false;
        selectedId.clear();
    }

    void AddItem(const InventoryItem& item)
    {
        if (item.categoryName.empty()) return;
        auto& vec = itemsByCategory[item.categoryName];
        vec.push_back(item);
        std::sort(vec.begin(), vec.end(), [](const InventoryItem& a, const InventoryItem& b) { return a.slotPosition < b.slotPosition; });
    }

    const std::map<std::string, std::vector<InventoryItem>>& GetCategories() { return itemsByCategory; }

    const InventoryItem* FindById(const std::string& id)
    {
        for (const auto& catPair : itemsByCategory)
            for (const auto& item : catPair.second)
                if (item.id == id)
                    return &item;
        return nullptr;
    }

    bool HasSelected()
    {
        return hasSelection;
    }

    const InventoryItem& GetSelected()
    {
        return selectedItemCopy;
    }

    static int CountTotal()
    {
        int n = 0;
        for (const auto& kv : itemsByCategory) n += (int)kv.second.size();
        return n;
    }

    void Render()
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin((std::string(TR(u8"Inventory")) + u8"###Inventory").c_str(), nullptr, flags))
        {
            if (ImGui::Button(TR(u8"Refresh")))
            {
                Clear();
                SendCommand(u8"inventory_refresh");
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText(u8"##inventory_filter", filterBuf, sizeof(filterBuf));
            if (!typing) typing = ImGui::IsItemActive();

            ImGui::Separator();
            ImGui::TextDisabled("%s: %d", TR(u8"Items"), CountTotal());

            std::string filter = filterBuf;
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

            if (itemsByCategory.empty())
            {
                ImGui::TextDisabled(TR(u8"No items"));
            }
            else
            {
                // Ensure activeCategoryName is valid
                if (itemsByCategory.find(activeCategoryName) == itemsByCategory.end())
                    activeCategoryName = itemsByCategory.begin()->first;

                const auto& activeItems = itemsByCategory.at(activeCategoryName);
                int visibleCount = (int)std::count_if(activeItems.begin(), activeItems.end(), [&](const InventoryItem& i) {
                    if (filter.empty()) return true;
                    std::string n = i.name; std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    return n.find(filter) != std::string::npos || i.id.find(filter) != std::string::npos;
                });

                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo(u8"##cat", (activeCategoryName + " (" + std::to_string((int)itemsByCategory.at(activeCategoryName).size()) + ")").c_str()))
                {
                    for (const auto& kv : itemsByCategory)
                    {
                        bool selected = (kv.first == activeCategoryName);
                        if (ImGui::Selectable((kv.first + " (" + std::to_string((int)kv.second.size()) + ")##" + kv.first).c_str(), selected)) activeCategoryName = kv.first;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::BeginChild(u8"##inv_list", ImVec2(0, 0), false);
                for (const auto& item : activeItems)
                {
                    if (!filter.empty()) { std::string n = item.name; std::transform(n.begin(), n.end(), n.begin(), ::tolower); if (n.find(filter) == std::string::npos && item.id.find(filter) == std::string::npos) continue; }
                    std::string label = (item.name.empty() ? (u8"[ID " + item.id + u8"]") : item.name) + u8"##" + item.id;
                    if (ImGui::Selectable(label.c_str(), selectedId == item.id)) { selectedId = item.id; selectedItemCopy = item; hasSelection = true; Properties::targetObjectId = u8"-1"; }
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        ImGui::SetDragDropPayload(u8"INVENTORY_ITEM", item.id.c_str(), item.id.size() + 1);
                        ImGui::Text(u8"%s", (item.name.empty() ? (u8"[ID " + item.id + u8"]") : item.name).c_str());
                        ImGui::EndDragDropSource();
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

}
