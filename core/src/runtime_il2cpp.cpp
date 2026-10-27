#include "host.h"
#include "runtime.h"

#include <cstring>
#include <string>
#include <unordered_map>

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
    void *(*class_get_field_from_name)(void *, const char *);
    size_t (*field_get_offset)(void *);
    int (*field_get_flags)(void *);
    const void *(*field_get_type)(void *);
    int (*type_get_type)(const void *);
    void (*field_static_get_value)(void *, void *);
    void (*field_static_set_value)(void *, void *);
    void *(*object_get_class)(void *);
    bool (*class_is_assignable_from)(void *, void *);
    void *(*class_get_methods)(void *, void **);
    const char *(*method_get_name)(void *);
    uint32_t (*method_get_param_count)(void *);
    const void *(*method_get_param)(void *, uint32_t);
    void *(*class_from_type)(const void *);
    bool (*class_is_enum)(void *);
    void *(*class_get_parent)(void *);
    const void *(*class_enum_basetype)(void *);
    void *(*object_get_virtual_method)(void *, void *);
    void *(*string_new)(const char *);
    int32_t (*class_value_size)(void *, uint32_t *);
    bool (*class_is_valuetype)(const void *);
    void *(*array_new)(void *, uintptr_t);
    uint32_t (*array_length)(void *);
    const uint16_t *(*string_chars)(void *);
    int32_t (*string_length)(void *);
    void *(*class_get_fields)(void *, void **);
    const char *(*field_get_name)(void *);
    bool (*type_is_byref)(const void *);
    bool (*method_is_generic)(const void *);
    void *(*object_new)(void *);
    int (*array_element_size)(const void *);
};

enum : int {
    TypeBoolean = 0x02,
    TypeSByte = 0x04,
    TypeByte = 0x05,
    TypeShort = 0x06,
    TypeUShort = 0x07,
    TypeInt = 0x08,
    TypeUInt = 0x09,
    TypeLong = 0x0a,
    TypeULong = 0x0b,
    TypeFloat = 0x0c,
    TypeDouble = 0x0d,
    TypeString = 0x0e,
    TypeValue = 0x11,
    TypeClass = 0x12,
    TypeArray = 0x14,
    TypeGeneric = 0x15,
    TypeObject = 0x1c,
    TypeVector = 0x1d,
};

constexpr int FieldStatic = 0x10;
constexpr size_t ArrayHeader = 4 * sizeof(void *);

struct Field {
    void *owner;
    void *info;
    int kind;
    bool shared;
    size_t offset;
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
            && bind(h, il2.thread_attach, "il2cpp_thread_attach")
            && bind(h, il2.class_get_field_from_name, "il2cpp_class_get_field_from_name")
            && bind(h, il2.field_get_offset, "il2cpp_field_get_offset")
            && bind(h, il2.field_get_flags, "il2cpp_field_get_flags")
            && bind(h, il2.field_get_type, "il2cpp_field_get_type")
            && bind(h, il2.type_get_type, "il2cpp_type_get_type")
            && bind(h, il2.field_static_get_value, "il2cpp_field_static_get_value")
            && bind(h, il2.field_static_set_value, "il2cpp_field_static_set_value")
            && bind(h, il2.object_get_class, "il2cpp_object_get_class")
            && bind(h, il2.class_is_assignable_from, "il2cpp_class_is_assignable_from")
            && bind(h, il2.class_get_methods, "il2cpp_class_get_methods")
            && bind(h, il2.method_get_name, "il2cpp_method_get_name")
            && bind(h, il2.method_get_param_count, "il2cpp_method_get_param_count")
            && bind(h, il2.method_get_param, "il2cpp_method_get_param")
            && bind(h, il2.class_from_type, "il2cpp_class_from_type")
            && bind(h, il2.class_is_enum, "il2cpp_class_is_enum")
            && bind(h, il2.class_get_parent, "il2cpp_class_get_parent")
            && bind(h, il2.class_enum_basetype, "il2cpp_class_enum_basetype")
            && bind(h, il2.object_get_virtual_method, "il2cpp_object_get_virtual_method")
            && bind(h, il2.string_new, "il2cpp_string_new")
            && bind(h, il2.class_value_size, "il2cpp_class_value_size")
            && bind(h, il2.class_is_valuetype, "il2cpp_class_is_valuetype")
            && bind(h, il2.array_new, "il2cpp_array_new")
            && bind(h, il2.array_length, "il2cpp_array_length")
            && bind(h, il2.string_chars, "il2cpp_string_chars")
            && bind(h, il2.string_length, "il2cpp_string_length")
            && bind(h, il2.class_get_fields, "il2cpp_class_get_fields")
            && bind(h, il2.field_get_name, "il2cpp_field_get_name")
            && bind(h, il2.type_is_byref, "il2cpp_type_is_byref")
            && bind(h, il2.method_is_generic, "il2cpp_method_is_generic")
            && bind(h, il2.object_new, "il2cpp_object_new")
            && bind(h, il2.array_element_size, "il2cpp_array_element_size");
        if (!g_bound) return false;
    }
    void *domain = il2.domain_get();
    if (!domain) return false;
    il2.thread_attach(domain);
    g_ready = true;
    return true;
}

