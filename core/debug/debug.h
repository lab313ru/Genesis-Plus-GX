#ifndef _DEBUG_H_
#define _DEBUG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "debug_wrap.h"

void start_debugging();
//void stop_debugging();
//int is_debugger_active();
//void send_dbg_request();
//int recv_dbg_event(int wait);
//void handle_dbg_commands();
void process_breakpoints();
void check_breakpoint(bpt_type_t type, int width, unsigned int address, unsigned int value);

#ifdef __cplusplus
}
#endif

#endif