#include "host.h"
#include "runtime.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace okmod {
namespace {
#if defined(_WIN32)

struct Mono {
    void *(*get_root_domain)();
    void *(*thread_attach)(void *);
    void (*assembly_foreach)(void (*)(void *, void *), void *);
    void *(*assembly_get_image)(void *);
    void *(*class_from_name)(void *, const char *, const char *);
    void *(*class_get_method_from_name)(void *, const char *, int);
    void *(*runtime_invoke)(void *, void *, void **, void **);
    void *(*object_unbox)(void *);
    void *(*class_get_field_from_name)(void *, const char *);
    uint32_t (*field_get_offset)(void *);
    uint32_t (*field_get_flags)(void *);
    void *(*field_get_type)(void *);
    int (*type_get_type)(void *);
    void *(*class_vtable)(void *, void *);
    void (*field_static_get_value)(void *, void *, void *);
    void (*field_static_set_value)(void *, void *, void *);
    void *(*object_get_class)(void *);
    int32_t (*class_is_assignable_from)(void *, void *);
    void *(*class_get_methods)(void *, void **);
    const char *(*method_get_name)(void *);
    void *(*method_signature)(void *);
    uint32_t (*signature_get_param_count)(void *);
    void *(*signature_get_params)(void *, void **);
    void *(*signature_get_return_type)(void *);
    void *(*class_from_mono_type)(void *);
    int32_t (*class_is_enum)(void *);
    void *(*class_get_parent)(void *);
    void *(*class_enum_basetype)(void *);
    void *(*object_get_virtual_method)(void *, void *);
    void *(*string_new)(void *, const char *);
    int32_t (*class_value_size)(void *, uint32_t *);
    int32_t (*class_is_valuetype)(void *);
    void *(*array_new)(void *, void *, uintptr_t);
    uint16_t *(*string_chars)(void *);
    int (*string_length)(void *);
    void *(*class_get_fields)(void *, void **);
    const char *(*field_get_name)(void *);
    int32_t (*type_is_byref)(void *);
    void *(*object_new)(void *, void *);
    int32_t (*array_element_size)(void *);
    void *(*gchandle_get_target)(uint32_t);
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
    TypeMethodParameter = 0x1e,
};

constexpr uint32_t FieldStatic = 0x10;
constexpr size_t ArrayHeader = 4 * sizeof(void *);

struct Field {
    void *owner;
    void *info;
    int kind;
    bool shared;
    size_t offset;
};

Mono mono;
HMODULE g_module = nullptr;
bool g_bound = false;
bool g_ready = false;

template <typename T>
bool bind(T &fn, const char *name) {
    fn = reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(g_module, name)));
    return fn != nullptr;
}

bool ready() {
    if (g_ready) return true;
    if (!g_bound) {
        if (!g_module) return false;
        g_bound = bind(mono.get_root_domain, "mono_get_root_domain")
            && bind(mono.thread_attach, "mono_thread_attach")
            && bind(mono.assembly_foreach, "mono_assembly_foreach")
            && bind(mono.assembly_get_image, "mono_assembly_get_image")
            && bind(mono.class_from_name, "mono_class_from_name")
            && bind(mono.class_get_method_from_name, "mono_class_get_method_from_name")
            && bind(mono.runtime_invoke, "mono_runtime_invoke")
            && bind(mono.object_unbox, "mono_object_unbox")
            && bind(mono.class_get_field_from_name, "mono_class_get_field_from_name")
            && bind(mono.field_get_offset, "mono_field_get_offset")
            && bind(mono.field_get_flags, "mono_field_get_flags")
            && bind(mono.field_get_type, "mono_field_get_type")
            && bind(mono.type_get_type, "mono_type_get_type")
            && bind(mono.class_vtable, "mono_class_vtable")
            && bind(mono.field_static_get_value, "mono_field_static_get_value")
            && bind(mono.field_static_set_value, "mono_field_static_set_value")
            && bind(mono.object_get_class, "mono_object_get_class")
            && bind(mono.class_is_assignable_from, "mono_class_is_assignable_from")
            && bind(mono.class_get_methods, "mono_class_get_methods")
            && bind(mono.method_get_name, "mono_method_get_name")
            && bind(mono.method_signature, "mono_method_signature")
            && bind(mono.signature_get_param_count, "mono_signature_get_param_count")
            && bind(mono.signature_get_params, "mono_signature_get_params")
            && bind(mono.signature_get_return_type, "mono_signature_get_return_type")
            && bind(mono.class_from_mono_type, "mono_class_from_mono_type")
            && bind(mono.class_get_parent, "mono_class_get_parent")
            && bind(mono.object_get_virtual_method, "mono_object_get_virtual_method")
            && bind(mono.string_new, "mono_string_new")
            && bind(mono.class_value_size, "mono_class_value_size")
            && bind(mono.class_is_valuetype, "mono_class_is_valuetype")
            && bind(mono.array_new, "mono_array_new")
            && bind(mono.class_get_fields, "mono_class_get_fields")
            && bind(mono.field_get_name, "mono_field_get_name")
            && bind(mono.object_new, "mono_object_new")
            && bind(mono.gchandle_get_target, "mono_gchandle_get_target");
        if (!g_bound) return false;
        bind(mono.class_is_enum, "mono_class_is_enum");
        bind(mono.class_enum_basetype, "mono_class_enum_basetype");
        bind(mono.type_is_byref, "mono_type_is_byref");
        bind(mono.array_element_size, "mono_array_element_size");
        bind(mono.string_chars, "mono_string_chars");
        bind(mono.string_length, "mono_string_length");
    }
    void *domain = mono.get_root_domain();
    if (!domain) return false;
    mono.thread_attach(domain);
    g_ready = true;
    return true;
}