void *find_class(const char *type) {
    static std::unordered_map<std::string, void *> classes;
    if (!ready() || !type) return nullptr;
    auto known = classes.find(type);
    if (known != classes.end()) return known->second;
    std::string full = type;
    size_t dot = full.rfind('.');
    std::string space = dot == std::string::npos ? std::string() : full.substr(0, dot);
    std::string name = dot == std::string::npos ? full : full.substr(dot + 1);
    size_t count = 0;
    void **assemblies = il2.domain_get_assemblies(il2.domain_get(), &count);
    void *found = nullptr;
    for (size_t i = 0; i < count && !found; i++) {
        void *image = il2.assembly_get_image(assemblies[i]);
        found = il2.class_from_name(image, space.c_str(), name.c_str());
        if (!found && dot != std::string::npos) found = il2.class_from_name(image, "", type);
    }
    if (found) classes[type] = found;
    return found;
}

bool accepts(const void *parameter, char wanted) {
    int kind = il2.type_get_type(parameter);
    switch (wanted) {
    case 'i': return (kind >= TypeBoolean && kind <= TypeULong) || (kind == TypeValue && il2.class_is_enum(il2.class_from_type(parameter)));
    case 'f': return kind == TypeFloat || kind == TypeDouble;
    case 'o': return kind == TypeClass || kind == TypeObject || kind == TypeString || kind == TypeArray || kind == TypeVector || kind == TypeGeneric;
    case 'p': return il2.type_is_byref(parameter) || (kind == TypeValue && !il2.class_is_enum(il2.class_from_type(parameter)));
    case 's': return kind == TypeString;
    default: return true;
    }
}

void *overload(void *klass, const char *method, int args, const char *argtypes) {
    for (; klass; klass = il2.class_get_parent(klass)) {
        void *iterator = nullptr;
        while (void *candidate = il2.class_get_methods(klass, &iterator)) {
            if (std::strcmp(il2.method_get_name(candidate), method) != 0 || (int)il2.method_get_param_count(candidate) != args) continue;
            if (il2.method_is_generic(candidate)) continue;
            bool fits = true;
            for (int i = 0; i < args && fits; i++) fits = accepts(il2.method_get_param(candidate, i), argtypes[i]);
            if (fits) return candidate;
        }
    }
    return nullptr;
}

void *virtual_of(ok_object self, void *method) {
    if (!self || !method) return method;
    void *exact = il2.object_get_virtual_method(reinterpret_cast<void *>(self), method);
    return exact ? exact : method;
}

void *class_for(ok_object self, const char *type) {
    if (type && *type) return find_class(type);
    return self && ready() ? il2.object_get_class(reinterpret_cast<void *>(self)) : nullptr;
}

