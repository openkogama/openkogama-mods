#include "stdafx.h"
#include "cubes.h"
#include "world.h"
#include "bridge.h"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <tuple>
#include <utility>
#include <vector>

namespace cubes {
    namespace {
        using Clock = std::chrono::steady_clock;
        using json = nlohmann::json;

        constexpr size_t BatchSize = 500;
        constexpr double BatchPause = 500.0 / 60.0;
        constexpr char Magic[4] = {'K', 'S', 'C', 'B'};
        constexpr char FileVersion = 1;
        constexpr int Axes = 3;
        constexpr int Faces = 6;
        constexpr int Corners = 8;
        constexpr int Steps = 5;
        constexpr int Top = Steps - 1;

        struct Cube {
            int x = 0, y = 0, z = 0;
            std::array<uint8_t, Faces> materials{};
            std::array<uint8_t, Corners> corners{};
        };

        struct Position { int16_t x, y, z; };

        struct Corner { int x, y, z; };

        struct Paste {
            std::vector<Position> removes;
            std::vector<Cube> adds;
            size_t done = 0;
            int target = -1;
            bool active = false;
            int reported = -1;
            Clock::time_point resumeAt = Clock::now();
        };

        const OkMod* api = nullptr;
        std::vector<Cube> clipboard;
        int offset[Axes] = {};
        int rotation[Axes] = {};
        int clipLow[Axes] = {};
        int clipHigh[Axes] = {};
        bool mirror[Axes] = {};
        bool clearFirst = false;
        Paste paste;

        int64_t Pointer(const void* value) { return (int64_t)(uintptr_t)value; }

        int& Axis(Cube& cube, int axis) { return axis == 0 ? cube.x : axis == 1 ? cube.y : cube.z; }
        int Axis(const Cube& cube, int axis) { return axis == 0 ? cube.x : axis == 1 ? cube.y : cube.z; }
        int& Axis(Corner& corner, int axis) { return axis == 0 ? corner.x : axis == 1 ? corner.y : corner.z; }

        Corner Unpack(uint8_t value) { return {value % Steps, (value / Steps) % Steps, (value / (Steps * Steps)) % Steps}; }
        uint8_t Pack(const Corner& c) { return (uint8_t)(c.x + c.y * Steps + c.z * Steps * Steps); }

        void SendCount() { bridge::ToMenu("clipboard_count|" + std::to_string(clipboard.size())); }

        ok_object Bytes(ok_object array, size_t needed) {
            return api->array_length(array) >= (int)needed ? array : 0;
        }

        std::vector<Cube> Read(ok_object model) {
            std::vector<Cube> result;
            ok_object prototype = model ? (ok_object)api->invoke(model, "MVCubeModelBase", "get_PrototypeCubeModel", 0, nullptr, "", 'o') : 0;
            ok_object chunks = prototype ? (ok_object)api->invoke(prototype, "RuntimePrototypeCubeModel", "get_Chunks", 0, nullptr, "", 'o') : 0;
            for (ok_object chunk : world::DictionaryValues(chunks, "CubeModelChunk")) {
                ok_object keys = world::DictionaryKeys(api->field_obj(chunk, "CubeModelChunk", "cells"), "MV.WorldObject.IntVector");
                int count = api->array_length(keys), stride = api->array_stride(keys);
                const uint8_t* data = (const uint8_t*)api->array_data(keys);
                if (!data || stride < (int)sizeof(Position)) continue;
                for (int i = 0; i < count; i++) {
                    Position at;
                    std::memcpy(&at, data + (size_t)i * stride, sizeof(at));
                    int64_t where = Pointer(&at);
                    ok_object cube = (ok_object)api->invoke(prototype, "RuntimePrototypeCubeModel", "GetCube", 1, &where, "p", 'o');
                    if (!cube) continue;
                    ok_object materials = Bytes((ok_object)api->invoke(cube, "MV.WorldObject.CubeBase", "get_FaceMaterials", 0, nullptr, "", 'o'), Faces);
                    ok_object corners = Bytes((ok_object)api->invoke(cube, "MV.WorldObject.CubeBase", "get_ByteCorners", 0, nullptr, "", 'o'), Corners);
                    if (!materials || !corners) continue;
                    Cube entry;
                    entry.x = at.x;
                    entry.y = at.y;
                    entry.z = at.z;
                    std::memcpy(entry.materials.data(), api->array_data(materials), Faces);
                    std::memcpy(entry.corners.data(), api->array_data(corners), Corners);
                    result.push_back(entry);
                }
            }
            return result;
        }

