#include <Windows.h>
#include <CommCtrl.h>
#include <Richedit.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "gui.h"
#include "disassembler.h"

#include "edit_fields.h"

#include "resource.h"

#include "shared.h"
#include "m68k.h"
#include "debug.h"

namespace cap
{
    #include "capstone/capstone.h"
}

#define BYTES_BEFORE_PC 0x30

static HANDLE hThread = NULL;

static HWND disHwnd = NULL, listHwnd = NULL;
static std::string cliptext;
static cap::csh cs_handle;
static std::map<unsigned int, int> linesMap;

static std::string previousText;
static unsigned int currentControlFocus;

static void resize_func()
{
    RECT r, r2;
    GetWindowRect(disHwnd, &r);

    SetWindowPos(listHwnd, NULL,
        r.left,
        r.top,
        (r.right - r.left) / 3,
        r.bottom - r.top - 50,
        SWP_NOZORDER | SWP_NOACTIVATE);

    GetWindowRect(listHwnd, &r);
    MapWindowPoints(HWND_DESKTOP, disHwnd, (LPPOINT)&r, 2);

    int left = r.right + 20;
    int top = r.top + 5;

    const int widthL = 30; // label
    const int height = 21;
    const int widthE = 80; // edit

    for (int i = 0; i <= (IDC_REG_D7 - IDC_REG_D0); ++i)
    {
        HWND hDL = GetDlgItem(disHwnd, IDC_REG_D0_L + i);
        HWND hDE = GetDlgItem(disHwnd, IDC_REG_D0 + i);
        HWND hAL = GetDlgItem(disHwnd, IDC_REG_A0_L + i);
        HWND hAE = GetDlgItem(disHwnd, IDC_REG_A0 + i);

        SetWindowPos(hDL, HWND_TOP,
            left,
            top + i * (height + 5) + 3,
            widthL,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hDL, NULL, FALSE);

        SetWindowPos(hDE, HWND_TOP,
            left + widthL + 3,
            top + i * (height + 5),
            widthE,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hDE, NULL, FALSE);

        SetWindowPos(hAL, HWND_TOP,
            left + widthL + 3 + widthE + 5,
            top + i * (height + 5) + 3,
            widthL,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hAL, NULL, FALSE);

        SetWindowPos(hAE, HWND_TOP,
            left + widthL + 3 + widthE + 5 + widthL + 3,
            top + i * (height + 5),
            widthE,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hAE, NULL, FALSE);
    }

    HWND hPCL = GetDlgItem(disHwnd, IDC_REG_PC_L);
    HWND hPCE = GetDlgItem(disHwnd, IDC_REG_PC);
    HWND hSPL = GetDlgItem(disHwnd, IDC_REG_SP_L);
    HWND hSPE = GetDlgItem(disHwnd, IDC_REG_SP);
    HWND hPPCL = GetDlgItem(disHwnd, IDC_REG_PPC_L);
    HWND hPPCE = GetDlgItem(disHwnd, IDC_REG_PPC);

    SetWindowPos(hPCL, NULL,
        left,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3,
        widthL,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hPCL, NULL, FALSE);

    SetWindowPos(hPCE, NULL,
        left + widthL + 3,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5),
        widthE,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hPCE, NULL, FALSE);

    SetWindowPos(hSPL, NULL,
        left + widthL + 3 + widthE + 5,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3,
        widthL,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hSPL, NULL, FALSE);

    SetWindowPos(hSPE, NULL,
        left + widthL + 3 + widthE + 5 + widthL + 3,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5),
        widthE,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hSPE, NULL, FALSE);

    SetWindowPos(hPPCL, NULL,
        left,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + (height + 5),
        widthL,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hPPCL, NULL, FALSE);

    SetWindowPos(hPPCE, NULL,
        left + widthL + 3,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + (height + 5),
        widthE,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hPPCE, NULL, FALSE);

    HWND F7 = GetDlgItem(disHwnd, IDC_STEP_INTO);
    HWND F8 = GetDlgItem(disHwnd, IDC_STEP_OVER);
    HWND F9 = GetDlgItem(disHwnd, IDC_RUN_PAUSE);
    GetClientRect(F7, &r2);

    SetWindowPos(F7, NULL,
        left,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + height + 10 + (height + 5),
        r2.right,
        r2.bottom,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(F7, NULL, FALSE);

    SetWindowPos(F8, NULL,
        left + (r2.right + 2) * 1,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + height + 10 + (height + 5),
        r2.right,
        r2.bottom,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(F8, NULL, FALSE);

    SetWindowPos(F9, NULL,
        left + (r2.right + 2) * 2,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + height + 10 + (height + 5),
        r2.right,
        r2.bottom,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(F9, NULL, FALSE);
}

INT_PTR cpuWM_COMMAND(HWND hwnd, WPARAM wparam, LPARAM lparam)
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
            case IDC_REG_PPC:
                m68k_set_reg(M68K_REG_PPC, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            }
        }
    }

    return TRUE;
}