void *domain() { return mono.get_root_domain(); }

bool is_enum(void *klass) { return klass && mono.class_is_enum && mono.class_is_enum(klass); }

void collect(void *assembly, void *images) {
    if (void *image = mono.assembly_get_image(assembly)) static_cast<std::vector<void *> *>(images)->push_back(image);
}

void *find_class(const char *type) {
    static std::unordered_map<std::string, void *> classes;
    if (!ready() || !type || !*type) return nullptr;
    auto known = classes.find(type);
    if (known != classes.end()) return known->second;
    std::vector<void *> images;
    mono.assembly_foreach(collect, &images);
    std::string full = type;
    std::vector<size_t> dots;
    for (size_t i = 0; i < full.size(); i++)
        if (full[i] == '.') dots.push_back(i);
    void *found = nullptr;
    for (size_t split = dots.size() + 1; split-- > 0 && !found;) {
        std::string space = split == 0 ? std::string() : full.substr(0, dots[split - 1]);
        std::string name = split == 0 ? full : full.substr(dots[split - 1] + 1);
        for (char &c : name)
            if (c == '.') c = '/';
        for (void *image : images)
            if ((found = mono.class_from_name(image, space.c_str(), name.c_str()))) break;
    }
    if (found) classes[type] = found;
    return found;
}

bool accepts(void *parameter, char wanted) {
    int kind = mono.type_get_type(parameter);
    bool byref = mono.type_is_byref && mono.type_is_byref(parameter);
    switch (wanted) {
    case 'i': return !byref && ((kind >= TypeBoolean && kind <= TypeULong) || (kind == TypeValue && is_enum(mono.class_from_mono_type(parameter))));
    case 'f': return !byref && (kind == TypeFloat || kind == TypeDouble);
    case 'o': return !byref && (kind == TypeClass || kind == TypeObject || kind == TypeString || kind == TypeArray || kind == TypeVector || kind == TypeGeneric);
    case 'p': return byref || (kind == TypeValue && !is_enum(mono.class_from_mono_type(parameter)));
    case 's': return !byref && kind == TypeString;
    default: return true;
    }
}

bool generic(void *signature, int args) {
    void *returned = mono.signature_get_return_type(signature);
    if (returned && mono.type_get_type(returned) == TypeMethodParameter) return true;
    void *parameters = nullptr;
    for (int i = 0; i < args; i++) {
        void *parameter = mono.signature_get_params(signature, &parameters);
        if (parameter && mono.type_get_type(parameter) == TypeMethodParameter) return true;
    }
    return false;
}

