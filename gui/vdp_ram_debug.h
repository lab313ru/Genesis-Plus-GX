#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void create_vdp_ram_debug();
void destroy_vdp_ram_debug();
void update_vdp_ram_debug();

extern HWND VDPRamHWnd;

#define VDP_RAM_MUTEX "DBG_VDP_RAM_MUTEX"

#ifdef __cplusplus
}
#endif
