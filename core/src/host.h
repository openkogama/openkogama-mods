#ifndef OKMOD_HOST_H
#define OKMOD_HOST_H

#include "okmod.h"

namespace okmod {
const OkMod *host_api();

int load_mods(const char *dir);

ok_call *call_begin();
void call_push_int(ok_call *call, int64_t v);
void call_push_num(ok_call *call, double v);
void call_push_str(ok_call *call, const char *v);
void call_push_object(ok_call *call, ok_object v);
void call_push_player(ok_call *call, ok_player v);
void call_push_vec3(ok_call *call, ok_vec3 v);
void dispatch(const char *hook, ok_call *call);
int call_argc(ok_call *call);
int64_t call_arg_int(ok_call *call, int index);
void call_set_arg_int(ok_call *call, int index, int64_t value);
int call_cancelled(ok_call *call);
int64_t call_result_int(ok_call *call, int64_t fallback);
double call_result_num(ok_call *call, double fallback);
void call_end(ok_call *call);

int run_gate(const char *hook);
int run_event(const char *hook, int64_t *args, int argc, int64_t *result);

void pump();

int control_command(const char *line);

void runtime_attach();
bool attach_mono();
int64_t runtime_address(void *handle);
}

#endif
