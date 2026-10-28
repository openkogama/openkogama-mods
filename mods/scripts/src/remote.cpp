#include "remote.h"
#include "runtime.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace remote {
    namespace {
        constexpr size_t ManifestLimit = 1u << 20;
        constexpr size_t FileLimit = 1u << 20;
        constexpr size_t TotalLimit = 16u << 20;
        constexpr size_t FileCount = 1024;
        constexpr size_t MessageLimit = 64u << 10;
        constexpr size_t PollLimit = 4u << 20;
        constexpr size_t QueueLimit = 256;
        constexpr std::chrono::seconds Interval(5);
        constexpr size_t Pullers = 4;
        constexpr size_t WantedLimit = 65536;
        constexpr std::chrono::seconds RetryAfter(30);
        constexpr uintmax_t DownloadsLimit = 256u << 20;
        constexpr std::chrono::hours DownloadsAge(24 * 7);

        struct Entry {
            std::string path;
            std::string hash;
            size_t size = 0;
        };

        using Manifest = std::map<std::string, std::vector<Entry>>;

        struct Wanted {
            images::State state = images::State::Loading;
            std::chrono::steady_clock::time_point since;
        };

        std::string root;
        std::string key;
        fs::path folder;
        fs::path store;
        fs::path downloads;
        std::mutex lock;
        std::condition_variable wake;
        std::atomic<bool> stopping{false};
        bool nudged = false;
        std::mutex mail;
        std::condition_variable outbox;
        std::vector<Message> outgoing;
        std::vector<Message> incoming;
        bool messaging = false;
        bool reachable = true;
        std::string announced;
        std::map<std::string, std::string> applied;
        std::mutex pulls;
        std::map<std::string, Wanted> wanted;
        std::deque<std::string> waiting;
        size_t pulling = 0;

        std::wstring Wide(const std::string& text) {
            if (text.empty()) return {};
            int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
            std::wstring wide(length, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), length);
            return wide;
        }

        std::string Base64(const std::string& text) {
            static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string decoded;
            unsigned value = 0;
            int bits = 0;
            for (char c : text) {
                size_t digit = alphabet.find(c == '-' ? '+' : c == '_' ? '/' : c);
                if (digit == std::string::npos) continue;
                value = (value << 6) | (unsigned)digit;
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    decoded.push_back((char)((value >> bits) & 0xFF));
                }
            }
            return decoded;
        }

        std::string Parameter(const std::string& url, const std::string& name) {
            size_t query = url.find('?');
            if (query == std::string::npos) return {};
            std::stringstream pairs(url.substr(query + 1));
            std::string pair;
            while (std::getline(pairs, pair, '&'))
                if (pair.compare(0, name.size() + 1, name + "=") == 0) return pair.substr(name.size() + 1);
            return {};
        }

        std::string Locate() {
            int count = 0;
            LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
            if (!arguments) return {};
            std::string url;
            const std::wstring prefix = L"kogamaPackage:";
            for (int i = 0; i < count && url.empty(); i++) {
                std::wstring argument = arguments[i];
                if (argument.compare(0, prefix.size(), prefix) != 0) continue;
                std::string encoded;
                for (wchar_t c : argument.substr(prefix.size())) encoded.push_back((char)c);
                url = Base64(encoded);
            }
            LocalFree(arguments);
            size_t session = url.find("/session");
            if (session == std::string::npos || url.compare(0, 4, "http") != 0) return {};
            key = Parameter(url, "key");
            for (char c : key)
                if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') key.clear();
            return url.substr(0, session);
        }

        std::string Place(const std::string& url) {
            size_t start = url.find("://");
            std::string place = url.substr(start == std::string::npos ? 0 : start + 3);
            for (char& c : place)
                if (!std::isalnum((unsigned char)c) && c != '.' && c != '-') c = '_';
            return place;
        }

        bool Request(const wchar_t* method, const std::string& path, const std::string& payload, std::string& body, size_t limit, int timeout) {
            std::wstring url = Wide(root + path);
            wchar_t host[256] = L"", resource[2048] = L"";
            URL_COMPONENTS parts = {sizeof(parts)};
            parts.lpszHostName = host;
            parts.dwHostNameLength = 256;
            parts.lpszUrlPath = resource;
            parts.dwUrlPathLength = 2048;
            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;
            HINTERNET session = WinHttpOpen(L"OpenKogama scripts", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session) return false;
            WinHttpSetTimeouts(session, 5000, 5000, 5000, timeout);
            HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
            HINTERNET request = connection ? WinHttpOpenRequest(connection, method, resource, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
            DWORD status = 0, size = sizeof(status);
            bool ok = request && WinHttpSendRequest(request, payload.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: text/plain; charset=utf-8", payload.empty() ? 0 : (DWORD)-1L,
                    payload.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)payload.data(), (DWORD)payload.size(), (DWORD)payload.size(), 0)
                && WinHttpReceiveResponse(request, nullptr)
                && WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)
                && (status == 200 || status == 204);
            body.clear();
            while (ok) {
                char buffer[16384];
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) ok = false;
                else if (read == 0) break;
                else if (body.size() + read > limit) ok = false;
                else body.append(buffer, read);
            }
            if (request) WinHttpCloseHandle(request);
            if (connection) WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return ok;
        }

        bool Get(const std::string& path, std::string& body, size_t limit) {
            return Request(L"GET", path, {}, body, limit, 10000);
        }

        bool MessageName(const std::string& name) {
            if (name.empty() || name.size() > 64) return false;
            for (char c : name)
                if (!std::isalnum((unsigned char)c) && c != '_' && c != '.' && c != ':' && c != '-') return false;
            return true;
        }

        std::string Pack(const std::vector<Message>& messages) {
            std::string text;
            for (const Message& message : messages)
                text += message.name + " " + std::to_string(message.data.size()) + "\n" + message.data + "\n";
            return text;
        }

        std::vector<Message> Unpack(const std::string& text) {
            std::vector<Message> messages;
            size_t at = 0;
            while (at < text.size()) {
                size_t end = text.find('\n', at);
                if (end == std::string::npos) break;
                std::stringstream header(text.substr(at, end - at));
                Message message;
                size_t length = 0;
                if (!(header >> message.name >> length) || length > MessageLimit || end + 1 + length > text.size()) break;
                message.data = text.substr(end + 1, length);
                at = end + 1 + length + 1;
                if (MessageName(message.name)) messages.push_back(std::move(message));
            }
            return messages;
        }

        void Sending() {
            std::unique_lock<std::mutex> guard(mail);
            while (!stopping) {
                outbox.wait(guard, [] { return stopping || !outgoing.empty(); });
                if (stopping) return;
                std::vector<Message> batch;
                batch.swap(outgoing);
                guard.unlock();
                std::string answer;
                Request(L"POST", "/api/scripts/send?key=" + key, Pack(batch), answer, 1024, 5000);
                guard.lock();
            }
        }

        void Polling() {
            while (!stopping) {
                auto asked = std::chrono::steady_clock::now();
                std::string body;
                std::vector<Message> messages;
                if (Request(L"GET", "/api/scripts/poll?key=" + key, {}, body, PollLimit, 30000)) messages = Unpack(body);
                if (!messages.empty()) {
                    std::lock_guard<std::mutex> guard(mail);
                    for (Message& message : messages)
                        if (incoming.size() < QueueLimit * 4) incoming.push_back(std::move(message));
                }
                else if (std::chrono::steady_clock::now() - asked < std::chrono::seconds(1)) std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }

        void Converse() {
            std::lock_guard<std::mutex> guard(mail);
            if (messaging || key.empty()) return;
            messaging = true;
            std::thread(Sending).detach();
            std::thread(Polling).detach();
        }

        std::string Sha256(const std::string& data) {
            unsigned char digest[32];
            if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), digest, sizeof(digest)) != 0) return {};
            static const char digits[] = "0123456789abcdef";
            std::string hex;
            for (unsigned char byte : digest) {
                hex.push_back(digits[byte >> 4]);
                hex.push_back(digits[byte & 15]);
            }
            return hex;
        }

        bool Name(const std::string& name) {
            if (name.empty() || name.size() > 48) return false;
            for (char c : name)
                if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') return false;
            return true;
        }

        bool Path(const std::string& path) {
            if (path.empty() || path.size() > 260) return false;
            std::stringstream parts(path);
            std::string part;
            int depth = 0;
            while (std::getline(parts, part, '/')) {
                if (part.empty() || part == "." || part == ".." || part.size() > 64 || ++depth > 4) return false;
                for (char c : part)
                    if (!std::isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') return false;
            }
            if (path.back() == '/') return false;
            std::string kind = fs::u8path(path).extension().u8string();
            for (char& c : kind) c = (char)std::tolower((unsigned char)c);
            return kind == ".lua" || kind == ".png" || kind == ".jpg" || kind == ".jpeg" || kind == ".json" || kind == ".txt";
        }

        bool Hash(const std::string& hash) {
            if (hash.size() != 64) return false;
            for (char c : hash)
                if (!std::isdigit((unsigned char)c) && (c < 'a' || c > 'f')) return false;
            return true;
        }

        Manifest Parse(const std::string& text) {
            Manifest manifest;
            std::stringstream lines(text);
            std::string line;
            size_t total = 0;
            while (std::getline(lines, line)) {
                std::stringstream fields(line);
                Entry entry;
                std::string name;
                if (!(fields >> name >> entry.path >> entry.hash >> entry.size)) continue;
                if (!Name(name) || !Path(entry.path) || !Hash(entry.hash) || entry.size > FileLimit) continue;
                std::vector<Entry>& files = manifest[name];
                if (files.size() >= FileCount || total + entry.size > TotalLimit) continue;
                total += entry.size;
                files.push_back(entry);
            }
            for (auto it = manifest.begin(); it != manifest.end();) {
                bool hasMain = false;
                for (const Entry& entry : it->second) hasMain = hasMain || entry.path == "main.lua";
                it = hasMain ? std::next(it) : manifest.erase(it);
            }
            return manifest;
        }

        bool Keep(const fs::path& place, const std::string& hash, const std::string& body) {
            std::error_code error;
            fs::create_directories(place, error);
            fs::path partial = place / (hash + ".part");
            {
                std::ofstream out(partial, std::ios::binary);
                out.write(body.data(), (std::streamsize)body.size());
                if (!out) return false;
            }
            fs::rename(partial, place / hash, error);
            return !error;
        }

        bool Fetch(const Entry& entry) {
            std::error_code error;
            if (fs::exists(store / entry.hash, error)) return true;
            std::string body;
            return Get("/api/scripts/" + entry.hash, body, FileLimit) && Sha256(body) == entry.hash && Keep(store, entry.hash, body);
        }

        bool Download(const std::string& id) {
            std::string body;
            return Get("/api/scripts/" + id + "?key=" + key, body, FileLimit) && Sha256(body) == id && Keep(downloads, id, body);
        }

        void Pulling() {
            std::unique_lock<std::mutex> guard(pulls);
            while (!waiting.empty() && !stopping) {
                std::string id = waiting.front();
                waiting.pop_front();
                guard.unlock();
                bool done = Download(id);
                guard.lock();
                Wanted& entry = wanted[id];
                entry.state = done ? images::State::Ready : images::State::Failed;
                entry.since = std::chrono::steady_clock::now();
            }
            pulling--;
        }

        void Prune() {
            struct Kept {
                fs::file_time_type time;
                uintmax_t size;
                fs::path path;
            };
            std::vector<Kept> kept;
            uintmax_t total = 0;
            fs::file_time_type now = fs::file_time_type::clock::now();
            std::error_code error;
            for (fs::directory_iterator entry(downloads, error), end; !error && entry != end; entry.increment(error)) {
                std::error_code timeError, sizeError;
                Kept file{entry->last_write_time(timeError), entry->file_size(sizeError), entry->path()};
                if (timeError || sizeError || file.path.extension() == ".part" || now - file.time > DownloadsAge) fs::remove(file.path, timeError);
                else {
                    total += file.size;
                    kept.push_back(file);
                }
            }
            std::sort(kept.begin(), kept.end(), [](const Kept& a, const Kept& b) { return a.time < b.time; });
            for (const Kept& file : kept) {
                if (total <= DownloadsLimit) break;
                fs::remove(file.path, error);
                total -= file.size;
            }
        }

        bool Apply(const std::string& name, const std::vector<Entry>& files) {
            std::error_code error;
            fs::path staging = folder / (".staging-" + name);
            fs::path target = folder / name;
            fs::path old = folder / (".old-" + name);
            fs::remove_all(staging, error);
            for (const Entry& entry : files) {
                fs::path destination = staging / fs::u8path(entry.path);
                fs::create_directories(destination.parent_path(), error);
                if (!fs::copy_file(store / entry.hash, destination, fs::copy_options::overwrite_existing, error)) return false;
            }
            fs::remove_all(old, error);
            if (fs::exists(target, error)) {
                fs::rename(target, old, error);
                if (error) return false;
            }
            fs::rename(staging, target, error);
            if (error) return false;
            fs::remove_all(old, error);
            return true;
        }

        void Sync() {
            std::string text;
            if (!Get("/api/scripts", text, ManifestLimit)) {
                if (reachable) runtime::Log("scripts from " + root + " are out of reach");
                reachable = false;
                return;
            }
            reachable = true;
            Manifest manifest = Parse(text);
            if (!manifest.empty()) Converse();
            std::error_code error;
            fs::create_directories(store, error);
            std::set<std::string> kept;
            std::string names;
            for (const auto& script : manifest) {
                std::string signature;
                for (const Entry& entry : script.second) {
                    signature += entry.path + ":" + entry.hash + ";";
                    kept.insert(entry.hash);
                }
                names += (names.empty() ? "" : ", ") + script.first;
                if (applied[script.first] == signature && fs::exists(folder / script.first, error)) continue;
                bool fetched = true;
                for (const Entry& entry : script.second) fetched = Fetch(entry) && fetched;
                if (fetched && Apply(script.first, script.second)) applied[script.first] = signature;
                else runtime::Log("could not fetch the script " + script.first + " from " + root);
            }
            for (fs::directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error)) {
                std::string name = entry->path().filename().u8string();
                if (manifest.count(name) || name.empty() || name[0] == '.') continue;
                fs::remove_all(entry->path(), error);
                applied.erase(name);
            }
            for (fs::directory_iterator entry(store, error), end; !error && entry != end; entry.increment(error))
                if (!kept.count(entry->path().filename().u8string())) fs::remove(entry->path(), error);
            if (names != announced) {
                announced = names;
                runtime::Log(names.empty() ? "no scripts from " + root : "scripts from " + root + ": " + names);
            }
        }

        void Run() {
            std::unique_lock<std::mutex> guard(lock);
            while (!stopping) {
                guard.unlock();
                Sync();
                guard.lock();
                wake.wait_for(guard, Interval, [] { return stopping || nudged; });
                nudged = false;
            }
        }
    }

    fs::path Start(const fs::path& cache) {
        root = Locate();
        if (root.empty()) return {};
        folder = cache / fs::u8path(Place(root));
        store = cache / ".files";
        downloads = cache / ".downloads";
        Prune();
        std::error_code error;
        for (fs::directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error))
            fs::remove_all(entry->path(), error);
        fs::create_directories(folder, error);
        std::thread(Run).detach();
        return folder;
    }

    void Refresh() {
        {
            std::lock_guard<std::mutex> guard(lock);
            nudged = true;
        }
        wake.notify_one();
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
        }
        wake.notify_one();
        outbox.notify_all();
    }

    bool Send(const std::string& name, const std::string& data) {
        if (root.empty() || key.empty() || !MessageName(name) || data.size() > MessageLimit) return false;
        Converse();
        {
            std::lock_guard<std::mutex> guard(mail);
            if (outgoing.size() >= QueueLimit) return false;
            outgoing.push_back({name, data});
        }
        outbox.notify_one();
        return true;
    }

    std::vector<Message> Take() {
        std::lock_guard<std::mutex> guard(mail);
        std::vector<Message> taken;
        taken.swap(incoming);
        return taken;
    }

    images::State Want(const std::string& id, fs::path& path) {
        if (root.empty() || key.empty() || !images::Served(id)) return images::State::Failed;
        path = downloads / id;
        std::lock_guard<std::mutex> guard(pulls);
        auto found = wanted.find(id);
        if (found != wanted.end() && (found->second.state != images::State::Failed || std::chrono::steady_clock::now() - found->second.since < RetryAfter))
            return found->second.state;
        if (found == wanted.end() && wanted.size() >= WantedLimit) return images::State::Failed;
        Wanted& entry = wanted[id];
        std::error_code error;
        entry.state = fs::exists(path, error) ? images::State::Ready : images::State::Loading;
        if (entry.state == images::State::Loading) {
            waiting.push_back(id);
            if (pulling < Pullers) {
                pulling++;
                std::thread(Pulling).detach();
            }
        }
        return entry.state;
    }
}