static void update_regs()
{
    if (currentControlFocus != IDC_REG_D0)
        UpdateDlgItemHex(disHwnd, IDC_REG_D0, 8, m68k_get_reg(M68K_REG_D0));
    if (currentControlFocus != IDC_REG_D1)
        UpdateDlgItemHex(disHwnd, IDC_REG_D1, 8, m68k_get_reg(M68K_REG_D1));
    if (currentControlFocus != IDC_REG_D2)
        UpdateDlgItemHex(disHwnd, IDC_REG_D2, 8, m68k_get_reg(M68K_REG_D2));
    if (currentControlFocus != IDC_REG_D3)
        UpdateDlgItemHex(disHwnd, IDC_REG_D3, 8, m68k_get_reg(M68K_REG_D3));
    if (currentControlFocus != IDC_REG_D4)
        UpdateDlgItemHex(disHwnd, IDC_REG_D4, 8, m68k_get_reg(M68K_REG_D4));
    if (currentControlFocus != IDC_REG_D5)
        UpdateDlgItemHex(disHwnd, IDC_REG_D5, 8, m68k_get_reg(M68K_REG_D5));
    if (currentControlFocus != IDC_REG_D6)
        UpdateDlgItemHex(disHwnd, IDC_REG_D6, 8, m68k_get_reg(M68K_REG_D6));
    if (currentControlFocus != IDC_REG_D7)
        UpdateDlgItemHex(disHwnd, IDC_REG_D7, 8, m68k_get_reg(M68K_REG_D7));

    if (currentControlFocus != IDC_REG_A0)
        UpdateDlgItemHex(disHwnd, IDC_REG_A0, 8, m68k_get_reg(M68K_REG_A0));
    if (currentControlFocus != IDC_REG_A1)
        UpdateDlgItemHex(disHwnd, IDC_REG_A1, 8, m68k_get_reg(M68K_REG_A1));
    if (currentControlFocus != IDC_REG_A2)
        UpdateDlgItemHex(disHwnd, IDC_REG_A2, 8, m68k_get_reg(M68K_REG_A2));
    if (currentControlFocus != IDC_REG_A3)
        UpdateDlgItemHex(disHwnd, IDC_REG_A3, 8, m68k_get_reg(M68K_REG_A3));
    if (currentControlFocus != IDC_REG_A4)
        UpdateDlgItemHex(disHwnd, IDC_REG_A4, 8, m68k_get_reg(M68K_REG_A4));
    if (currentControlFocus != IDC_REG_A5)
        UpdateDlgItemHex(disHwnd, IDC_REG_A5, 8, m68k_get_reg(M68K_REG_A5));
    if (currentControlFocus != IDC_REG_A6)
        UpdateDlgItemHex(disHwnd, IDC_REG_A6, 8, m68k_get_reg(M68K_REG_A6));
    if (currentControlFocus != IDC_REG_A7)
        UpdateDlgItemHex(disHwnd, IDC_REG_A7, 8, m68k_get_reg(M68K_REG_A7));
    if (currentControlFocus != IDC_REG_PC)
        UpdateDlgItemHex(disHwnd, IDC_REG_PC, 8, m68k_get_reg(M68K_REG_PC));
    if (currentControlFocus != IDC_REG_SP)
        UpdateDlgItemHex(disHwnd, IDC_REG_SP, 8, m68k_get_reg(M68K_REG_SP));
    if (currentControlFocus != IDC_REG_PPC)
        UpdateDlgItemHex(disHwnd, IDC_REG_PPC, 8, m68k_get_reg(M68K_REG_PPC));
}

