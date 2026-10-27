#include "okmod.h"
#include <stdint.h>

static const OkMod *g;

static void on_frame(ok_call *call, void *user) {
    (void)call; (void)user;
    if (g->flag_get("anti_afk")) {
        int64_t off = 0;
        g->invoke(0, "AwayMonitor", "set_IdleKickEnabled", 1, &off, "i", 'v');
    }
}

static int start(const OkMod *api) {
    g = api;
    api->on("frame", on_frame, 0);
    api->log(OK_LOG_INFO, "tweaks ready");
    return 0;
}

OKMOD_MOD(start)