std::string class_key(void *klass) { return std::to_string(reinterpret_cast<uintptr_t>(klass)); }

void *find_method(void *klass, const char *method, int args, const char *argtypes = nullptr) {
    static std::unordered_map<std::string, void *> methods;
    if (!method || !klass) return nullptr;
    bool typed = args > 0 && argtypes && (int)std::strlen(argtypes) >= args;
    std::string key = class_key(klass) + "::" + method + "/" + std::to_string(args) + (typed ? std::string(argtypes, args) : std::string());
    auto known = methods.find(key);
    if (known != methods.end()) return known->second;
    void *found = typed ? overload(klass, method, args, argtypes) : il2.class_get_method_from_name(klass, method, args);
    if (found) methods[key] = found;
    return found;
}

bool find_field(void *klass, const char *name, Field &out) {
    static std::unordered_map<std::string, Field> fields;
    if (!name || !klass) return false;
    std::string key = class_key(klass) + "::" + name;
    auto known = fields.find(key);
    if (known != fields.end()) {
        out = known->second;
        return true;
    }
    void *info = il2.class_get_field_from_name(klass, name);
    if (!info) return false;
    const void *declared = il2.field_get_type(info);
    int kind = il2.type_get_type(declared);
    if (kind == TypeValue) {
        void *value = il2.class_from_type(declared);
        if (value && il2.class_is_enum(value)) kind = il2.type_get_type(il2.class_enum_basetype(value));
    }
    Field field{klass, info, kind, (il2.field_get_flags(info) & FieldStatic) != 0, il2.field_get_offset(info)};
    fields[key] = field;
    out = field;
    return true;
}

unsigned char *field_data(ok_object self, const Field &field, unsigned char *shared) {
    if (field.shared) {
        il2.field_static_get_value(field.info, shared);
        return shared;
    }
    void *object = reinterpret_cast<void *>(self);
    if (!object || !il2.class_is_assignable_from(field.owner, il2.object_get_class(object))) return nullptr;
    return static_cast<unsigned char *>(object) + field.offset;
}

template <typename T>
T load(const unsigned char *data) {
    T value;
    std::memcpy(&value, data, sizeof(T));
    return value;
}

template <typename T>
void store(unsigned char *data, double value) {
    T typed = static_cast<T>(value);
    std::memcpy(data, &typed, sizeof(T));
}

int64_t unbox_i(void *result) {
    if (!result) return 0;
    void *value = il2.object_unbox(result);
    return value ? (int64_t)(*reinterpret_cast<int32_t *>(value)) : 0;
}

int64_t call_static_i(const char *type, const char *method) {
    void *m = find_method(find_class(type), method, 0);
    if (!m) return 0;
    void *exc = nullptr;
    return unbox_i(il2.runtime_invoke(m, nullptr, nullptr, &exc));
}

ok_object call_static_obj(const char *type, const char *method) {
    void *m = find_method(find_class(type), method, 0);
    if (!m) return 0;
    void *exc = nullptr;
    void *result = il2.runtime_invoke(m, nullptr, nullptr, &exc);
    return exc ? 0 : reinterpret_cast<ok_object>(result);
}

int64_t call_obj_i(ok_object self, const char *type, const char *method) {
    void *m = virtual_of(self, find_method(class_for(self, type), method, 0));
    if (!m || !self) return 0;
    void *exc = nullptr;
    return unbox_i(il2.runtime_invoke(m, reinterpret_cast<void *>(self), nullptr, &exc));
}

