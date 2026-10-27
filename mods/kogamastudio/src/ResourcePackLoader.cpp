#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include "stdafx.h"
#define STB_IMAGE_IMPLEMENTATION

#include <d3d11.h>
#include "imgui/imgui_impl_dx11.h"

#include <filesystem>
#include <fstream>
#include "json.hpp"
#include "ResourcePackLoader.h"
#include "stb_image.h"


using json = nlohmann::json;
namespace fs = std::filesystem;

ImTextureID LoadTextureFromFile(const char* path)
{
	if (!path || path[0] == '\0') return (ImTextureID)0;

	int width, height, channels;
	unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
	if (!data) return (ImTextureID)0;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = data;
	initData.SysMemPitch = width * 4;

	ID3D11Texture2D* texture = nullptr;
	hooks_dx11::gDevice->CreateTexture2D(&desc, &initData, &texture);

	ID3D11ShaderResourceView* srv = nullptr;
	hooks_dx11::gDevice->CreateShaderResourceView(texture, nullptr, &srv);

	stbi_image_free(data);
	texture->Release();

	return (ImTextureID)srv;
}

std::vector<Pack> LoadPacks()
{
	std::vector<Pack> packs;

	char* buffer = nullptr;
	size_t len;
	_dupenv_s(&buffer, &len, "APPDATA");

	std::string base = std::string(buffer) + "\\..\\Local\\KogamaStudio\\ResourcePacks";

	free(buffer);

	for (const auto& entry : fs::directory_iterator(base))
	{
		if (!fs::is_directory(entry)) continue;

		std::string cfg = entry.path().string() + "\\config.json";
		if (!fs::exists(cfg)) continue;

		try
		{
			json j = json::parse(std::ifstream(cfg));

			std::string iconPath = entry.path().string() + "\\icon.png";

			Pack p;
			p.name = j["name"];
			p.author = j["author"];
			p.description = j["description"];
			p.path = entry.path().string();
			p.folder = entry.path().filename().string();
			p.iconPath = fs::exists(iconPath) ? iconPath : "";
			p.iconTexture = LoadTextureFromFile(p.iconPath.c_str());
			packs.push_back(p);
		}
		catch (...) {}
	}
	return packs;
}