void *overload(void *klass, const char *method, int args, const char *argtypes) {
    for (; klass; klass = mono.class_get_parent(klass)) {
        void *iterator = nullptr;
        while (void *candidate = mono.class_get_methods(klass, &iterator)) {
            if (std::strcmp(mono.method_get_name(candidate), method) != 0) continue;
            void *signature = mono.method_signature(candidate);
            if (!signature || (int)mono.signature_get_param_count(signature) != args || generic(signature, args)) continue;
            void *parameters = nullptr;
            bool fits = true;
            for (int i = 0; i < args && fits && argtypes; i++) {
                void *parameter = mono.signature_get_params(signature, &parameters);
                fits = parameter && accepts(parameter, argtypes[i]);
            }
            if (fits) return candidate;
        }
    }
    return nullptr;
}

void *named(void *klass, const char *method, int args) { return overload(klass, method, args, nullptr); }

void *virtual_of(ok_object self, void *method) {
    if (!self || !method) return method;
    void *exact = mono.object_get_virtual_method(reinterpret_cast<void *>(self), method);
    return exact ? exact : method;
}

void *class_for(ok_object self, const char *type) {
    if (type && *type) return find_class(type);
    return self && ready() ? mono.object_get_class(reinterpret_cast<void *>(self)) : nullptr;
}

std::string class_key(void *klass) { return std::to_string(reinterpret_cast<uintptr_t>(klass)); }

void *find_method(void *klass, const char *method, int args, const char *argtypes = nullptr) {
    static std::unordered_map<std::string, void *> methods;
    if (!method || !klass) return nullptr;
    bool typed = args > 0 && argtypes && (int)std::strlen(argtypes) >= args;
    std::string key = class_key(klass) + "::" + method + "/" + std::to_string(args) + (typed ? std::string(argtypes, args) : std::string());
    auto known = methods.find(key);
    if (known != methods.end()) return known->second;
    void *found = typed ? overload(klass, method, args, argtypes) : named(klass, method, args);
    methods[key] = found;
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
    void *info = mono.class_get_field_from_name(klass, name);
    if (!info) return false;
    void *declared = mono.field_get_type(info);
    int kind = mono.type_get_type(declared);
    if (kind == TypeValue && mono.class_enum_basetype) {
        void *value = mono.class_from_mono_type(declared);
        if (is_enum(value)) kind = mono.type_get_type(mono.class_enum_basetype(value));
    }
    Field field{klass, info, kind, (mono.field_get_flags(info) & FieldStatic) != 0, mono.field_get_offset(info)};
    fields[key] = field;
    out = field;
    return true;
}

void *vtable_of(void *klass) { return mono.class_vtable(domain(), klass); }

unsigned char *field_data(ok_object self, const Field &field, unsigned char *shared) {
    if (field.shared) {
        void *vtable = vtable_of(field.owner);
        if (!vtable) return nullptr;
        mono.field_static_get_value(vtable, field.info, shared);
        return shared;
    }
    void *object = reinterpret_cast<void *>(self);
    if (!object || !mono.class_is_assignable_from(field.owner, mono.object_get_class(object))) return nullptr;
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
    void *value = mono.object_unbox(result);
    return value ? (int64_t)(*reinterpret_cast<int32_t *>(value)) : 0;
}

int64_t call_static_i(const char *type, const char *method) {
    void *m = find_method(find_class(type), method, 0);
    if (!m) return 0;
    void *exc = nullptr;
    void *result = mono.runtime_invoke(m, nullptr, nullptr, &exc);
    return exc ? 0 : unbox_i(result);
}

ok_object call_static_obj(const char *type, const char *method) {
    void *m = find_method(find_class(type), method, 0);
    if (!m) return 0;
    void *exc = nullptr;
    void *result = mono.runtime_invoke(m, nullptr, nullptr, &exc);
    return exc ? 0 : reinterpret_cast<ok_object>(result);
}

int64_t call_obj_i(ok_object self, const char *type, const char *method) {
    void *m = virtual_of(self, find_method(class_for(self, type), method, 0));
    if (!m || !self) return 0;
    void *exc = nullptr;
    void *result = mono.runtime_invoke(m, reinterpret_cast<void *>(self), nullptr, &exc);
    return exc ? 0 : unbox_i(result);
}

