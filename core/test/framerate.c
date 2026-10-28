#include "okmod.h"
#include <stdio.h>
#include <windows.h>

static int frames;
static ULONGLONG since;
static ULONGLONG previous;
static ULONGLONG longest;

static void on_frame(ok_call *call, void *user) {
    (void)call;
    (void)user;
    ULONGLONG now = GetTickCount64();
    if (previous && now - previous > longest) longest = now - previous;
    previous = now;
    frames++;
    if (!since) since = now;
    if (now - since < 1000) return;
    FILE *f = fopen("framerate.txt", "a");
    if (f) {
        fprintf(f, "%llu %d %llu %llu\n", now, frames, now - since, longest);
        fclose(f);
    }
    frames = 0;
    longest = 0;
    since = now;
}

static int start(const OkMod *api) {
    api->on("frame", on_frame, 0);
    return 0;
}

OKMOD_MOD(start)
