#include "stdafx.h"
#include <algorithm>
#include "Inventory.h"
#include "MenuHelpers.h"
#include "UILocale.h"
#include "Properties.h"
#include "IconsFontAwesome6.h"
#include "ui.h"
#include <cctype>

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

    namespace {
        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return text;
        }

        std::string LabelOf(const InventoryItem& item) { return item.name.empty() ? u8"ID " + item.id : item.name; }

        bool Matches(const InventoryItem& item, const std::string& query) {
            return query.empty() || Lower(item.name).find(query) != std::string::npos || item.id.find(query) != std::string::npos;
        }

        void Select(const InventoryItem& item) {
            selectedId = item.id;
            selectedItemCopy = item;
            hasSelection = true;
            Properties::targetObjectId = u8"-1";
        }

        void Toolbar() {
            float refresh = ImGui::CalcTextSize(ICON_FA_ROTATE).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
            float half = (ImGui::GetContentRegionAvail().x - refresh - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::SetNextItemWidth(half);
            std::string preview = activeCategoryName.empty() ? TR(u8"No items") : activeCategoryName;
            if (ImGui::BeginCombo(u8"##category", preview.c_str())) {
                for (const auto& category : itemsByCategory) {
                    bool selected = category.first == activeCategoryName;
                    std::string label = category.first + u8"  (" + std::to_string(category.second.size()) + u8")##" + category.first;
                    if (ImGui::Selectable(label.c_str(), selected)) activeCategoryName = category.first;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-refresh);
            ImGui::InputTextWithHint(u8"##search", TR(u8"Search"), filterBuf, sizeof(filterBuf));
            if (!typing) typing = ImGui::IsItemActive();
            ImGui::SameLine();
            if (ui::IconButton(u8"refresh", ICON_FA_ROTATE, TR(u8"Refresh"))) {
                Clear();
                SendCommand(u8"inventory_refresh");
            }
        }

        void ItemRow(const InventoryItem& item) {
            ImGui::PushID(item.id.c_str());
            if (ImGui::Selectable(LabelOf(item).c_str(), hasSelection && selectedId == item.id)) Select(item);
            ImGui::SetItemTooltip(u8"%s\nID %s", item.categoryName.c_str(), item.id.c_str());
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(u8"INVENTORY_ITEM", item.id.c_str(), item.id.size() + 1);
                ImGui::Text(u8"%s  %s", ICON_FA_CUBE, LabelOf(item).c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem(u8"##item")) {
                if (ImGui::MenuItem(TR(u8"Place in world"))) SendCommand((u8"world_place_item|" + item.id).c_str());
                ImGui::Separator();
                if (ImGui::MenuItem(TR(u8"Remove from Assets"))) SendCommand((u8"inventory_remove|" + item.id).c_str());
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }

    void Render()
    {
        if (ui::BeginPanel(u8"Inventory", ICON_FA_CUBES, TR(u8"Assets"), nullptr)) {
            if (!itemsByCategory.empty() && itemsByCategory.find(activeCategoryName) == itemsByCategory.end())
                activeCategoryName = itemsByCategory.begin()->first;
            Toolbar();
            ImGui::BeginChild(u8"##items");
            if (itemsByCategory.empty()) ui::EmptyState(TR(u8"No items"));
            else {
                std::string query = Lower(filterBuf);
                std::vector<const InventoryItem*> shown;
                for (const InventoryItem& item : itemsByCategory.at(activeCategoryName))
                    if (Matches(item, query)) shown.push_back(&item);
                if (shown.empty()) ui::EmptyState(TR(u8"Nothing matches the search"));
                ImGuiListClipper clipper;
                clipper.Begin((int)shown.size());
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) ItemRow(*shown[i]);
            }
            ImGui::EndChild();
        }
        ui::EndPanel();
    }
}
