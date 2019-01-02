#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void create_hex_editor();
void destroy_hex_editor();
void update_hex_editor();

extern HWND HexEditorHwnd;

#define HEX_EDITOR_MUTEX "DBG_HEX_EDITOR_MUTEX"

#ifdef __cplusplus
}
#endif
