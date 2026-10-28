#include "okmod.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define TOGGLE_KEY 284
#define LOOK_BUTTON 1
#define SPEED 20.0
#define SENSITIVITY 2.0
#define SPRINT 3.0
#define MAX_PITCH 89.0
#define DEGREES 57.29577951308232

enum { KEY_A = 97, KEY_D = 100, KEY_E = 101, KEY_Q = 113, KEY_S = 115, KEY_W = 119, KEY_SHIFT = 304 };

static const OkMod *g;
static int enabled;
static int ready;
static double yaw;
static double pitch;
static ok_vec3 position;

static double from_bits(int64_t bits) {
    int32_t raw = (int32_t)bits;
    float value;
    memcpy(&value, &raw, 4);
    return value;
}

static int64_t pointer(const void *value) { return (int64_t)(uintptr_t)value; }

static int input(const char *method, int code) {
    int64_t arg = code;
    return (int)(g->invoke(0, "UnityEngine.Input", method, 1, &arg, "i", 'i') & 0xFF);
}

static double axis(const char *name) {
    int64_t arg = pointer(name);
    return from_bits(g->invoke(0, "UnityEngine.Input", "GetAxis", 1, &arg, "s", 'i'));
}

static ok_object get(ok_object self, const char *type, const char *property) {
    return self ? (ok_object)g->invoke(self, type, property, 0, NULL, "", 'o') : 0;
}

static ok_vec3 rotate(ok_quat q, ok_vec3 v) {
    ok_vec3 t = {2 * (q.y * v.z - q.z * v.y), 2 * (q.z * v.x - q.x * v.z), 2 * (q.x * v.y - q.y * v.x)};
    ok_vec3 r = {v.x + q.w * t.x + (q.y * t.z - q.z * t.y), v.y + q.w * t.y + (q.z * t.x - q.x * t.z), v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
    return r;
}

static double clamp(double value, double low, double high) { return value < low ? low : value > high ? high : value; }

static void on_frame(ok_call *call, void *user) {
    (void)call;
    (void)user;
    if (!input("GetKeyDown", TOGGLE_KEY)) return;
    enabled = !enabled;
    ready = 0;
    g->log(OK_LOG_INFO, enabled ? "freecam on" : "freecam off");
}

static void start_from(ok_object transform) {
    ok_quat rotation = {0, 0, 0, 1};
    ok_vec3 ahead = {0, 0, 1};
    g->unbox(get(transform, "ProtectedTransform", "get_position"), &position, sizeof(position));
    g->unbox(get(transform, "ProtectedTransform", "get_rotation"), &rotation, sizeof(rotation));
    ok_vec3 forward = rotate(rotation, ahead);
    yaw = atan2(forward.x, forward.z) * DEGREES;
    pitch = -asin(clamp(forward.y, -1.0, 1.0)) * DEGREES;
    ready = 1;
}

static void on_late_frame(ok_call *call, void *user) {
    (void)call;
    (void)user;
    if (!enabled) return;
    ok_object manager = g->call_static_obj("MVGameControllerBase", "get_MainCameraManager");
    ok_object transform = get(manager, "MainCameraManager", "get_ProtectedTransform");
    if (!transform) return;
    if (!ready) {
        start_from(transform);
        return;
    }
    if (input("GetMouseButton", LOOK_BUTTON)) {
        yaw += axis("Mouse X") * SENSITIVITY;
        pitch = clamp(pitch - axis("Mouse Y") * SENSITIVITY, -MAX_PITCH, MAX_PITCH);
    }
    double y = yaw / DEGREES, p = pitch / DEGREES;
    ok_vec3 forward = {(float)(sin(y) * cos(p)), (float)-sin(p), (float)(cos(y) * cos(p))};
    ok_vec3 right = {(float)cos(y), 0, (float)-sin(y)};
    double dx = 0, dy = 0, dz = 0;
    if (input("GetKey", KEY_W)) { dx += forward.x; dy += forward.y; dz += forward.z; }
    if (input("GetKey", KEY_S)) { dx -= forward.x; dy -= forward.y; dz -= forward.z; }
    if (input("GetKey", KEY_D)) { dx += right.x; dz += right.z; }
    if (input("GetKey", KEY_A)) { dx -= right.x; dz -= right.z; }
    if (input("GetKey", KEY_E)) dy += 1;
    if (input("GetKey", KEY_Q)) dy -= 1;
    double length = sqrt(dx * dx + dy * dy + dz * dz);
    if (length > 0) {
        double delta = from_bits(g->invoke(0, "UnityEngine.Time", "get_deltaTime", 0, NULL, "", 'i'));
        double step = SPEED * delta * (input("GetKey", KEY_SHIFT) ? SPRINT : 1.0) / length;
        position.x += (float)(dx * step);
        position.y += (float)(dy * step);
        position.z += (float)(dz * step);
    }
    double sx = sin(p / 2), cx = cos(p / 2), sy = sin(y / 2), cy = cos(y / 2);
    ok_quat rotation = {(float)(cy * sx), (float)(sy * cx), (float)(-sy * sx), (float)(cy * cx)};
    int64_t at = pointer(&position), turn = pointer(&rotation);
    g->invoke(transform, "ProtectedTransform", "set_position", 1, &at, "p", 'v');
    g->invoke(transform, "ProtectedTransform", "set_rotation", 1, &turn, "p", 'v');
}

static int start(const OkMod *api) {
    g = api;
    api->on("frame", on_frame, 0);
    api->on("frame.late", on_late_frame, 0);
    api->log(OK_LOG_INFO, "freecam ready, F3 flies the camera with WASD, Q and E, hold the right mouse button to look");
    return 0;
}

OKMOD_MOD(start)
