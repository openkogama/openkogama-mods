#include "okmod.h"
#include <math.h>
#include <windows.h>

#define NOTCH 120.0
#define STEP 0.85
#define EYE 0.05
#define NEAREST 1.2
#define FARTHEST 300.0
#define SETTLE_SECONDS 0.08
#define THIRD_PERSON 0

static const OkMod *g;
static HWND window;
static WNDPROC original;
static volatile LONG wheel;
static double wanted;
static double smooth;
static double last;
static int first_person;
static ok_object hidden_avatar;

static double seconds(void) {
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    return (double)now.QuadPart / (double)rate.QuadPart;
}

static LRESULT CALLBACK on_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_MOUSEWHEEL) InterlockedExchangeAdd(&wheel, GET_WHEEL_DELTA_WPARAM(wparam));
    return CallWindowProcW(original, hwnd, message, wparam, lparam);
}

static BOOL CALLBACK pick(HWND hwnd, LPARAM found) {
    DWORD owner = 0;
    wchar_t name[32];
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
    if (!GetClassNameW(hwnd, name, 32) || wcscmp(name, L"UnityWndClass") != 0) return TRUE;
    *(HWND *)found = hwnd;
    return FALSE;
}

static void attach(void) {
    HWND found = NULL;
    EnumWindows(pick, (LPARAM)&found);
    if (!found) return;
    window = found;
    original = (WNDPROC)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)on_message);
}

static void hide_head(ok_object avatar, int hidden) {
    ok_object local = avatar ? (ok_object)g->invoke(avatar, "MVAvatarLocal", "get_AvatarLocal", 0, NULL, "", 'o') : 0;
    ok_object cameras = local ? g->field_obj(local, "AvatarLocal", "avatarCameraController") : 0;
    ok_object eyes = cameras ? g->field_obj(cameras, "AvatarCamerasDesktop", "firstPersonDesktopCamera") : 0;
    int64_t arg = hidden;
    if (eyes) g->invoke(eyes, "FirstPersonCamera", "HideBody", 1, &arg, "i", 'v');
    hidden_avatar = hidden ? avatar : 0;
}

static void zoom(LONG turned, ok_object avatar) {
    if (!turned) return;
    if (first_person) {
        if (turned > 0) return;
        first_person = 0;
        wanted = NEAREST;
        hide_head(avatar, 0);
        return;
    }
    if (turned > 0 && wanted <= NEAREST * 1.001) {
        first_person = 1;
        wanted = EYE;
        hide_head(avatar, 1);
        return;
    }
    wanted *= pow(STEP, turned / NOTCH);
    if (wanted < NEAREST) wanted = NEAREST;
    if (wanted > FARTHEST) wanted = FARTHEST;
}

static void follow(ok_object camera, double current) {
    double now = seconds();
    double step = last > 0 ? 1.0 - exp(-(now - last) / SETTLE_SECONDS) : 1.0;
    last = now;
    smooth += (wanted - smooth) * step;
    if (fabs(smooth - current) < 0.0005) return;
    double distance = g->field_num(camera, "PlaymodeCamera", "distance", current);
    g->set_field_num(camera, "PlaymodeCamera", "distance", distance * smooth / current);
    g->set_field_num(camera, "PlaymodeCamera", "distanceToAvatar", smooth);
}

static void on_frame(ok_call *call, void *user) {
    (void)call;
    (void)user;
    if (!window) attach();
    LONG turned = InterlockedExchange(&wheel, 0);

    ok_object manager = g->call_static_obj("MVGameControllerBase", "get_MainCameraManager");
    ok_object camera = manager ? (ok_object)g->invoke(manager, "MainCameraManager", "get_CurrentCamera", 0, NULL, "", 'o') : 0;
    if (!camera || g->invoke(camera, "MVCameraBase", "get_CameraType", 0, NULL, "", 'i') != THIRD_PERSON) {
        last = 0;
        return;
    }
    double current = g->field_num(camera, "PlaymodeCamera", "distanceToAvatar", -1);
    if (current <= 0) return;
    if (wanted <= 0) wanted = smooth = current;

    ok_object avatar = g->field_obj(camera, "PlaymodeCamera", "avatarLocal");
    zoom(turned, avatar);
    if (first_person && avatar != hidden_avatar) hide_head(avatar, 1);
    follow(camera, current);
}

static int start(const OkMod *api) {
    g = api;
    api->on("frame", on_frame, 0);
    api->log(OK_LOG_INFO, "scrollzoom ready, scroll to move the camera, all the way in for first person");
    return 0;
}

OKMOD_MOD(start)