        std::vector<Cube> Clip(const std::vector<Cube>& cubes, int axis, int low, int high) {
            if (cubes.empty()) return cubes;
            int lowest[Axes], highest[Axes];
            for (int a = 0; a < Axes; a++) {
                lowest[a] = highest[a] = Axis(cubes.front(), a);
                for (const Cube& cube : cubes) {
                    lowest[a] = std::min(lowest[a], Axis(cube, a));
                    highest[a] = std::max(highest[a], Axis(cube, a));
                }
            }
            lowest[axis] = low;
            highest[axis] = high;
            std::vector<Cube> kept;
            for (const Cube& cube : cubes) {
                bool inside = true;
                for (int a = 0; a < Axes; a++) inside = inside && Axis(cube, a) >= lowest[a] && Axis(cube, a) <= highest[a];
                if (inside) kept.push_back(cube);
            }
            return kept;
        }

        std::vector<Cube> Rotate(const std::vector<Cube>& cubes, int axis, int degrees) {
            static const int orders[Axes][Corners] = {{7, 6, 1, 0, 3, 2, 5, 4}, {1, 2, 3, 0, 7, 4, 5, 6}, {1, 6, 5, 2, 3, 4, 7, 0}};
            int turns = (degrees / 90) % 4;
            if (turns == 0) return cubes;
            std::vector<Cube> result;
            for (Cube cube : cubes) {
                for (int turn = 0; turn < turns; turn++) {
                    if (axis == 0) std::tie(cube.y, cube.z) = std::make_pair(-cube.z, cube.y);
                    else if (axis == 1) std::tie(cube.x, cube.z) = std::make_pair(cube.z, -cube.x);
                    else std::tie(cube.x, cube.y) = std::make_pair(-cube.y, cube.x);
                    std::array<uint8_t, Corners> turned{};
                    for (int i = 0; i < Corners; i++) {
                        Corner c = Unpack(cube.corners[i]);
                        if (axis == 0) std::tie(c.x, c.y) = std::make_pair(c.y, Top - c.x);
                        else if (axis == 1) std::tie(c.x, c.z) = std::make_pair(Top - c.z, c.x);
                        else std::tie(c.y, c.z) = std::make_pair(c.z, Top - c.y);
                        turned[i] = Pack(c);
                    }
                    for (int i = 0; i < Corners; i++) cube.corners[i] = turned[orders[axis][i]];
                    auto& m = cube.materials;
                    if (axis == 0) std::tie(m[1], m[2], m[4], m[5]) = std::make_tuple(m[5], m[1], m[2], m[4]);
                    else if (axis == 1) std::tie(m[0], m[2], m[3], m[5]) = std::make_tuple(m[2], m[3], m[5], m[0]);
                    else std::tie(m[0], m[1], m[3], m[4]) = std::make_tuple(m[4], m[0], m[1], m[3]);
                }
                result.push_back(cube);
            }
            return result;
        }

        std::vector<Cube> Mirror(const std::vector<Cube>& cubes, int axis) {
            static const int swaps[Axes][4][2] = {
                {{0, 3}, {1, 2}, {4, 7}, {5, 6}},
                {{0, 7}, {1, 6}, {2, 5}, {3, 4}},
                {{0, 1}, {3, 2}, {4, 5}, {7, 6}},
            };
            if (cubes.empty()) return cubes;
            int low = Axis(cubes.front(), axis), high = low;
            for (const Cube& cube : cubes) {
                low = std::min(low, Axis(cube, axis));
                high = std::max(high, Axis(cube, axis));
            }
            double center = (low + high) / 2.0;
            std::vector<Cube> result;
            for (Cube cube : cubes) {
                Axis(cube, axis) = (int)(2 * center - Axis(cube, axis));
                Corner corners[Corners];
                for (int i = 0; i < Corners; i++) corners[i] = Unpack(cube.corners[i]);
                for (const auto& pair : swaps[axis]) std::swap(corners[pair[0]], corners[pair[1]]);
                for (Corner& corner : corners) Axis(corner, axis) = Top - Axis(corner, axis);
                if (axis != 1) {
                    for (Corner& corner : corners) corner = {Top - corner.x, corner.y, Top - corner.z};
                    std::swap(corners[0], corners[2]);
                    std::swap(corners[1], corners[3]);
                    std::swap(corners[4], corners[6]);
                    std::swap(corners[5], corners[7]);
                }
                for (int i = 0; i < Corners; i++) cube.corners[i] = Pack(corners[i]);
                auto& m = cube.materials;
                if (axis == 0) std::swap(m[2], m[5]);
                else if (axis == 1) std::swap(m[0], m[1]);
                else std::swap(m[3], m[4]);
                result.push_back(cube);
            }
            return result;
        }

