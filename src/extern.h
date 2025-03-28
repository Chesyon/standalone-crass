#pragma once

#include <pmdsky.h>
#include <cot.h>

extern int MESSAGE_SET_WAIT_MODE_PARAMS[2];
extern void* UNIONALL_RAM_ADDRESS;

void MessageSetWaitMode(int speed1, int speed2);
void InitScriptRoutineFromCoroutineInfo(struct script_routine* routine, undefined4 param_2, struct coroutine_info* coroutine_info, int status);
void GetSceneName(char* dst, const char* src);

static inline bool IsWithinRange(int x, int min, int max) { return min <= x && x <= max; }
