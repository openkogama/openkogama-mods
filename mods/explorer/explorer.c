#include "okmod.h"

static const OkMod *g;

static void bump(const char *name, double by) {
    g->param_set_num(name, g->param_get_num(name, 0) + by);
}

static void added(ok_call *call, void *user) {
    (void)call; (void)user;
    bump("explorer.count", 1);
}

static void removed(ok_call *call, void *user) {
    (void)call; (void)user;
    bump("explorer.count", -1);
}

static int start(const OkMod *api) {
    g = api;
    api->on("world.object_added", added, 0);
    api->on("world.object_removed", removed, 0);
    api->log(OK_LOG_INFO, "explorer watching the world");
    return 0;
}

OKMOD_MOD(start)
