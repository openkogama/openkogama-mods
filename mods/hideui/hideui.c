#include "okmod.h"
#include <windows.h>

#define TOGGLE_KEY VK_F1

static const OkMod *g;
static int was_down;

static int focused(void) {
    DWORD owner = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &owner);
    return owner == GetCurrentProcessId();
}

static ok_object interface_root(void) {
    ok_object ui = g->call_static_obj("MVGameControllerBase", "get_PlayModeUI");
    if (!ui) ui = g->call_static_obj("MVGameControllerBase", "get_EditModeUI");
    return ui ? (ok_object)g->invoke(ui, "UnityEngine.Component", "get_gameObject", 0, NULL, "", 'o') : 0;
}

static void on_frame(ok_call *call, void *user) {
    (void)call;
    (void)user;
    int down = focused() && (GetAsyncKeyState(TOGGLE_KEY) & 0x8000);
    int pressed = down && !was_down;
    was_down = down;
    if (!pressed) return;

    ok_object root = interface_root();
    if (!root) return;
    int64_t shown = g->invoke(root, "UnityEngine.GameObject", "get_activeSelf", 0, NULL, "", 'i') & 0xFF;
    int64_t show = !shown;
    g->invoke(root, "UnityEngine.GameObject", "SetActive", 1, &show, "i", 'v');
}

static int start(const OkMod *api) {
    g = api;
    api->on("frame", on_frame, 0);
    api->log(OK_LOG_INFO, "hideui ready, F1 hides and shows the interface");
    return 0;
}

OKMOD_MOD(start)
