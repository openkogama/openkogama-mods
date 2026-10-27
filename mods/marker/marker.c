#include "okmod.h"
#include <stdio.h>
#include <string.h>

static const OkMod *g;
static int frames;

static void mark(const char *line) {
    FILE *f = fopen("okmod-fired.txt", "a");
    if (f) { fputs(line, f); fputc('\n', f); fclose(f); }
}

static void on_frame(ok_call *call, void *user) {
    (void)call; (void)user;
    frames++;
}

static void fired(ok_call *call, void *user) {
    (void)call;
    mark((const char *)user);
}

static void on_loading(ok_call *call, void *user) {
    (void)call;
    mark((const char *)user);
    g->flag_set("fov", 1);
    g->param_set_num("fov_value", 77.0);
}

static void on_loaded(ok_call *call, void *user) {
    (void)call; (void)user;
    char line[128];
    long long mode = g->call_static_i("MVGameControllerBase", "get_GameMode");
    ok_object cam = g->call_static_obj("MVGameControllerBase", "get_MainCameraManager");

    long long fovbits = cam ? g->call_obj_i(cam, "MainCameraManager", "get_FieldOfView") : 0;
    snprintf(line, sizeof(line), "world.loaded GameMode=%lld cam=%d fovbits=%lld frames=%d", mode, cam != 0, fovbits, frames);
    mark(line);
}

static int start(const OkMod *api) {
    g = api;
    api->on("world.object_added", fired, (void *)"world.object_added");
    api->on("world.object_removed", fired, (void *)"world.object_removed");
    api->on("editor.add_prototype", fired, (void *)"editor.add_prototype");
    api->on("loading.start", on_loading, (void *)"loading.start");
    api->on("world.loaded", on_loaded, 0);
    api->on("frame", on_frame, 0);
    api->log(OK_LOG_INFO, "marker armed");
    return 0;
}

OKMOD_MOD(start)
