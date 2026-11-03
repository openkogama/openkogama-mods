#include "okmod.h"
#include "api.h"
#include "build.h"
#include "editor.h"
#include "tools.h"
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
    bool reloading = true;
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
            reloading = false;
            remote::Refresh();
            runtime::Emit("world");
            runtime::Keep("world");
        }
        for (const remote::Message& message : remote::Take())
            runtime::Emit("message", {runtime::Arg::Of(message.name), runtime::Arg::Of(message.data)});
        runtime::Tick(dt);
        api::Maintain(dt);
        tools::Apply();
        editor::Tick(dt);
        api::HoldCursor();
        if (api::Blocking()) game::SuppressInput();
        images::Sweep();
        render::Frame();
    }

    void OnLateFrame(ok_call*, void*) {
        if (!started) return;
        api::HoldCursor();
        tools::Apply();
    }

    void OnLoading(ok_call*, void*) {
        reloading = true;
        api::Unblock();
    }

    void OnWorld(ok_call*, void*) {
        if (!reloading) return;
        loading = true;
        loaded = Clock::now();
    }

    void OnCommand(ok_call* call, void*) {
        if (!started) return;
        char line[1024] = "";
        ok_object text = host->arg_object(call, 0);
        if (!text || host->string_utf8(text, line, sizeof(line)) <= 0) return;
        if (runtime::Ask("command", {runtime::Arg::Of(std::string(line))})) host->cancel(call);
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
        build::Attach(api);
        tools::Attach(api);
        editor::Attach(api);
        api::SetHost(api);
        log = _wfopen((GameFolder() / "scripts.log").c_str(), L"w");
        runtime::SetLogger(Write);
        server = remote::Start(GameFolder() / "scripts-server");
        api::SetSender(remote::Send);
        images::SetSource(remote::Want);
        api->on("frame", OnFrame, nullptr);
        api->on("loading.start", OnLoading, nullptr);
        api->on("world.loaded", OnWorld, nullptr);
        api->on("frame.late", OnLateFrame, nullptr);
        api->on("app.quit", OnQuit, nullptr);
        api->on("chat.command", OnCommand, nullptr);
        return 0;
    }
}

extern "C" {
OKMOD_MOD(Start)
}
