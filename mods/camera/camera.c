#include "okmod.h"
#include <stdint.h>
#include <string.h>

static const OkMod *g;

static void on_frame(ok_call *call, void *user) {
    (void)call; (void)user;
    int fov = g->flag_get("fov");
    int zoom = g->flag_get("zoom");
    if (!fov && !zoom) return;
    ok_object cam = g->call_static_obj("MVGameControllerBase", "get_MainCameraManager");
    if (!cam) return;

    float value = fov ? (float)g->param_get_num("fov_value", 60.0) : 60.0f;
    if (zoom) {
        double level = g->param_get_num("zoom_level", 1.0);
        if (level > 0.01) value = (float)(value / level);
    }
    int64_t arg = 0;
    memcpy(&arg, &value, 4);
    g->invoke(cam, "MainCameraManager", "set_FieldOfView", 1, &arg, "f", 'v');
}

static int start(const OkMod *api) {
    g = api;
    api->on("frame", on_frame, 0);
    api->log(OK_LOG_INFO, "camera fov control ready");
    return 0;
}

OKMOD_MOD(start)
