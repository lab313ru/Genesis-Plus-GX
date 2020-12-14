#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#include <Windows.h>

#include "plane_explorer.h"
#include "vdp_ram_debug.h"
#include "hex_editor.h"

#define UpdateMSG (WM_USER+0x0001)

extern const COLORREF normal_pal[];
unsigned short cram_9b_to_16b(unsigned short data);
unsigned short cram_16b_to_9b(unsigned short data);

int select_file_save(char *Dest, const char *Dir, const char *Titre, const char *Filter, const char *Ext, HWND hwnd);
int select_file_load(char *Dest, const char *Dir, const char *Titre, const char *Filter, const char *Ext, HWND hwnd);

extern HINSTANCE pinst;
extern HWND rarch;
#endif

void run_gui();
void stop_gui();

#ifdef __cplusplus
}
#endif
