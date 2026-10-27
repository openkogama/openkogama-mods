#pragma once

namespace AppLog {
    enum class LogType { Incoming, Outgoing, Warning, Error };
    void AddLog(LogType type, const char* msg);
    void Render();
}