        std::vector<Cube> Transformed() {
            std::vector<Cube> cubes = clipboard;
            for (int a = 0; a < Axes; a++)
                if (clipLow[a] != 0 || clipHigh[a] != 0) cubes = Clip(cubes, a, clipLow[a], clipHigh[a]);
            for (int a = 0; a < Axes; a++)
                if (rotation[a] != 0) cubes = Rotate(cubes, a, rotation[a]);
            for (int a = 0; a < Axes; a++)
                if (mirror[a]) cubes = Mirror(cubes, a);
            for (Cube& cube : cubes) {
                cube.x += offset[0];
                cube.y += offset[1];
                cube.z += offset[2];
            }
            return cubes;
        }

        Position At(int x, int y, int z) { return {(int16_t)x, (int16_t)y, (int16_t)z}; }

        bool Contains(ok_object model, Position& at) {
            int64_t where = Pointer(&at);
            return api->invoke(model, "MVCubeModelBase", "ContainsCube", 1, &where, "p", 'i') & 0xFF;
        }

        void Remove(ok_object model, Position at) {
            int64_t where = Pointer(&at);
            if (Contains(model, at)) api->invoke(model, "MVCubeModelBase", "RemoveCube", 1, &where, "p", 'v');
        }

        void Add(ok_object model, const Cube& cube) {
            ok_object corners = api->array_new("System.Byte", Corners);
            ok_object materials = api->array_new("System.Byte", Faces);
            if (!corners || !materials) return;
            std::memcpy(api->array_data(corners), cube.corners.data(), Corners);
            std::memcpy(api->array_data(materials), cube.materials.data(), Faces);
            int64_t parts[] = {(int64_t)corners, (int64_t)materials};
            ok_object block = api->new_object("Cube", 2, parts, "oo");
            if (!block) return;
            Position at = At(cube.x, cube.y, cube.z);
            Remove(model, at);
            int64_t args[] = {Pointer(&at), (int64_t)block};
            api->invoke(model, "MVCubeModelBase", "AddCube", 2, args, "po", 'v');
        }

        void Finish() {
            paste.active = false;
            paste.adds.clear();
            paste.removes.clear();
            bridge::ToMenu("clipboard_paste_done");
        }

        void Report(size_t total) {
            if ((int)paste.done == paste.reported) return;
            paste.reported = (int)paste.done;
            size_t batches = (total - paste.done + BatchSize - 1) / BatchSize;
            char line[128];
            std::snprintf(line, sizeof(line), "clipboard_paste_progress|%d|%d|%.1f", (int)paste.done, (int)total, batches * BatchPause);
            bridge::ToMenu(line);
        }

        void StartPaste() {
            if (paste.active || clipboard.empty()) return;
            int target = world::SelectedId();
            ok_object model = world::ModelById(target);
            if (!model) return;
            paste = Paste{};
            paste.target = target;
            paste.adds = Transformed();
            if (clearFirst)
                for (const Cube& cube : Read(model)) paste.removes.push_back(At(cube.x, cube.y, cube.z));
            paste.active = true;
        }

        void Step() {
            if (!paste.active || Clock::now() < paste.resumeAt) return;
            ok_object model = world::ModelById(paste.target);
            if (!model || (api->call_static_i("MVGameControllerBase", "get_IsPlaying") & 0xFF)) {
                Finish();
                return;
            }
            size_t total = paste.removes.size() + paste.adds.size();
            size_t end = std::min(total, paste.done + BatchSize);
            for (; paste.done < end; paste.done++) {
                if (paste.done < paste.removes.size()) Remove(model, paste.removes[paste.done]);
                else Add(model, paste.adds[paste.done - paste.removes.size()]);
            }
            api->invoke(model, "MVCubeModelBase", "HandleDelta", 0, nullptr, "", 'v');
            Report(total);
            if (paste.done >= total) Finish();
            else paste.resumeAt = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(BatchPause));
        }

        void Cancel() {
            if (!paste.active) return;
            if (ok_object model = world::ModelById(paste.target)) api->invoke(model, "MVCubeModelBase", "HandleDelta", 0, nullptr, "", 'v');
            Finish();
        }

        bool Rooted(const std::string& path) {
            return (path.size() > 1 && path[1] == ':') || (!path.empty() && (path[0] == '\\' || path[0] == '/'));
        }

