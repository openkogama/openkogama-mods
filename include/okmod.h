#ifndef OKMOD_H
#define OKMOD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OKMOD_ABI 1

typedef uint64_t ok_object;
typedef uint64_t ok_player;
typedef uint64_t ok_sub;

typedef struct { float x, y, z; } ok_vec3;
typedef struct { float x, y, z, w; } ok_quat;

typedef enum {
    OK_TRUST_SAFE  = 0,
    OK_TRUST_LOCAL = 1
} ok_trust;

typedef enum {
    OK_LOG_INFO  = 0,
    OK_LOG_WARN  = 1,
    OK_LOG_ERROR = 2
} ok_log_level;

typedef struct ok_call ok_call;
typedef void (*ok_handler)(ok_call *call, void *user);

typedef struct OkMod {
    uint32_t abi;
    uint32_t size;

    void (*log)(ok_log_level level, const char *message);
    int (*has)(const char *name);

    ok_sub (*on)(const char *event, ok_handler fn, void *user);
    void (*off)(ok_sub sub);

    int      (*arg_count)(ok_call *call);
    int64_t  (*arg_int)(ok_call *call, int index);
    double   (*arg_num)(ok_call *call, int index);
    int      (*arg_str)(ok_call *call, int index, char *buf, int cap);
    ok_object(*arg_object)(ok_call *call, int index);
    ok_player(*arg_player)(ok_call *call, int index);
    ok_vec3  (*arg_vec3)(ok_call *call, int index);

    void (*set_result_bool)(ok_call *call, int value);
    void (*set_result_int)(ok_call *call, int64_t value);
    void (*set_result_num)(ok_call *call, double value);
    void (*cancel)(ok_call *call);

    void   (*flag_set)(const char *name, int on);
    int    (*flag_get)(const char *name);
    void   (*param_set_num)(const char *name, double value);
    double (*param_get_num)(const char *name, double fallback);

    ok_object (*object_at)(int index);
    int       (*object_count)(void);
    ok_vec3   (*object_position)(ok_object obj);
    ok_quat   (*object_rotation)(ok_object obj);
    void      (*object_set_visible)(ok_object obj, int visible);
    void      (*object_destroy)(ok_object obj);
    ok_object (*object_clone)(ok_object obj);

    ok_player (*player_local)(void);
    int       (*player_str)(ok_player p, const char *field, char *buf, int cap);
    void      (*player_teleport)(ok_player p, ok_vec3 to);

    ok_vec3 (*camera_position)(void);

    void (*defer)(void (*fn)(void *user), void *user);
    void (*set_arg_int)(ok_call *call, int index, int64_t value);

    int64_t   (*call_static_i)(const char *type, const char *method);
    ok_object (*call_static_obj)(const char *type, const char *method);
    int64_t   (*call_obj_i)(ok_object self, const char *type, const char *method);

    int64_t (*invoke)(ok_object self, const char *type, const char *method,
                      int argc, const int64_t *args, const char *argtypes, char ret);

    double (*field_num)(ok_object self, const char *type, const char *field, double fallback);
    int    (*set_field_num)(ok_object self, const char *type, const char *field, double value);
    ok_object (*field_obj)(ok_object self, const char *type, const char *field);

    void *(*field_ptr)(ok_object self, const char *type, const char *field);
    int (*unbox)(ok_object boxed, void *out, int size);
    int64_t (*invoke_exact)(ok_object self, const char *type, const char *method,
                            int argc, const int64_t *args, const char *argtypes, char ret);

    ok_object (*array_new)(const char *element_type, int length);
    int (*array_length)(ok_object array);
    void *(*array_data)(ok_object array);
    int (*is_a)(ok_object obj, const char *type);
    int (*string_utf8)(ok_object str, char *buf, int cap);
    int (*enum_name)(const char *type, int64_t value, char *buf, int cap);
    ok_object (*new_object)(const char *type, int argc, const int64_t *args, const char *argtypes);
    int (*array_stride)(ok_object array);
} OkMod;

#define OKMOD_MOD(main_fn)                                     \
    OKMOD_EXPORT uint32_t okmod_abi(void) { return OKMOD_ABI; } \
    OKMOD_EXPORT int okmod_main(const OkMod *api) { return (main_fn)(api); }

#if defined(_WIN32)
#  define OKMOD_EXPORT __declspec(dllexport)
#else
#  define OKMOD_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif

#endif
