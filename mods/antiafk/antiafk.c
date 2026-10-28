#include "okmod.h"
#include <windows.h>

#define EVERY_MILLISECONDS 5000

static const OkMod *g;
static ULONGLONG last;

static void on_frame(ok_call *call, void *user) {
    (void)call;
    (void)user;
    ULONGLONG now = GetTickCount64();
    if (last && now - last < EVERY_MILLISECONDS) return;
    last = now;
    int64_t kick = 0;
    g->invoke(0, "AwayMonitor", "set_IdleKickEnabled", 1, &kick, "i", 'v');
}

static int start(const OkMod *api) {
    g = api;
    api->on("frame", on_frame, 0);
    api->log(OK_LOG_INFO, "antiafk ready, idle players are no longer kicked");
    return 0;
}

OKMOD_MOD(start)
