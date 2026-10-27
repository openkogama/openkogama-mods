#include "okmod.h"
#include <stdint.h>
#include <string.h>

typedef struct { float x, y, z; } Vec3;

static const OkMod *g;

static float as_float(int64_t slot) { float f; memcpy(&f, &slot, 4); return f; }
static int64_t from_float(float f) { int64_t v = 0; memcpy(&v, &f, 4); return v; }

static void move_character(ok_call *call, void *user) {
    (void)user;
    if (!g->flag_get("edit_speed")) return;

    Vec3 *delta = (Vec3 *)(uintptr_t)g->arg_int(call, 1);
    if (!delta) return;
    float m = (float)g->param_get_num("edit_speed_mult", 1.0);
    delta->x *= m;
    delta->y *= m;
    delta->z *= m;
}

static void add_prototype(ok_call *call, void *user) {
    (void)user;
    if (!g->flag_get("model_scale")) return;

    (void)as_float;
    g->set_arg_int(call, 6, from_float((float)g->param_get_num("model_scale_value", 1.0)));
}

static int start(const OkMod *api) {
    g = api;
    api->on("editor.move_character", move_character, 0);
    api->on("editor.add_prototype", add_prototype, 0);
    api->log(OK_LOG_INFO, "buildtools ready");
    return 0;
}

OKMOD_MOD(start)