        std::string CubesFile(const std::string& name) {
            if (Rooted(name)) return name;
            char base[MAX_PATH] = {};
            SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base);
            return std::string(base) + "\\KogamaStudio\\Generate\\Cubes\\" + name + ".kscubes";
        }

        void MakeFolder(const std::string& path) {
            size_t slash = path.find_last_of("\\/");
            if (slash != std::string::npos) SHCreateDirectoryExA(nullptr, path.substr(0, slash).c_str(), nullptr);
        }

        void Save(const std::string& name) {
            if (clipboard.empty()) return;
            std::string path = CubesFile(name);
            MakeFolder(path);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) return;
            int32_t count = (int32_t)clipboard.size();
            file.write(Magic, sizeof(Magic));
            file.put(FileVersion);
            file.write(reinterpret_cast<const char*>(&count), sizeof(count));
            for (const Cube& cube : clipboard) {
                int32_t at[Axes] = {cube.x, cube.y, cube.z};
                file.write(reinterpret_cast<const char*>(at), sizeof(at));
                file.write(reinterpret_cast<const char*>(cube.materials.data()), Faces);
                file.write(reinterpret_cast<const char*>(cube.corners.data()), Corners);
            }
        }

        std::vector<uint8_t> Decode(const std::string& text) {
            static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::vector<uint8_t> bytes;
            int buffer = 0, bits = 0;
            for (char c : text) {
                size_t value = alphabet.find(c);
                if (value == std::string::npos) continue;
                buffer = (buffer << 6) | (int)value;
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    bytes.push_back((uint8_t)((buffer >> bits) & 0xFF));
                }
            }
            return bytes;
        }

        template <size_t N>
        void Fill(std::array<uint8_t, N>& target, const json& source) {
            std::vector<uint8_t> bytes;
            if (source.is_string()) bytes = Decode(source.get<std::string>());
            else if (source.is_array())
                for (const json& value : source) bytes.push_back((uint8_t)value.get<int>());
            std::copy_n(bytes.begin(), std::min(bytes.size(), N), target.begin());
        }

        void Load(const std::string& name) {
            std::ifstream file(CubesFile(name), std::ios::binary);
            if (!file) return;
            std::vector<Cube> cubes;
            if (file.peek() == '{') {
                json root = json::parse(file, nullptr, false);
                if (!root.is_object() || !root["cubes"].is_array()) return;
                for (const json& entry : root["cubes"]) {
                    Cube cube;
                    cube.x = entry.value("x", 0);
                    cube.y = entry.value("y", 0);
                    cube.z = entry.value("z", 0);
                    if (entry.contains("materials")) Fill(cube.materials, entry["materials"]);
                    if (entry.contains("corners")) Fill(cube.corners, entry["corners"]);
                    cubes.push_back(cube);
                }
            }
            else {
                char magic[sizeof(Magic)] = {};
                int32_t count = 0;
                file.read(magic, sizeof(magic));
                if (!file || std::memcmp(magic, Magic, sizeof(Magic)) != 0) return;
                file.get();
                file.read(reinterpret_cast<char*>(&count), sizeof(count));
                for (int32_t i = 0; i < count && file; i++) {
                    int32_t at[Axes] = {};
                    Cube cube;
                    file.read(reinterpret_cast<char*>(at), sizeof(at));
                    file.read(reinterpret_cast<char*>(cube.materials.data()), Faces);
                    file.read(reinterpret_cast<char*>(cube.corners.data()), Corners);
                    if (!file) break;
                    cube.x = at[0];
                    cube.y = at[1];
                    cube.z = at[2];
                    cubes.push_back(cube);
                }
            }
            clipboard = std::move(cubes);
            SendCount();
        }

        bool SetAxis(const std::string& command, const char* prefix, int* values, int value) {
            std::string start = prefix;
            if (command.size() != start.size() + 1 || command.compare(0, start.size(), start) != 0) return false;
            char axis = command.back();
            if (axis < 'x' || axis > 'z') return false;
            values[axis - 'x'] = value;
            return true;
        }
    }

    void Attach(const OkMod* host) { api = host; }

    bool Handle(const std::string& command, const std::string& value) {
        int number = std::atoi(value.c_str());
        bool on = value == "true";
        if (command == "clipboard_copy_model") {
            clipboard = Read(world::SelectedModel());
            SendCount();
        }
        else if (command == "clipboard_clear") {
            clipboard.clear();
            SendCount();
        }
        else if (command == "clipboard_paste_model") StartPaste();
        else if (command == "clipboard_paste_cancel") Cancel();
        else if (command == "clipboard_clear_before_paste") clearFirst = on;
        else if (command == "clipboard_save_to_file") Save(value);
        else if (command == "clipboard_load_from_file") Load(value);
        else if (command == "clipboard_preview_paste_model") {}
        else if (command.rfind("clipboard_mirror_", 0) == 0 && command.size() == 18) mirror[command.back() - 'x'] = on;
        else if (SetAxis(command, "clipboard_offset_", offset, number)) {}
        else if (SetAxis(command, "clipboard_rotation_", rotation, number)) {}
        else if (SetAxis(command, "clipboard_clip_min_", clipLow, number)) {}
        else if (SetAxis(command, "clipboard_clip_max_", clipHigh, number)) {}
        else return false;
        return true;
    }

    void Tick() { Step(); }
}
