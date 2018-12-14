#include <Windows.h>
#include <string>

#include "gui.h"
#include "disassembler.h"

#include "BabyGrid.h"
#include "edit_fields.h"

#include "resource.h"

#include "shared.h"
#include "m68k.h"
#include "debug.h"

#define DO_INIT_GRIDS (WM_USER + 0x100)

static HANDLE hThread = NULL;
static HWND disHwnd = NULL;
static _BGCELL cell;
static std::string previousText;
static unsigned int currentControlFocus;

static HWND listHwnd = NULL;

static void PutCell(HWND hgrid,int row, int col, const char* text)
{
    SetCell(&cell,row,col);
    SendMessage(hgrid, BGM_SETCELLDATA, (size_t)&cell, (size_t)text);
}

static void resize_func()
{
    RECT r;
    GetWindowRect(disHwnd, &r);

    SetWindowPos(listHwnd, NULL,
        r.left,
        r.top,
        (r.right - r.left) / 3 * 2,
        r.bottom - r.top - 10,
        SWP_NOZORDER | SWP_NOACTIVATE);

    GetWindowRect(listHwnd, &r);
    MapWindowPoints(HWND_DESKTOP, disHwnd, (LPPOINT)&r, 2);

    const int widthL = 30; // label
    const int height = 21;
    const int widthE = 80; // edit

    for (int i = 0; i <= (IDC_REG_D7 - IDC_REG_D0); ++i)
    {
        HWND hDL = GetDlgItem(disHwnd, IDC_REG_D0_L + i);
        HWND hDE = GetDlgItem(disHwnd, IDC_REG_D0 + i);
        HWND hAL = GetDlgItem(disHwnd, IDC_REG_A0_L + i);
        HWND hAE = GetDlgItem(disHwnd, IDC_REG_A0 + i);

        SetWindowPos(hDL, NULL,
            r.right + 1,
            5 + i * (height + 5) + 3,
            widthL,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);

        SetWindowPos(hDE, NULL,
            r.right + 1 + widthL + 3,
            5 + i * (height + 5),
            widthE,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);

        SetWindowPos(hAL, NULL,
            r.right + 1 + widthL + 3 + widthE + 5,
            5 + i * (height + 5) + 3,
            widthL,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);

        SetWindowPos(hAE, NULL,
            r.right + 1 + widthL + 3 + widthE + 5 + widthL + 3,
            5 + i * (height + 5),
            widthE,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    HWND hPCL = GetDlgItem(disHwnd, IDC_REG_PC_L);
    HWND hPCE = GetDlgItem(disHwnd, IDC_REG_PC);
    HWND hSPL = GetDlgItem(disHwnd, IDC_REG_SP_L);
    HWND hSPE = GetDlgItem(disHwnd, IDC_REG_SP);

    SetWindowPos(hPCL, NULL,
        r.right + 1,
        10 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3,
        widthL,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);

    SetWindowPos(hPCE, NULL,
        r.right + 1 + widthL + 3,
        10 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5),
        widthE,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);

    SetWindowPos(hSPL, NULL,
        r.right + 1 + widthL + 3 + widthE + 5,
        10 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3,
        widthL,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);

    SetWindowPos(hSPE, NULL,
        r.right + 1 + widthL + 3 + widthE + 5 + widthL + 3,
        10 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5),
        widthE,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

INT_PTR disassemblerWM_COMMAND(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
    if ((HIWORD(wparam) == EN_CHANGE))
    {
        return FALSE;
    }
    else if ((HIWORD(wparam) == EN_SETFOCUS))
    {
        previousText = GetDlgItemString(hwnd, LOWORD(wparam));
        currentControlFocus = LOWORD(wparam);
    }
    else if ((HIWORD(wparam) == EN_KILLFOCUS))
    {
        std::string newText = GetDlgItemString(hwnd, LOWORD(wparam));
        if (newText != previousText)
        {
            switch (LOWORD(wparam))
            {
            case IDC_REG_D0:
            case IDC_REG_D1:
            case IDC_REG_D2:
            case IDC_REG_D3:
            case IDC_REG_D4:
            case IDC_REG_D5:
            case IDC_REG_D6:
            case IDC_REG_D7:
            case IDC_REG_A0:
            case IDC_REG_A1:
            case IDC_REG_A2:
            case IDC_REG_A3:
            case IDC_REG_A4:
            case IDC_REG_A5:
            case IDC_REG_A6:
            case IDC_REG_A7:
               m68k_set_reg((m68k_register_t)((int)M68K_REG_D0 + LOWORD(wparam) - IDC_REG_D0), GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_REG_PC:
                m68k_set_reg(M68K_REG_PC, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_REG_SP:
                m68k_set_reg(M68K_REG_SP, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            }
        }
    }

    return TRUE;
}

void disassemblerUpdateRegs(HWND hwnd)
{
    if (currentControlFocus != IDC_REG_D0)
        UpdateDlgItemHex(hwnd, IDC_REG_D0, 8, m68k_get_reg(M68K_REG_D0));
    if (currentControlFocus != IDC_REG_D1)
        UpdateDlgItemHex(hwnd, IDC_REG_D1, 8, m68k_get_reg(M68K_REG_D1));
    if (currentControlFocus != IDC_REG_D2)
        UpdateDlgItemHex(hwnd, IDC_REG_D2, 8, m68k_get_reg(M68K_REG_D2));
    if (currentControlFocus != IDC_REG_D3)
        UpdateDlgItemHex(hwnd, IDC_REG_D3, 8, m68k_get_reg(M68K_REG_D3));
    if (currentControlFocus != IDC_REG_D4)
        UpdateDlgItemHex(hwnd, IDC_REG_D4, 8, m68k_get_reg(M68K_REG_D4));
    if (currentControlFocus != IDC_REG_D5)
        UpdateDlgItemHex(hwnd, IDC_REG_D5, 8, m68k_get_reg(M68K_REG_D5));
    if (currentControlFocus != IDC_REG_D6)
        UpdateDlgItemHex(hwnd, IDC_REG_D6, 8, m68k_get_reg(M68K_REG_D6));
    if (currentControlFocus != IDC_REG_D7)
        UpdateDlgItemHex(hwnd, IDC_REG_D7, 8, m68k_get_reg(M68K_REG_D7));

    if (currentControlFocus != IDC_REG_A0)
        UpdateDlgItemHex(hwnd, IDC_REG_A0, 8, m68k_get_reg(M68K_REG_A0));
    if (currentControlFocus != IDC_REG_A1)
        UpdateDlgItemHex(hwnd, IDC_REG_A1, 8, m68k_get_reg(M68K_REG_A1));
    if (currentControlFocus != IDC_REG_A2)
        UpdateDlgItemHex(hwnd, IDC_REG_A2, 8, m68k_get_reg(M68K_REG_A2));
    if (currentControlFocus != IDC_REG_A3)
        UpdateDlgItemHex(hwnd, IDC_REG_A3, 8, m68k_get_reg(M68K_REG_A3));
    if (currentControlFocus != IDC_REG_A4)
        UpdateDlgItemHex(hwnd, IDC_REG_A4, 8, m68k_get_reg(M68K_REG_A4));
    if (currentControlFocus != IDC_REG_A5)
        UpdateDlgItemHex(hwnd, IDC_REG_A5, 8, m68k_get_reg(M68K_REG_A5));
    if (currentControlFocus != IDC_REG_A6)
        UpdateDlgItemHex(hwnd, IDC_REG_A6, 8, m68k_get_reg(M68K_REG_A6));
    if (currentControlFocus != IDC_REG_A7)
        UpdateDlgItemHex(hwnd, IDC_REG_A7, 8, m68k_get_reg(M68K_REG_A7));
    if (currentControlFocus != IDC_REG_PC)
        UpdateDlgItemHex(hwnd, IDC_REG_PC, 8, m68k_get_reg(M68K_REG_PC));
    if (currentControlFocus != IDC_REG_SP)
        UpdateDlgItemHex(hwnd, IDC_REG_SP, 8, m68k_get_reg(M68K_REG_SP));
}

LRESULT CALLBACK DisasseblerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {

    } break;
    case WM_SIZE:
    {
        resize_func();
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case DO_INIT_GRIDS:
    {
        RegisterGridClass(dbg_wnd_hinst);

        listHwnd = CreateWindowEx(WS_EX_CLIENTEDGE, "BABYGRID", "", WS_VISIBLE|WS_CHILD, 0, 0, 0, 0, disHwnd, (HMENU)500, dbg_wnd_hinst, NULL);

        // listing
        SendMessage(listHwnd, BGM_SETGRIDDIM, 100, 6);
        SendMessage(listHwnd, BGM_SETCOLAUTOWIDTH, TRUE, 0);
        SendMessage(listHwnd, BGM_SETCOLWIDTH, 0, 0);
        SendMessage(listHwnd, BGM_SETHEADERROWHEIGHT, 0, 0);
        SendMessage(listHwnd, BGM_SETROWSNUMBERED, FALSE, 0);
        SendMessage(listHwnd, BGM_SETCOLSNUMBERED, FALSE, 0);
        SendMessage(listHwnd, BGM_SETGRIDLINECOLOR, (UINT)RGB(255,255,255), 0);

        PutCell(listHwnd, 1, 1, "Address");
        PutCell(listHwnd, 1, 2, "Opcode");
        PutCell(listHwnd, 1, 3, "Args");
        PutCell(listHwnd, 1, 4, "Comment");
        PutCell(listHwnd, 1, 5, "Machine Code");

        SendMessage(listHwnd, BGM_NOTIFYROWCHANGED, 0, 0);
    } break;
    case WM_COMMAND:
        return disassemblerWM_COMMAND(hWnd, wParam, lParam);
        break;
    case UpdateMSG:
        disassemblerUpdateRegs(hWnd);
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

static DWORD WINAPI ThreadProc(LPVOID lpParam)
{
    MSG messages;

    memset(&cell, 0, sizeof(cell));
    disHwnd = CreateDialog(dbg_wnd_hinst, MAKEINTRESOURCE(IDD_DISASSEMBLER), dbg_window, (DLGPROC)DisasseblerWndProc);
    SendMessage(disHwnd, DO_INIT_GRIDS, 0, 0);
    ShowWindow(disHwnd, SW_SHOW);
    UpdateWindow(disHwnd);

    resize_func();

    ShowWindow(listHwnd, SW_SHOW);
    UpdateWindow(listHwnd);

    while (GetMessage(&messages, disHwnd, 0, 0))
    {
        TranslateMessage(&messages);
        DispatchMessage(&messages);
    }

    return 1;
}

void create_disassembler()
{
    hThread = CreateThread(0, NULL, ThreadProc, NULL, NULL, NULL);
}

void destroy_disassembler()
{
    DestroyWindow(disHwnd);
    TerminateThread(hThread, 0);
    CloseHandle(hThread);
}

void update_disassembler()
{
    if (disHwnd)
        SendMessage(disHwnd, UpdateMSG, 0, 0);
}