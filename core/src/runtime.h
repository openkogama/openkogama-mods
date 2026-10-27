#ifndef OKMOD_RUNTIME_H
#define OKMOD_RUNTIME_H

#include "okmod.h"

namespace okmod {
struct Runtime {
    ok_object (*object_at)(int index);
    int (*object_count)();
    ok_vec3 (*object_position)(ok_object obj);
    ok_quat (*object_rotation)(ok_object obj);
    void (*object_set_visible)(ok_object obj, int visible);
    void (*object_destroy)(ok_object obj);
    ok_object (*object_clone)(ok_object obj);
    ok_player (*player_local)();
    int (*player_str)(ok_player p, const char *field, char *buf, int cap);
    void (*player_teleport)(ok_player p, ok_vec3 to);
    ok_vec3 (*camera_position)();
    int64_t (*call_static_i)(const char *type, const char *method);
    ok_object (*call_static_obj)(const char *type, const char *method);
    int64_t (*call_obj_i)(ok_object self, const char *type, const char *method);
    int64_t (*invoke)(ok_object self, const char *type, const char *method,
                      int argc, const int64_t *args, const char *argtypes, char ret);
    double (*field_num)(ok_object self, const char *type, const char *field, double fallback);
    int (*set_field_num)(ok_object self, const char *type, const char *field, double value);
    ok_object (*field_obj)(ok_object self, const char *type, const char *field);
    void *(*field_ptr)(ok_object self, const char *type, const char *field);
    int (*unbox)(ok_object boxed, void *out, int size);
    int64_t (*invoke_exact)(ok_object self, const char *type, const char *method,
                            int argc, const int64_t *args, const char *argtypes, char ret);
};

void set_runtime(const Runtime *r);
}

#endif
