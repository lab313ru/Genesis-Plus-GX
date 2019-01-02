#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <Windows.h>

void create_plane_explorer();
void destroy_plane_explorer();
void update_plane_explorer();

#define PLANE_EXPLORER_MUTEX "DBG_PLANE_EXPLORER_MUTEX"
extern HWND PlaneExplorerHWnd;

#ifdef __cplusplus
}
#endif
