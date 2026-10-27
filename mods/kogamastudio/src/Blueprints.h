#pragma once
#include "stdafx.h"
#include <string>

namespace Blueprints {
	void Render();
	extern bool g_wantBlockViewport;

	// Create a logic circuit group in the Explorer and register it
	void CreateLogicCircuit(int parentGroupId);
	// Check if an Explorer group ID is a logic circuit
	bool IsLogicCircuit(const std::string& groupId);
	// Mark or unmark an Explorer group as a logic circuit
	void SetLogicCircuit(const std::string& groupId, bool isCircuit);
}
