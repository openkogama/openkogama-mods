#include "host.h"

#include <cstdio>
#include <cstring>

using namespace okmod;

static int failures = 0;
static void check(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static int sum_seen = 0;
static void on_sum(ok_call *call, void *user) {
    const OkMod *api = (const OkMod *)user;
    sum_seen = api->arg_count(call);
    int64_t total = api->arg_int(call, 0) + api->arg_int(call, 1);
    api->set_result_int(call, total);
}

static int deferred_ran = 0;
static void later(void *user) { deferred_ran = *(int *)user; }

int main(int argc, char **argv) {
    const OkMod *api = host_api();
    check(api->abi == OKMOD_ABI, "abi matches");
    check(api->size == sizeof(OkMod), "table is full size");

    ok_sub sub = api->on("test.sum", on_sum, (void *)api);
    ok_call *call = call_begin();
    call_push_int(call, 20);
    call_push_int(call, 22);
    dispatch("test.sum", call);
    check(sum_seen == 2, "handler saw both args");
    check(call_result_int(call, -1) == 42, "result came back");
    call_end(call);

    api->off(sub);
    sum_seen = 0;
    call = call_begin();
    call_push_int(call, 1);
    call_push_int(call, 2);
    dispatch("test.sum", call);
    check(sum_seen == 0, "off stopped the handler");
    call_end(call);

    api->flag_set("x", 1);
    check(api->flag_get("x") == 1, "flag set/get");
    check(api->flag_get("missing") == 0, "missing flag is 0");
    api->param_set_num("grid", 0.25);
    check(api->param_get_num("grid", 1.0) == 0.25, "param set/get");
    check(api->param_get_num("absent", 9.0) == 9.0, "param fallback");

    check(api->object_count() == 0, "no objects without a game");
    check(api->player_local() == 0, "no local player without a game");

    check(control_command("flag|no_build_limit|1") == 1 && api->flag_get("no_build_limit") == 1, "control sets a flag");
    check(control_command("flag|no_build_limit|0") == 1 && api->flag_get("no_build_limit") == 0, "control clears a flag");
    check(control_command("param|fov_value|90") == 1 && api->param_get_num("fov_value", 0) == 90, "control sets a param");
    check(control_command("nonsense") == 0, "control rejects junk");

    static int token = 7;
    api->defer(later, &token);
    check(deferred_ran == 0, "deferred waits for pump");
    pump();
    check(deferred_ran == 7, "pump ran deferred work");

    api->on("ev.clamp", [](ok_call *c, void *u) {
        const OkMod *a = (const OkMod *)u;
        if (a->arg_int(c, 0) > 100) a->set_arg_int(c, 0, 100);
        if (a->arg_int(c, 1) != 0) { a->set_result_int(c, 7); a->cancel(c); }
    }, (void *)api);

    int64_t args1[2] = {250, 0};
    int64_t result1 = -1;
    int cancelled1 = run_event("ev.clamp", args1, 2, &result1);
    check(cancelled1 == 0, "event not cancelled when arg1 is 0");
    check(args1[0] == 100, "event clamped arg0 back into the buffer");

    int64_t args2[2] = {5, 1};
    int64_t result2 = -1;
    int cancelled2 = run_event("ev.clamp", args2, 2, &result2);
    check(cancelled2 == 1, "event cancelled when arg1 set");
    check(result2 == 7, "event returned the result");

    if (argc > 1) {
        int loaded = load_mods(argv[1]);
        check(loaded >= 1, "loaded the hello mod");
        ok_call *ping = call_begin();
        dispatch("test.ping", ping);
        check(call_cancelled(ping) == 1, "mod cancelled the gate");
        call_end(ping);
        check(api->flag_get("hello.pinged") == 1, "mod handler ran");

        int64_t obj[1] = {0x1000};
        run_event("world.object_added", obj, 1, nullptr);
        run_event("world.object_added", obj, 1, nullptr);
        run_event("world.object_removed", obj, 1, nullptr);
        check(api->param_get_num("explorer.count", -1) == 1, "explorer followed add/add/remove");

        struct { float x, y, z; } delta = {1, 2, 3};
        api->flag_set("edit_speed", 1);
        api->param_set_num("edit_speed_mult", 2);
        int64_t move[2] = {0, (int64_t)(intptr_t)&delta};
        run_event("editor.move_character", move, 2, nullptr);
        check(delta.x == 2 && delta.y == 4 && delta.z == 6, "buildtools scaled the move delta through the pointer");
        api->flag_set("edit_speed", 0);
        delta.x = 5;
        run_event("editor.move_character", move, 2, nullptr);
        check(delta.x == 5, "buildtools left the delta alone when off");

        float one = 1.0f;
        int64_t proto[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        memcpy(&proto[6], &one, 4);
        api->flag_set("model_scale", 1);
        api->param_set_num("model_scale_value", 4.0);
        run_event("editor.add_prototype", proto, 8, nullptr);
        float scaled = 0;
        memcpy(&scaled, &proto[6], 4);
        check(scaled == 4.0f, "buildtools set the by-value float scale in its xmm slot");
    }

    std::printf("%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
