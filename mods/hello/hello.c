#include "okmod.h"

static const OkMod *g;

static void on_ping(ok_call *call, void *user) {
    (void)user;
    g->flag_set("hello.pinged", 1);
    g->set_result_bool(call, 1);
    g->cancel(call);
}

static int start(const OkMod *api) {
    g = api;
    api->log(OK_LOG_INFO, "hello mod up");
    api->on("test.ping", on_ping, 0);
    return 0;
}

OKMOD_MOD(start)
