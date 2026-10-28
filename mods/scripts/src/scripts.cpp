#include "okmod.h"
#include "api.h"
#include "game.h"
#include "images.h"
#include "remote.h"
#include "render.h"
#include "runtime.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

namespace {
    using Clock = std::chrono::steady_clock;
    constexpr long LogLimit = 4L << 20;

    const OkMod* host = nullptr;
    std::FILE* log = nullptr;
    long written = 0;
    std::mutex logLock;
    bool started = false;
    bool loading = false;
    Clock::time_point last;
    Clock::time_point watched;
    Clock::time_point loaded;
    std::filesystem::path server;

    std::filesystem::path GameFolder() {
        wchar_t path[MAX_PATH] = L"";
        DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(std::wstring(path, length)).parent_path();
    }

    void Write(const std::string& line) {
        std::lock_guard<std::mutex> guard(logLock);
        if (host) host->log(OK_LOG_INFO, line.c_str());
        if (!log || written > LogLimit) return;
        written += std::fprintf(log, "%s\n", line.c_str());
        if (written > LogLimit) std::fputs("the log is full\n", log);
        std::fflush(log);
    }

    void OnFrame(ok_call*, void*) {
        Clock::time_point now = Clock::now();
        if (!started) {
            if (!render::Start()) return;
            started = true;
            last = watched = now;
            runtime::Open({{GameFolder() / "scripts", ""}, {server, "server:"}});
        }
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (now - watched >= std::chrono::seconds(1)) {
            watched = now;
            runtime::Watch();
        }
        if (loading && now - loaded >= std::chrono::milliseconds(500)) {
            loading = false;
            remote::Refresh();
            runtime::Emit("world");
            runtime::Keep("world");
        }
        for (const remote::Message& message : remote::Take())
            runtime::Emit("message", {runtime::Arg::Of(message.name), runtime::Arg::Of(message.data)});
        runtime::Tick(dt);
        api::Maintain(dt);
        api::HoldCursor();
        if (api::Blocking()) game::SuppressInput();
        images::Sweep();
        render::Frame();
    }

    void OnLateFrame(ok_call*, void*) {
        if (started) api::HoldCursor();
    }

    void OnWorld(ok_call*, void*) {
        api::Unblock();
        loading = true;
        loaded = Clock::now();
    }

    void OnQuit(ok_call*, void*) {
        remote::Stop();
        if (!started) return;
        render::Stop();
        runtime::Close();
    }

    int Start(const OkMod* api) {
        host = api;
        game::Attach(api);
        log = _wfopen((GameFolder() / "scripts.log").c_str(), L"w");
        runtime::SetLogger(Write);
        server = remote::Start(GameFolder() / "scripts-server");
        api::SetSender(remote::Send);
        images::SetSource(remote::Want);
        api->on("frame", OnFrame, nullptr);
        api->on("world.loaded", OnWorld, nullptr);
        api->on("frame.late", OnLateFrame, nullptr);
        api->on("app.quit", OnQuit, nullptr);
        return 0;
    }
}

extern "C" {
OKMOD_MOD(Start)
}
