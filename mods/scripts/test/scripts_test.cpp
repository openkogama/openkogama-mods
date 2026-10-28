#include "api.h"
#include "frame.h"
#include "runtime.h"
#include "textures.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
    const unsigned char Picture[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4, 0x22, 0x7f, 0x8a, 0x00, 0x00, 0x00, 0x0e, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9c, 0x63, 0xd0, 0xef, 0x79, 0xf1, 0x1f, 0x04, 0x00, 0x19, 0xb6, 0x06, 0x9f, 0xf1, 0x5d, 0xe8, 0xb4, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };

    std::vector<std::string> lines;
    std::vector<std::string> sent;
    int failed = 0;

    bool Record(const std::string& name, const std::string& data) {
        sent.push_back(name + " " + data);
        return true;
    }

    void Collect(const std::string& line) {
        lines.push_back(line);
        std::printf("  %s\n", line.c_str());
    }

    bool Logged(const std::string& script, const std::string& part) {
        std::string prefix = "[" + script + "] ";
        for (const std::string& line : lines)
            if (line.compare(0, prefix.size(), prefix) == 0 && line.find(part) != std::string::npos) return true;
        return false;
    }

    void Expect(bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) failed++;
    }

    void Write(const fs::path& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    struct Result {
        size_t vertices = 0;
        size_t pictures = 0;
        size_t atlases = 0;
        double slowest = 0;
    };

    void Run(int frames, Result& result) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < frames; i++) {
            auto start = std::chrono::steady_clock::now();
            io.DeltaTime = 1.0f / 60.0f;
            runtime::Tick(1.0 / 60.0);
            api::LoadFonts();
            ImGui::NewFrame();
            runtime::Draw();
            ImGui::Render();
            auto frame = frame::Build(ImGui::GetDrawData());
            result.vertices = std::max(result.vertices, frame->vertices.size());
            for (const textures::Op& op : textures::Take()) {
                if (op.kind != textures::Op::Create) continue;
                if (op.width == 2 && op.height == 1) result.pictures++;
                else result.atlases++;
            }
            result.slowest = std::max(result.slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: scripts_test <folder>\n");
        return 2;
    }
    fs::path folder = fs::temp_directory_path() / "openkogama-scripts-test";
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder);
    fs::copy(fs::u8path(argv[1]) / "scripts", folder, fs::copy_options::recursive);
    std::ofstream(folder / "hud" / "dot.png", std::ios::binary).write(reinterpret_cast<const char*>(Picture), sizeof(Picture));

    runtime::SetLogger(Collect);
    api::SetSender(Record);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigErrorRecoveryEnableAssert = false;
    api::Style();

    runtime::Open({{folder, ""}});
    Result result;
    Run(5, result);
    api::Key("F7", true);
    api::Key("F7", false);
    runtime::Emit("world");
    runtime::Emit("message", {runtime::Arg::Of(std::string("test:reply")), runtime::Arg::Of(std::string("42"))});
    Run(40, result);

    Expect(Logged("sandbox", "sandbox ok"), "the sandbox keeps files, processes, bytecode and finalizers away");
    Expect(Logged("hud", "isolated"), "scripts do not share globals");
    Expect(Logged("hud", "outside ok"), "drawing outside on(\"draw\") is refused");
    Expect(Logged("hud", "screen 1280x720"), "screen.size reports the window");
    Expect(Logged("hud", "key F7 true") && Logged("hud", "key F7 false"), "keys reach scripts");
    Expect(Logged("hud", "world"), "game events reach scripts");
    Expect(sent.size() == 1 && sent[0] == "test:hello from lua", "server.send hands messages to the game's connection");
    Expect(Logged("hud", "message test:reply 42"), "messages from the server reach scripts");
    Expect(Logged("timers", "timers ok"), "after and every fire on time");
    Expect(Logged("loop", "took too long") && Logged("loop", "stopped after"), "an endless loop is cut off and the script stops");
    Expect(Logged("trap", "took too long"), "an endless loop behind pcall is cut off");
    Expect(Logged("pattern", "took too long"), "a slow string pattern is cut off");
    Expect(Logged("memory", "not enough memory"), "the memory limit holds");
    Expect(Logged("failing", "stopped after 10 errors"), "a script that keeps failing stops");
    Expect(Logged("broken", "unexpected symbol"), "a syntax error is reported");
    Expect(result.vertices > 0, "the hud produces geometry");
    Expect(Logged("hud", "styles ok"), "styles take imgui's names and refuse the rest");
    Expect(ImGui::GetIO().Fonts->Fonts.Size >= 3, "scripts pick fonts of the Windows Fonts folder");
    Expect(result.atlases > 0, "the font atlas is uploaded");
    Expect(result.pictures == 1, "an image is decoded once");
    Expect(result.slowest < 1.0, "no frame takes longer than a second");

    fs::create_directories(folder / "reload");
    Write(folder / "reload" / "main.lua", "print(\"version 1\")\n");
    runtime::Watch();
    Write(folder / "reload" / "main.lua", "print(\"version 2\")\n");
    fs::last_write_time(folder / "reload" / "main.lua", fs::last_write_time(folder / "reload" / "main.lua") + std::chrono::seconds(2));
    runtime::Watch();
    fs::remove_all(folder / "reload");
    runtime::Watch();
    Expect(Logged("reload", "version 1") && Logged("reload", "version 2") && Logged("reload", "removed"), "scripts reload when their files change");

    runtime::Close();
    ImGui::DestroyContext();
    fs::remove_all(folder, error);
    std::printf("%d failed\n", failed);
    return failed ? 1 : 0;
}
