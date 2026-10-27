#include "host.h"
#include "runtime.h"

#include <cstring>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace okmod {
namespace {
#if defined(_WIN32)

struct Il2 {
    void *(*domain_get)();
    void **(*domain_get_assemblies)(void *, size_t *);
    void *(*assembly_get_image)(void *);
    void *(*class_from_name)(void *, const char *, const char *);
    void *(*class_get_method_from_name)(void *, const char *, int);
    void *(*runtime_invoke)(void *, void *, void **, void **);
    void *(*object_unbox)(void *);
    void *(*thread_attach)(void *);
};

Il2 il2;
bool g_bound = false;
bool g_ready = false;

template <typename T>
bool bind(HMODULE h, T &fn, const char *name) {
    fn = reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(h, name)));
    return fn != nullptr;
}

bool ready() {
    if (g_ready) return true;
    if (!g_bound) {
        HMODULE h = GetModuleHandleA("GameAssembly.dll");
        if (!h) return false;
        g_bound = bind(h, il2.domain_get, "il2cpp_domain_get")
            && bind(h, il2.domain_get_assemblies, "il2cpp_domain_get_assemblies")
            && bind(h, il2.assembly_get_image, "il2cpp_assembly_get_image")
            && bind(h, il2.class_from_name, "il2cpp_class_from_name")
            && bind(h, il2.class_get_method_from_name, "il2cpp_class_get_method_from_name")
            && bind(h, il2.runtime_invoke, "il2cpp_runtime_invoke")
            && bind(h, il2.object_unbox, "il2cpp_object_unbox")
            && bind(h, il2.thread_attach, "il2cpp_thread_attach");
        if (!g_bound) return false;
    }
    void *domain = il2.domain_get();
    if (!domain) return false;
    il2.thread_attach(domain);
    g_ready = true;
    return true;
}

void *find_method(const char *type, const char *method, int args) {
    if (!ready()) return nullptr;
    size_t count = 0;
    void **assemblies = il2.domain_get_assemblies(il2.domain_get(), &count);
    for (size_t i = 0; i < count; i++) {
        void *image = il2.assembly_get_image(assemblies[i]);
        if (void *klass = il2.class_from_name(image, "", type))
            if (void *m = il2.class_get_method_from_name(klass, method, args))
                return m;
    }
    return nullptr;
}

int64_t unbox_i(void *result) {
    if (!result) return 0;
    void *value = il2.object_unbox(result);
    return value ? (int64_t)(*reinterpret_cast<int32_t *>(value)) : 0;
}

int64_t call_static_i(const char *type, const char *method) {
    void *m = find_method(type, method, 0);
    if (!m) return 0;
    void *exc = nullptr;
    return unbox_i(il2.runtime_invoke(m, nullptr, nullptr, &exc));
}

ok_object call_static_obj(const char *type, const char *method) {
    void *m = find_method(type, method, 0);
    if (!m) return 0;
    void *exc = nullptr;
    void *result = il2.runtime_invoke(m, nullptr, nullptr, &exc);
    return exc ? 0 : reinterpret_cast<ok_object>(result);
}

int64_t call_obj_i(ok_object self, const char *type, const char *method) {
    void *m = find_method(type, method, 0);
    if (!m || !self) return 0;
    void *exc = nullptr;
    return unbox_i(il2.runtime_invoke(m, reinterpret_cast<void *>(self), nullptr, &exc));
}

int64_t invoke(ok_object self, const char *type, const char *method, int argc, const int64_t *args, const char *argtypes, char ret) {
    if (argc < 0 || argc > 16) return 0;
    void *m = find_method(type, method, argc);
    if (!m) return 0;
    void *params[16];
    int32_t ints[16];
    float floats[16];
    for (int i = 0; i < argc; i++) {
        char t = argtypes ? argtypes[i] : 'i';
        if (t == 'o') { params[i] = reinterpret_cast<void *>(static_cast<uintptr_t>(args[i])); }
        else if (t == 'f') { int32_t bits = (int32_t)args[i]; memcpy(&floats[i], &bits, 4); params[i] = &floats[i]; }
        else { ints[i] = (int32_t)args[i]; params[i] = &ints[i]; }
    }
    void *exc = nullptr;
    void *result = il2.runtime_invoke(m, self ? reinterpret_cast<void *>(self) : nullptr, argc ? params : nullptr, &exc);
    if (exc) return 0;
    if (ret == 'o') return reinterpret_cast<int64_t>(result);
    if (ret == 'v') return 0;
    return unbox_i(result);
}

ok_object object_at(int) { return 0; }
int object_count() { return 0; }
ok_vec3 object_position(ok_object) { return {0, 0, 0}; }
ok_quat object_rotation(ok_object) { return {0, 0, 0, 1}; }
void object_set_visible(ok_object, int) {}
void object_destroy(ok_object) {}
ok_object object_clone(ok_object) { return 0; }
ok_player player_local() { return 0; }
int player_str(ok_player, const char *, char *, int) { return 0; }
void player_teleport(ok_player, ok_vec3) {}
ok_vec3 camera_position() { return {0, 0, 0}; }

const Runtime g_runtime = {
    object_at, object_count, object_position, object_rotation, object_set_visible, object_destroy, object_clone,
    player_local, player_str, player_teleport,
    camera_position,
    call_static_i,
    call_static_obj,
    call_obj_i,
    invoke,
};

#endif
}

void runtime_attach() {
#if defined(_WIN32)
    if (!GetModuleHandleA("GameAssembly.dll")) return;
    set_runtime(&g_runtime);
#endif
}
}