static DWORD CALLBACK EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    std::string *writes = (std::string *)dwCookie;	//cast as string

    if (writes->size() < cb)
    {
        *pcb = (LONG)writes->size();
        memcpy(pbBuff, (void *)writes->data(), *pcb);
        writes->erase();
    }
    else
    {
        *pcb = cb;
        memcpy(pbBuff, (void *)writes->data(), *pcb);
        writes->erase(0, cb);
    }

    return 0;
}

static void set_listing_text()
{
    CHARRANGE cr;
    cr.cpMin = 0;
    cr.cpMax = -1;

    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessage(listHwnd, EM_REPLACESEL, 0, (LPARAM)cliptext.c_str());
}

static void set_listing_font(const char *strFont, int nSize)
{
    // Setup char format
	CHARFORMAT cfFormat;
	memset(&cfFormat, 0, sizeof(cfFormat));
	cfFormat.cbSize = sizeof(cfFormat);
	cfFormat.dwMask = CFM_CHARSET | CFM_FACE | CFM_SIZE;
	cfFormat.bCharSet = ANSI_CHARSET;
	cfFormat.bPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
	cfFormat.yHeight = (nSize*1440)/72;
	strcpy(cfFormat.szFaceName, strFont);

	// Set char format and goto end of text
	CHARRANGE cr;
	cr.cpMin = INT_MAX;
	cr.cpMax = INT_MAX;
	SendMessage(listHwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfFormat);
    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr);
}

