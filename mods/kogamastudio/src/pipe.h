#pragma once
#include <string>

//typedef void(*CommandCallback)(const char* command, const char* )

namespace pipe {
	extern bool cursorVisible;
	extern bool gameInitialized;
	extern bool openMenu;
	extern bool isFullscreen;

	extern bool  isPasteBuilding;
	extern int   pastePlacedCubes;
	extern int   pasteTotalCubes;
	extern float pasteEtaSeconds;

	extern bool  isWorldLoading;
	extern int   worldLoadPlaced;
	extern int   worldLoadTotal;
	extern float worldLoadEta;

	bool ProcessPending();
	void ProcessCommand(const std::string& cmd);
}