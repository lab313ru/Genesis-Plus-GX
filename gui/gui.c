#ifdef _WIN32
#include <Windows.h>
#include <commctrl.h>
#include <process.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

#include "gui.h"

#include "cpuhook.h"
#include "debug.h"

#ifdef _WIN32
static HANDLE hThread;
HINSTANCE pinst = NULL;
HWND rarch = NULL;

static ACTCTX actCtx;
static HANDLE hActCtx;
static ULONG_PTR cookie;
#else
static pthread_t *threadId;
#endif

static int dbg_active = 0;

#ifdef _WIN32
HINSTANCE GetHInstance()
{
    MEMORY_BASIC_INFORMATION mbi;
    SetLastError(ERROR_SUCCESS);
    VirtualQuery(GetHInstance, &mbi, sizeof(mbi));

    return (HINSTANCE)mbi.AllocationBase;
}

static void enable_visual_styles()
{
    ZeroMemory(&actCtx, sizeof(actCtx));
    actCtx.cbSize = sizeof(actCtx);
    actCtx.hModule = pinst;
    actCtx.lpResourceName = MAKEINTRESOURCE(2);
    actCtx.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;

    hActCtx = CreateActCtx(&actCtx);
    if (hActCtx != INVALID_HANDLE_VALUE) {
        ActivateActCtx(hActCtx, &cookie);
    }
}

static void disable_visual_styles()
{
    DeactivateActCtx(0, cookie);
    ReleaseActCtx(hActCtx);
}

const COLORREF normal_pal[] =
{
    0x00000000,
    0x00000011,
    0x00000022,
    0x00000033,
    0x00000044,
    0x00000055,
    0x00000066,
    0x00000077,
    0x00000088,
    0x00000099,
    0x000000AA,
    0x000000BB,
    0x000000CC,
    0x000000DD,
    0x000000EE,
    0x000000FF
};
#endif

unsigned short cram_9b_to_16b(unsigned short data)
{
    /* Unpack 9-bit CRAM data (BBBGGGRRR) to 16-bit data (BBB0GGG0RRR0) */
    return (unsigned short)(((data & 0x1C0) << 3) | ((data & 0x038) << 2) | ((data & 0x007) << 1));
}

unsigned short cram_16b_to_9b(unsigned short data)
{
    return (unsigned short)(((data & 0xE00) >> 3) | ((data & 0x0E0) >> 2) | ((data & 0x00E) >> 1));
}

#ifdef _WIN32
int select_file_save(char *Dest, const char *Dir, const char *Titre, const char *Filter, const char *Ext, HWND hwnd)
{
    OPENFILENAME ofn;

    if (!strcmp(Dest, ""))
    {
        strcpy(Dest, "default.");
        strcat(Dest, Ext);
    }

    memset(&ofn, 0, sizeof(OPENFILENAME));

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwnd;
    ofn.hInstance = pinst;
    ofn.lpstrFile = Dest;
    ofn.nMaxFile = 2047;
    ofn.lpstrFilter = Filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = Dir;
    ofn.lpstrTitle = Titre;
    ofn.lpstrDefExt = Ext;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetSaveFileName(&ofn)) return 1;

    return 0;
}

int select_file_load(char *Dest, const char *Dir, const char *Titre, const char *Filter, const char *Ext, HWND hwnd)
{
    OPENFILENAME ofn;

    if (!strcmp(Dest, ""))
    {
        strcpy(Dest, "default.");
        strcat(Dest, Ext);
    }

    memset(&ofn, 0, sizeof(OPENFILENAME));

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = hwnd;
    ofn.hInstance = pinst;
    ofn.lpstrFile = Dest;
    ofn.nMaxFile = 2047;
    ofn.lpstrFilter = Filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = Dir;
    ofn.lpstrTitle = Titre;
    ofn.lpstrDefExt = Ext;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileName(&ofn)) return 1;

    return 0;
}
#endif

static void update_windows(void *data)
{
    while (dbg_active)
    {
#ifdef _WIN32
        update_plane_explorer();
        update_vdp_ram_debug();
        update_hex_editor();
#endif

#ifdef _WIN32
        Sleep(300);
#else
        usleep(300 * 1000);
#endif
    }

#ifdef _WIN32
    _endthread();
#else
    pthread_exit(0);
#endif
}

void run_gui()
{
#ifdef _WIN32
    enable_visual_styles();

    create_plane_explorer();
    create_vdp_ram_debug();
    create_hex_editor();
#endif

    set_cpu_hook(process_breakpoints);
    dbg_active = 1;

#ifdef _WIN32
    _beginthread(update_windows, 1024, NULL);
#else
    pthread_attr_t attr;
    if (pthread_attr_init(&attr))
        return;

    if (pthread_attr_setstacksize(&attr, 1024))
        return;

    if (pthread_create(threadId, &attr, (void*(*)(void*))update_windows, NULL))
        return;
#endif

#ifdef _WIN32
    create_breakpoints_window();
#endif
}

void stop_gui()
{
#ifdef _WIN32
    destroy_plane_explorer();
    destroy_vdp_ram_debug();
    destroy_hex_editor();
#endif

    dbg_active = 0;
    set_cpu_hook(NULL);

#ifdef _WIN32
    destroy_breakpoints_window();

    disable_visual_styles();
#endif
}