static void addExtraSelection(unsigned int address, COLORREF color)
{
    std::map<unsigned int, int>::const_iterator i = linesMap.cbegin();
    while (i != linesMap.end())
    {
        if (i->first == address)
        {
            int start_pos = SendMessage(listHwnd, EM_LINEINDEX, i->second, 0);
            int end_pos = SendMessage(listHwnd, EM_LINELENGTH, start_pos, 0);

            SendMessage(listHwnd, EM_SETSEL, start_pos, start_pos + end_pos);
            CHARFORMAT cf;
            memset( &cf, 0, sizeof cf );
            cf.cbSize = sizeof cf;
            cf.dwMask = CFM_COLOR;
            cf.crTextColor = color;
            SendMessage(listHwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            break;
        }

        ++i;
    }
}

static void scrollToAddress(unsigned int address)
{
    std::map<unsigned int, int>::const_iterator i = linesMap.cbegin();
    while (i != linesMap.end())
    {
        if (i->first == address)
        {
            int start_pos = SendMessage(listHwnd, EM_LINEINDEX, max(0, i->second - 15), 0);
            int end_pos = SendMessage(listHwnd, EM_LINELENGTH, start_pos, 0);

            SendMessage(listHwnd, WM_VSCROLL, SB_BOTTOM, 0L);

            SendMessage(listHwnd, EM_LINESCROLL, 0, start_pos);
            //setTextCursor(QTextCursor(document()->findBlockByLineNumber()));
            //ensureCursorVisible();
            break;
        }

        ++i;
    }
}

static void get_disasm_listing_pc(unsigned int pc, const unsigned char *code, size_t code_size)
{
    linesMap.clear();
    cliptext.clear();

    cap::cs_insn *insn = cap::cs_malloc(cs_handle);

    const uint8_t *code_ptr = (const uint8_t *)code;
    uint64_t start_addr = max((int)(pc - BYTES_BEFORE_PC), (pc < MAXROMSIZE) ? 0 : 0xFF0000);
    uint64_t address = start_addr;

    int lines = 0;

    bool ep_fixed = false;

    while (code_size && cs_disasm_iter(cs_handle, &code_ptr, &code_size, &address, insn))
    {
        if (!ep_fixed && insn->address >= pc)
        {
            unsigned int pc_data_offset = pc;
            pc_data_offset = (pc_data_offset < MAXROMSIZE) ? (pc_data_offset - start_addr) : ((pc_data_offset - start_addr) & 0xFFFF);

            code_ptr = (const uint8_t *)(&code[pc_data_offset]);
            address = pc;
            code_size = code_size - pc_data_offset;
            ep_fixed = true;
            continue;
        }

        linesMap.insert(std::make_pair(insn->address, lines));

        std::string line;

        std::string addrString = make_hex_string(6, insn->address);
        line.append(addrString);

        std::string mnemo(insn->mnemonic);

        std::ostringstream temp;
        temp << mnemo << " ";
        
        for (int i = 0; i < (7 - mnemo.length()); ++i)
            temp << " ";

        std::string mnemonicString = temp.str();
        std::string operandsString(insn->op_str);
        //operandsString.erase(std::remove_if(operandsString.begin(), operandsString.end(), isspace), operandsString.end());

        line.append(" ");
        line.append(mnemonicString);
        line.append(" ");
        line.append(operandsString);
        line.append("\n");

        cliptext.append(line);
        lines++;
    }

    cs_free(insn, 1);

    set_listing_text();
    //clearExtraSelection();

    addExtraSelection(pc, RGB(0x60, 0xD0, 0xFF));
    scrollToAddress(pc);

    //foreach (const BreakpointItem &item, bptList)
    //{
    //    addExtraSelection(item.address, Qt::red);

    //    if (item.address == pc)
    //        changeExtraSelectionColor(pc, QColor(0xA0, 0xA0, 0xFF));
    //}
}

static void update_disasm_listing()
{
    const unsigned char code[] = { 0x4E, 0x75, 0x4E, 0x75, 0x4E, 0x75, 0x4E, 0x75, 0x4E, 0x75, 0x4E, 0x75 };
    get_disasm_listing_pc(0x200, code, sizeof(code));
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
    case UpdateMSG:
    {
        update_regs();
        update_disasm_listing();
    } break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

static bool openCapstone()
{
    bool error = (cap::cs_open(cap::CS_ARCH_M68K, cap::CS_MODE_M68K_000, &cs_handle) == cap::CS_ERR_OK);

    cap::cs_opt_skipdata skipdata;
    skipdata.callback = NULL;
    skipdata.mnemonic = "db";
    skipdata.user_data = NULL;

    cap::cs_option(cs_handle, cap::CS_OPT_SKIPDATA_SETUP, (size_t)&skipdata);

    cap::cs_option(cs_handle, cap::CS_OPT_SKIPDATA, cap::CS_OPT_ON);

    return error;
}

static void closeCapstone()
{
    cap::cs_close(&cs_handle);
}

static DWORD WINAPI ThreadProc(LPVOID lpParam)
{
    MSG messages;

    openCapstone();

    HMODULE hRich = LoadLibrary("Riched32.dll");

    disHwnd = CreateDialog(dbg_wnd_hinst, MAKEINTRESOURCE(IDD_DISASSEMBLER), dbg_window, (DLGPROC)DisasseblerWndProc);
    ShowWindow(disHwnd, SW_SHOW);
    UpdateWindow(disHwnd);

    listHwnd = GetDlgItem(disHwnd, IDC_DISASM_LIST);
    SetFocus(listHwnd);
    set_listing_font("Liberation Mono", 8);

    resize_func();

    while (GetMessage(&messages, disHwnd, 0, 0))
    {
        TranslateMessage(&messages);
        DispatchMessage(&messages);
    }

    FreeLibrary(hRich);

    closeCapstone();

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