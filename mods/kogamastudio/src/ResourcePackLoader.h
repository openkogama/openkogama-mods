#pragma once
#include <string>
#include <vector>
#include <imgui/imgui.h>

struct Pack {
    std::string name, author, description, path, folder, iconPath;
    ImTextureID iconTexture = (ImTextureID)0;
};


std::vector<Pack> LoadPacks();
ImTextureID LoadTextureFromFile(const char* path);
