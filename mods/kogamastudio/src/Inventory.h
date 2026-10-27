#pragma once
#include <string>
#include <vector>
#include <map>

namespace Inventory {
    struct InventoryItem {
        std::string id;
        std::string name;
        std::string typeId;
        std::string categoryId;
        std::string categoryName;
        bool resellable = false;
        int priceGold = 0;
        bool purchased = false;
        std::string authorProfileId;
        int slotPosition = 0;
        std::string description;
    };

    void Clear();
    void ClearSelection();
    void AddItem(const InventoryItem& item);
    bool HasSelected();
    const InventoryItem& GetSelected();
    const InventoryItem* FindById(const std::string& id);
    const std::map<std::string, std::vector<InventoryItem>>& GetCategories();
    void Render();
}
