#include "host.h"
#include "runtime.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dirent.h>
#  include <dlfcn.h>
#endif

namespace okmod {
namespace {
enum Kind { NONE, INT, NUM, STR, OBJECT, PLAYER, VEC3 };
struct Val {
    Kind kind = NONE;
    int64_t i = 0;
    double n = 0;
    std::string s;
    ok_vec3 v{0, 0, 0};
};
}
}

struct ok_call {
    std::vector<okmod::Val> args;
    bool has_result = false;
    okmod::Val result;
    bool cancelled = false;
};

namespace okmod {
namespace {
struct Sub {
    ok_sub id;
    std::string hook;
    ok_handler fn;
    void *user;
};

struct Host {
    std::vector<Sub> subs;
    ok_sub next_sub = 1;
    std::unordered_map<std::string, int> flags;
    std::unordered_map<std::string, double> params;
    std::vector<std::pair<void (*)(void *), void *>> deferred;
    std::mutex defer_lock;
};

Host &host() {
    static Host h;
    return h;
}

const Runtime *&runtime() {
    static const Runtime *r = nullptr;
    return r;
}

void host_log(ok_log_level level, const char *message) {
    const char *tag = level == OK_LOG_ERROR ? "error" : level == OK_LOG_WARN ? "warn" : "info";
    std::fprintf(stderr, "[okmod %s] %s\n", tag, message ? message : "");
    std::fflush(stderr);
#if defined(_WIN32)
    char line[1024];
    std::snprintf(line, sizeof(line), "[okmod %s] %s\n", tag, message ? message : "");
    OutputDebugStringA(line);
#endif
}

int host_has(const char *name) {
    if (!name) return 0;
    bool action = std::strncmp(name, "object.", 7) == 0 || std::strncmp(name, "player.", 7) == 0 || std::strncmp(name, "camera.", 7) == 0;
    return action ? runtime() != nullptr : 1;
}

ok_sub host_on(const char *event, ok_handler fn, void *user) {
    if (!event || !fn) return 0;
    Host &h = host();
    ok_sub id = h.next_sub++;
    h.subs.push_back({id, event, fn, user});
    return id;
}

void host_off(ok_sub sub) {
    Host &h = host();
    for (size_t i = 0; i < h.subs.size(); i++)
        if (h.subs[i].id == sub) { h.subs.erase(h.subs.begin() + i); return; }
}

const Val *arg(ok_call *call, int index) {
    return call && index >= 0 && index < (int)call->args.size() ? &call->args[index] : nullptr;
}

int arg_count(ok_call *call) { return call ? (int)call->args.size() : 0; }
int64_t arg_int(ok_call *call, int index) { const Val *a = arg(call, index); return !a ? 0 : a->kind == NUM ? (int64_t)a->n : a->i; }
double arg_num(ok_call *call, int index) { const Val *a = arg(call, index); return !a ? 0 : a->kind == INT ? (double)a->i : a->n; }
ok_object arg_object(ok_call *call, int index) { const Val *a = arg(call, index); return a ? (ok_object)a->i : 0; }
ok_player arg_player(ok_call *call, int index) { const Val *a = arg(call, index); return a ? (ok_player)a->i : 0; }
ok_vec3 arg_vec3(ok_call *call, int index) { const Val *a = arg(call, index); return a ? a->v : ok_vec3{0, 0, 0}; }

int arg_str(ok_call *call, int index, char *buf, int cap) {
    const Val *a = arg(call, index);
    std::string s = a ? a->s : std::string();
    if (buf && cap > 0) {
        int n = (int)s.size() < cap - 1 ? (int)s.size() : cap - 1;
        std::memcpy(buf, s.data(), n);
        buf[n] = 0;
    }
    return (int)s.size();
}

void set_result_bool(ok_call *call, int value) { if (call) { call->has_result = true; call->result.kind = INT; call->result.i = value ? 1 : 0; } }
void set_result_int(ok_call *call, int64_t value) { if (call) { call->has_result = true; call->result.kind = INT; call->result.i = value; } }
void set_result_num(ok_call *call, double value) { if (call) { call->has_result = true; call->result.kind = NUM; call->result.n = value; } }
void cancel(ok_call *call) { if (call) call->cancelled = true; }

void flag_set(const char *name, int on) { if (name) host().flags[name] = on; }
int flag_get(const char *name) { auto &f = host().flags; auto it = name ? f.find(name) : f.end(); return it == f.end() ? 0 : it->second; }
void param_set_num(const char *name, double v) { if (name) host().params[name] = v; }
double param_get_num(const char *name, double fallback) { auto &p = host().params; auto it = name ? p.find(name) : p.end(); return it == p.end() ? fallback : it->second; }

ok_object object_at(int i) { return runtime() ? runtime()->object_at(i) : 0; }
int object_count() { return runtime() ? runtime()->object_count() : 0; }
ok_vec3 object_position(ok_object o) { return runtime() ? runtime()->object_position(o) : ok_vec3{0, 0, 0}; }
ok_quat object_rotation(ok_object o) { return runtime() ? runtime()->object_rotation(o) : ok_quat{0, 0, 0, 1}; }
void object_set_visible(ok_object o, int v) { if (runtime()) runtime()->object_set_visible(o, v); }
void object_destroy(ok_object o) { if (runtime()) runtime()->object_destroy(o); }
ok_object object_clone(ok_object o) { return runtime() ? runtime()->object_clone(o) : 0; }
ok_player player_local() { return runtime() ? runtime()->player_local() : 0; }
int player_str(ok_player p, const char *field, char *buf, int cap) { return runtime() ? runtime()->player_str(p, field, buf, cap) : 0; }
void player_teleport(ok_player p, ok_vec3 to) { if (runtime()) runtime()->player_teleport(p, to); }
ok_vec3 camera_position() { return runtime() ? runtime()->camera_position() : ok_vec3{0, 0, 0}; }
int64_t call_static_i(const char *type, const char *method) { return runtime() ? runtime()->call_static_i(type, method) : 0; }
ok_object call_static_obj(const char *type, const char *method) { return runtime() ? runtime()->call_static_obj(type, method) : 0; }
int64_t call_obj_i(ok_object self, const char *type, const char *method) { return runtime() ? runtime()->call_obj_i(self, type, method) : 0; }
int64_t invoke(ok_object self, const char *type, const char *method, int argc, const int64_t *args, const char *argtypes, char ret) {
    return runtime() ? runtime()->invoke(self, type, method, argc, args, argtypes, ret) : 0;
}

void defer(void (*fn)(void *), void *user) {
    if (!fn) return;
    Host &h = host();
    std::lock_guard<std::mutex> lock(h.defer_lock);
    h.deferred.push_back({fn, user});
}

void set_arg_int(ok_call *call, int index, int64_t value) {
    if (call && index >= 0 && index < (int)call->args.size()) { call->args[index].kind = INT; call->args[index].i = value; }
}

const OkMod g_api = {
    OKMOD_ABI, (uint32_t)sizeof(OkMod),
    host_log, host_has,
    host_on, host_off,
    arg_count, arg_int, arg_num, arg_str, arg_object, arg_player, arg_vec3,
    set_result_bool, set_result_int, set_result_num, cancel,
    flag_set, flag_get, param_set_num, param_get_num,
    object_at, object_count, object_position, object_rotation, object_set_visible, object_destroy, object_clone,
    player_local, player_str, player_teleport,
    camera_position,
    defer,
    set_arg_int,
    call_static_i,
    call_static_obj,
    call_obj_i,
    invoke,
};

#if defined(_WIN32)
typedef HMODULE Lib;
Lib lib_open(const char *path) { return LoadLibraryA(path); }
void *lib_sym(Lib l, const char *name) { return (void *)GetProcAddress(l, name); }
const char *lib_suffix() { return ".dll"; }
#else
typedef void *Lib;
Lib lib_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
void *lib_sym(Lib l, const char *name) { return dlsym(l, name); }
const char *lib_suffix() { return ".so"; }
#endif

bool ends_with(const std::string &s, const char *suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

int load_one(const std::string &path) {
    Lib lib = lib_open(path.c_str());
    if (!lib) { host_log(OK_LOG_ERROR, (std::string("could not load ") + path).c_str()); return 0; }
    auto abi = (uint32_t(*)())lib_sym(lib, "okmod_abi");
    auto main = (int (*)(const OkMod *))lib_sym(lib, "okmod_main");
    if (!abi || !main) { host_log(OK_LOG_WARN, (path + " is not an okmod mod").c_str()); return 0; }
    if (abi() > OKMOD_ABI) { host_log(OK_LOG_WARN, (path + " needs a newer okmod").c_str()); return 0; }
    if (main(host_api()) != 0) { host_log(OK_LOG_ERROR, (path + " failed to start").c_str()); return 0; }
    host_log(OK_LOG_INFO, (std::string("loaded ") + path).c_str());
    return 1;
}
}

const OkMod *host_api() { return &g_api; }

ok_call *call_begin() { return new ok_call(); }
void call_push_int(ok_call *c, int64_t v) { Val a; a.kind = INT; a.i = v; c->args.push_back(std::move(a)); }
void call_push_num(ok_call *c, double v) { Val a; a.kind = NUM; a.n = v; c->args.push_back(std::move(a)); }
void call_push_str(ok_call *c, const char *v) { Val a; a.kind = STR; a.s = v ? v : ""; c->args.push_back(std::move(a)); }
void call_push_object(ok_call *c, ok_object v) { Val a; a.kind = OBJECT; a.i = (int64_t)v; c->args.push_back(std::move(a)); }
void call_push_player(ok_call *c, ok_player v) { Val a; a.kind = PLAYER; a.i = (int64_t)v; c->args.push_back(std::move(a)); }
void call_push_vec3(ok_call *c, ok_vec3 v) { Val a; a.kind = VEC3; a.v = v; c->args.push_back(std::move(a)); }

void dispatch(const char *hook, ok_call *call) {
    if (!hook) return;
    Host &h = host();
    std::vector<Sub> snapshot = h.subs;
    for (const Sub &sub : snapshot)
        if (sub.hook == hook) sub.fn(call, sub.user);
}

int call_argc(ok_call *c) { return c ? (int)c->args.size() : 0; }
int64_t call_arg_int(ok_call *c, int index) { return arg_int(c, index); }
void call_set_arg_int(ok_call *c, int index, int64_t value) { set_arg_int(c, index, value); }
int call_cancelled(ok_call *c) { return c && c->cancelled ? 1 : 0; }
int64_t call_result_int(ok_call *c, int64_t fallback) { return c && c->has_result ? (c->result.kind == NUM ? (int64_t)c->result.n : c->result.i) : fallback; }
double call_result_num(ok_call *c, double fallback) { return c && c->has_result ? (c->result.kind == INT ? (double)c->result.i : c->result.n) : fallback; }
void call_end(ok_call *c) { delete c; }

int run_gate(const char *hook) {
    ok_call *c = call_begin();
    dispatch(hook, c);
    int cancelled = call_cancelled(c);
    call_end(c);
    return cancelled;
}

int run_event(const char *hook, int64_t *args, int argc, int64_t *result) {
    ok_call *c = call_begin();
    for (int i = 0; i < argc; i++) call_push_int(c, args ? args[i] : 0);
    dispatch(hook, c);
    for (int i = 0; i < argc && args; i++) args[i] = call_arg_int(c, i);
    if (result) *result = call_result_int(c, 0);
    int cancelled = call_cancelled(c);
    call_end(c);
    return cancelled;
}

int control_command(const char *line) {
    if (!line) return 0;
    std::string s(line);
    size_t a = s.find('|');
    if (a == std::string::npos) return 0;
    size_t b = s.find('|', a + 1);
    if (b == std::string::npos) return 0;
    std::string kind = s.substr(0, a);
    std::string name = s.substr(a + 1, b - a - 1);
    std::string value = s.substr(b + 1);
    if (name.empty()) return 0;
    if (kind == "flag") { flag_set(name.c_str(), value == "1" || value == "true" ? 1 : 0); return 1; }
    if (kind == "param") { try { param_set_num(name.c_str(), std::stod(value)); return 1; } catch (...) { return 0; } }
    return 0;
}

void pump() {
    Host &h = host();
    std::vector<std::pair<void (*)(void *), void *>> ready;
    {
        std::lock_guard<std::mutex> lock(h.defer_lock);
        ready.swap(h.deferred);
    }
    for (auto &job : ready) job.first(job.second);
}

void set_runtime(const Runtime *r) { runtime() = r; }

int load_mods(const char *dir) {
    if (!dir) return 0;
    std::string base = dir;
    int loaded = 0;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA((base + "\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        std::string name = fd.cFileName;
        if (ends_with(name, lib_suffix())) loaded += load_one(base + "\\" + name);
    } while (FindNextFileA(find, &fd));
    FindClose(find);
#else
    DIR *d = opendir(base.c_str());
    if (!d) return 0;
    while (struct dirent *e = readdir(d)) {
        std::string name = e->d_name;
        if (ends_with(name, lib_suffix())) loaded += load_one(base + "/" + name);
    }
    closedir(d);
#endif
    return loaded;
}
}