int64_t run(ok_object self, void *m, int argc, const int64_t *args, const char *argtypes, char ret) {
    if (!m) return 0;
    void *params[16];
    int32_t ints[16];
    float floats[16];
    for (int i = 0; i < argc; i++) {
        char t = argtypes ? argtypes[i] : 'i';
        if (t == 'o' || t == 'p') { params[i] = reinterpret_cast<void *>(static_cast<uintptr_t>(args[i])); }
        else if (t == 's') { params[i] = mono.string_new(domain(), reinterpret_cast<const char *>(static_cast<uintptr_t>(args[i]))); }
        else if (t == 'f') { int32_t bits = (int32_t)args[i]; std::memcpy(&floats[i], &bits, 4); params[i] = &floats[i]; }
        else { ints[i] = (int32_t)args[i]; params[i] = &ints[i]; }
    }
    void *exc = nullptr;
    void *result = mono.runtime_invoke(m, self ? reinterpret_cast<void *>(self) : nullptr, argc ? params : nullptr, &exc);
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
    return reinterpret_cast<ok_object>(mono.array_new(domain(), klass, static_cast<uintptr_t>(length)));
}

int array_length(ok_object array) {
    if (!array || !ready()) return 0;
    return static_cast<int>(load<uintptr_t>(reinterpret_cast<unsigned char *>(array) + 3 * sizeof(void *)));
}

void *array_data(ok_object array) {
    return array ? reinterpret_cast<unsigned char *>(array) + ArrayHeader : nullptr;
}

int is_a(ok_object obj, const char *type) {
    void *klass = find_class(type);
    return obj && klass && mono.class_is_assignable_from(klass, mono.object_get_class(reinterpret_cast<void *>(obj))) ? 1 : 0;
}

int string_utf8(ok_object str, char *buf, int cap) {
    if (!str || !ready()) return 0;
    const unsigned char *header = reinterpret_cast<const unsigned char *>(str) + 2 * sizeof(void *);
    int32_t length = mono.string_length ? mono.string_length(reinterpret_cast<void *>(str)) : load<int32_t>(header);
    const wchar_t *chars = mono.string_chars ? reinterpret_cast<const wchar_t *>(mono.string_chars(reinterpret_cast<void *>(str)))
                                             : reinterpret_cast<const wchar_t *>(header + sizeof(int32_t));
    if (length < 0 || length > (1 << 24)) return 0;
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
    void *object = klass ? mono.object_new(domain(), klass) : nullptr;
    void *constructor = object ? find_method(klass, ".ctor", argc, argtypes) : nullptr;
    if (!object || (argc > 0 && !constructor)) return 0;
    if (constructor) run(reinterpret_cast<ok_object>(object), constructor, argc, args, argtypes, 'v');
    return reinterpret_cast<ok_object>(object);
}

int array_stride(ok_object array) {
    if (!array || !ready() || !mono.array_element_size) return 0;
    return mono.array_element_size(mono.object_get_class(reinterpret_cast<void *>(array)));
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
    if (!is_enum(klass) || !mono.class_enum_basetype) return 0;
    void *vtable = vtable_of(klass);
    if (!vtable) return 0;
    int kind = mono.type_get_type(mono.class_enum_basetype(klass));
    void *iterator = nullptr;
    while (void *field = mono.class_get_fields(klass, &iterator)) {
        if (!(mono.field_get_flags(field) & FieldStatic)) continue;
        unsigned char data[8] = {};
        mono.field_static_get_value(vtable, field, data);
        if (widen(kind, data) != value) continue;
        const char *name = mono.field_get_name(field);
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
    void *klass = mono.object_get_class(object);
    if (!klass || !mono.class_is_valuetype(klass)) return 0;
    uint32_t align = 0;
    int length = mono.class_value_size(klass, &align);
    void *data = mono.object_unbox(object);
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
    if (field.shared) {
        void *vtable = vtable_of(field.owner);
        if (!vtable) return 0;
        mono.field_static_set_value(vtable, field.info, shared);
    }
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

bool attach_mono() {
#if defined(_WIN32)
    for (const char *name : {"mono-2.0-bdwgc.dll", "mono.dll", "mono-2.0-sgen.dll"})
        if ((g_module = GetModuleHandleA(name))) break;
    if (!g_module) return false;
    set_runtime(&g_runtime);
    return true;
#else
    return false;
#endif
}

int64_t runtime_address(void *handle) {
#if defined(_WIN32)
    if (!handle || !ready()) return 0;
    return reinterpret_cast<int64_t>(mono.gchandle_get_target(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle))));
#else
    (void)handle;
    return 0;
#endif
}
}