int64_t run(ok_object self, void *m, int argc, const int64_t *args, const char *argtypes, char ret) {
    if (!m) return 0;
    void *params[16];
    int32_t ints[16];
    float floats[16];
    for (int i = 0; i < argc; i++) {
        char t = argtypes ? argtypes[i] : 'i';
        if (t == 'o' || t == 'p') { params[i] = reinterpret_cast<void *>(static_cast<uintptr_t>(args[i])); }
        else if (t == 's') { params[i] = il2.string_new(reinterpret_cast<const char *>(static_cast<uintptr_t>(args[i]))); }
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

int64_t invoke(ok_object self, const char *type, const char *method, int argc, const int64_t *args, const char *argtypes, char ret) {
    if (argc < 0 || argc > 16) return 0;
    return run(self, virtual_of(self, find_method(class_for(self, type), method, argc, argtypes)), argc, args, argtypes, ret);
}

int64_t invoke_exact(ok_object self, const char *type, const char *method, int argc, const int64_t *args, const char *argtypes, char ret) {
    if (argc < 0 || argc > 16) return 0;
    return run(self, find_method(class_for(self, type), method, argc, argtypes), argc, args, argtypes, ret);
}

ok_object array_new(const char *element_type, int length) {
    void *klass = find_class(element_type);
    if (!klass || length < 0) return 0;
    return reinterpret_cast<ok_object>(il2.array_new(klass, static_cast<uintptr_t>(length)));
}

int array_length(ok_object array) {
    return array && ready() ? static_cast<int>(il2.array_length(reinterpret_cast<void *>(array))) : 0;
}

void *array_data(ok_object array) {
    return array ? reinterpret_cast<unsigned char *>(array) + ArrayHeader : nullptr;
}

int is_a(ok_object obj, const char *type) {
    void *klass = find_class(type);
    return obj && klass && il2.class_is_assignable_from(klass, il2.object_get_class(reinterpret_cast<void *>(obj))) ? 1 : 0;
}

int string_utf8(ok_object str, char *buf, int cap) {
    if (!str || !ready()) return 0;
    const wchar_t *chars = reinterpret_cast<const wchar_t *>(il2.string_chars(reinterpret_cast<void *>(str)));
    int length = il2.string_length(reinterpret_cast<void *>(str));
    int needed = WideCharToMultiByte(CP_UTF8, 0, chars, length, nullptr, 0, nullptr, nullptr);
    if (!buf || cap <= 0 || needed <= 0) return needed;
    std::string text(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, chars, length, text.data(), needed, nullptr, nullptr);
    int copied = needed < cap - 1 ? needed : cap - 1;
    std::memcpy(buf, text.data(), copied);
    buf[copied] = 0;
    return needed;
}

ok_object new_object(const char *type, int argc, const int64_t *args, const char *argtypes) {
    if (argc < 0 || argc > 16) return 0;
    void *klass = find_class(type);
    void *object = klass ? il2.object_new(klass) : nullptr;
    void *constructor = object ? find_method(klass, ".ctor", argc, argtypes) : nullptr;
    if (!object || (argc > 0 && !constructor)) return 0;
    if (constructor) run(reinterpret_cast<ok_object>(object), constructor, argc, args, argtypes, 'v');
    return reinterpret_cast<ok_object>(object);
}

int array_stride(ok_object array) {
    return array && ready() ? il2.array_element_size(il2.object_get_class(reinterpret_cast<void *>(array))) : 0;
}

int64_t widen(int kind, const unsigned char *data) {
    switch (kind) {
    case TypeBoolean:
    case TypeByte: return load<uint8_t>(data);
    case TypeSByte: return load<int8_t>(data);
    case TypeShort: return load<int16_t>(data);
    case TypeUShort: return load<uint16_t>(data);
    case TypeInt: return load<int32_t>(data);
    case TypeUInt: return load<uint32_t>(data);
    default: return load<int64_t>(data);
    }
}

int enum_name(const char *type, int64_t value, char *buf, int cap) {
    void *klass = find_class(type);
    if (!klass || !il2.class_is_enum(klass)) return 0;
    int kind = il2.type_get_type(il2.class_enum_basetype(klass));
    void *iterator = nullptr;
    while (void *field = il2.class_get_fields(klass, &iterator)) {
        if (!(il2.field_get_flags(field) & FieldStatic)) continue;
        unsigned char data[8] = {};
        il2.field_static_get_value(field, data);
        if (widen(kind, data) != value) continue;
        const char *name = il2.field_get_name(field);
        int length = static_cast<int>(std::strlen(name));
        if (buf && cap > 0) {
            int copied = length < cap - 1 ? length : cap - 1;
            std::memcpy(buf, name, copied);
            buf[copied] = 0;
        }
        return length;
    }
    return 0;
}

int unbox(ok_object boxed, void *out, int size) {
    void *object = reinterpret_cast<void *>(boxed);
    if (!ready() || !object || !out || size <= 0) return 0;
    void *klass = il2.object_get_class(object);
    if (!klass || !il2.class_is_valuetype(klass)) return 0;
    int length = il2.class_value_size(klass, nullptr);
    void *data = il2.object_unbox(object);
    if (!data || length <= 0) return 0;
    int copied = size < length ? size : length;
    std::memcpy(out, data, copied);
    return copied;
}

double field_num(ok_object self, const char *type, const char *name, double fallback) {
    Field field;
    unsigned char shared[8] = {};
    if (!find_field(class_for(self, type), name, field)) return fallback;
    const unsigned char *data = field_data(self, field, shared);
    if (!data) return fallback;
    switch (field.kind) {
    case TypeBoolean:
    case TypeByte: return load<uint8_t>(data);
    case TypeSByte: return load<int8_t>(data);
    case TypeShort: return load<int16_t>(data);
    case TypeUShort: return load<uint16_t>(data);
    case TypeInt: return load<int32_t>(data);
    case TypeUInt: return load<uint32_t>(data);
    case TypeLong: return static_cast<double>(load<int64_t>(data));
    case TypeULong: return static_cast<double>(load<uint64_t>(data));
    case TypeFloat: return load<float>(data);
    case TypeDouble: return load<double>(data);
    default: return fallback;
    }
}

int set_field_num(ok_object self, const char *type, const char *name, double value) {
    Field field;
    unsigned char shared[8] = {};
    if (!find_field(class_for(self, type), name, field)) return 0;
    unsigned char *data = field_data(self, field, shared);
    if (!data) return 0;
    switch (field.kind) {
    case TypeBoolean: store<uint8_t>(data, value != 0 ? 1 : 0); break;
    case TypeByte: store<uint8_t>(data, value); break;
    case TypeSByte: store<int8_t>(data, value); break;
    case TypeShort: store<int16_t>(data, value); break;
    case TypeUShort: store<uint16_t>(data, value); break;
    case TypeInt: store<int32_t>(data, value); break;
    case TypeUInt: store<uint32_t>(data, value); break;
    case TypeLong: store<int64_t>(data, value); break;
    case TypeULong: store<uint64_t>(data, value); break;
    case TypeFloat: store<float>(data, value); break;
    case TypeDouble: store<double>(data, value); break;
    default: return 0;
    }
    if (field.shared) il2.field_static_set_value(field.info, shared);
    return 1;
}

ok_object field_obj(ok_object self, const char *type, const char *name) {
    Field field;
    unsigned char shared[8] = {};
    if (!find_field(class_for(self, type), name, field)) return 0;
    bool reference = field.kind == TypeClass || field.kind == TypeObject || field.kind == TypeString
        || field.kind == TypeArray || field.kind == TypeVector || field.kind == TypeGeneric;
    const unsigned char *data = reference ? field_data(self, field, shared) : nullptr;
    return data ? reinterpret_cast<ok_object>(load<void *>(data)) : 0;
}

void *field_ptr(ok_object self, const char *type, const char *name) {
    Field field;
    unsigned char shared[8] = {};
    if (!find_field(class_for(self, type), name, field) || field.shared) return nullptr;
    return field_data(self, field, shared);
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
    field_num,
    set_field_num,
    field_obj,
    field_ptr,
    unbox,
    invoke_exact,
    array_new,
    array_length,
    array_data,
    is_a,
    string_utf8,
    enum_name,
    new_object,
    array_stride,
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
