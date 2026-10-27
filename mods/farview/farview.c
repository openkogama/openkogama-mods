#include "okmod.h"
#include <stdint.h>
#include <string.h>

#define FARTHER 10.0
#define SETTLED 0.999

static const OkMod *g;
static double base_distance;
static ok_object last_camera;
static double far_plane;

static int64_t float_bits(double value) {
    float narrow = (float)value;
    int64_t bits = 0;
    memcpy(&bits, &narrow, 4);
    return bits;
}

static double as_float(int64_t bits) {
    int32_t raw = (int32_t)bits;
    float value;
    memcpy(&value, &raw, 4);
    return value;
}

static void widen_culling(void) {
    ok_object camera = g->call_static_obj("CullingApiWrapper", "get_TargetCamera");
    if (!camera) return;
    double distance = g->field_num(0, "CullingApiWrapper", "baseDistance", -1);
    if (distance <= 0) return;
    if (base_distance <= 0) base_distance = distance;
    if (distance >= base_distance * FARTHER * SETTLED) return;
    int64_t args[2] = {float_bits(base_distance * FARTHER), (int64_t)camera};
    g->invoke(0, "CullingApiWrapper", "ChangeDistances", 2, args, "fo", 'v');
}

static void widen_camera(void) {
    ok_object manager = g->call_static_obj("MVGameControllerBase", "get_MainCameraManager");
    ok_object camera = manager ? (ok_object)g->invoke(manager, "MainCameraManager", "get_MainCamera", 0, NULL, "", 'o') : 0;
    if (!camera) return;
    double current = as_float(g->invoke(camera, "UnityEngine.Camera", "get_farClipPlane", 0, NULL, "", 'i'));
    if (camera != last_camera) {
        last_camera = camera;
        far_plane = current;
    }
    if (far_plane <= 0 || current >= far_plane * FARTHER * SETTLED) return;
    int64_t arg = float_bits(far_plane * FARTHER);
    g->invoke(camera, "UnityEngine.Camera", "set_farClipPlane", 1, &arg, "f", 'v');
}

static void on_frame(ok_call *call, void *user) {
    (void)call;
    (void)user;
    widen_culling();
    widen_camera();
}

static int start(const OkMod *api) {
    g = api;
    api->on("frame", on_frame, 0);
    api->log(OK_LOG_INFO, "farview ready, objects and the camera reach ten times farther");
    return 0;
}

OKMOD_MOD(start)
