#include "stdafx.h"
#include "MenuHelpers.h"
#include "bridge.h"
#include "Console.h"

namespace menu {

    bool typing = false;

    void SendCommand(const char* cmd)
    {
        bridge::ToGame(cmd);
        DebugLog("[menu] Command sent: %s\n", cmd);
        AppLog::AddLog(AppLog::LogType::Outgoing, cmd);
    }

}
