#ifndef _DEBUG_H_
#define _DEBUG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <setjmp.h>
#include "debug_wrap.h"

extern void start_debugging();
extern void stop_debugging();
extern int is_debugger_accessible();
extern void process_request();
extern int is_debugger_paused();
extern int activate_shared_mem();
extern void deactivate_shared_mem();
void check_breakpoint(bpt_type_t type, int width, unsigned int address, unsigned int value);

extern jmp_buf jmp_env;

#ifdef __cplusplus
}
#endif

#endif