#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <Windows.h>

#define UpdateMSG (WM_USER+0x0001)

void run_gui();
void update_gui();
void stop_gui();

extern const COLORREF normal_pal[];
unsigned short cram_9b_to_16b(unsigned short data);
unsigned short cram_16b_to_9b(unsigned short data);

int select_file_save(char *Dest, const char *Dir, const char *Titre, const char *Filter, const char *Ext, HWND hwnd);
int select_file_load(char *Dest, const char *Dir, const char *Titre, const char *Filter, const char *Ext, HWND hwnd);

extern HINSTANCE dbg_wnd_hinst;
extern HWND dbg_window;

#ifdef __cplusplus
}
#endif
