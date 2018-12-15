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

#include "debug.h"

namespace cap
{
    #include "capstone/capstone.h"
}

#define UPDATE_DISASM_TIMER 1
#define BYTES_BEFORE_PC 0x30
#define DISASM_LISTING_BYTES 0x100

static HANDLE hThread = NULL;

static HWND disHwnd = NULL, listHwnd = NULL;
static std::string cliptext;
static cap::csh cs_handle;
static std::map<unsigned int, int> linesMap;
static bool pausedResumed = false;

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
                dbg_req->data.regs_data.type = REG_TYPE_M68K;
                dbg_req->data.regs_data.data.regs_68k.array[LOWORD(wparam) - IDC_REG_D0] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_PC:
                dbg_req->data.regs_data.data.regs_68k.values.pc = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_SP:
                dbg_req->data.regs_data.data.regs_68k.values.sp =  GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_PPC:
                dbg_req->data.regs_data.data.regs_68k.values.ppc = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            }

            dbg_req->req_type = REQ_SET_REGS;
            send_dbg_request();
        }
    }

    return TRUE;
}

static void update_regs()
{
    dbg_req->data.regs_data.type = REG_TYPE_M68K;
    dbg_req->req_type = REQ_GET_REGS;
    send_dbg_request();

    regs_68k_data_t *reg_vals = &dbg_req->data.regs_data.data.regs_68k.values;

    if (currentControlFocus != IDC_REG_D0)
        UpdateDlgItemHex(disHwnd, IDC_REG_D0, 8, reg_vals->d0);
    if (currentControlFocus != IDC_REG_D1)
        UpdateDlgItemHex(disHwnd, IDC_REG_D1, 8, reg_vals->d1);
    if (currentControlFocus != IDC_REG_D2)
        UpdateDlgItemHex(disHwnd, IDC_REG_D2, 8, reg_vals->d2);
    if (currentControlFocus != IDC_REG_D3)
        UpdateDlgItemHex(disHwnd, IDC_REG_D3, 8, reg_vals->d3);
    if (currentControlFocus != IDC_REG_D4)
        UpdateDlgItemHex(disHwnd, IDC_REG_D4, 8, reg_vals->d4);
    if (currentControlFocus != IDC_REG_D5)
        UpdateDlgItemHex(disHwnd, IDC_REG_D5, 8, reg_vals->d5);
    if (currentControlFocus != IDC_REG_D6)
        UpdateDlgItemHex(disHwnd, IDC_REG_D6, 8, reg_vals->d6);
    if (currentControlFocus != IDC_REG_D7)
        UpdateDlgItemHex(disHwnd, IDC_REG_D7, 8, reg_vals->d7);

    if (currentControlFocus != IDC_REG_A0)
        UpdateDlgItemHex(disHwnd, IDC_REG_A0, 8, reg_vals->a0);
    if (currentControlFocus != IDC_REG_A1)
        UpdateDlgItemHex(disHwnd, IDC_REG_A1, 8, reg_vals->a1);
    if (currentControlFocus != IDC_REG_A2)
        UpdateDlgItemHex(disHwnd, IDC_REG_A2, 8, reg_vals->a2);
    if (currentControlFocus != IDC_REG_A3)
        UpdateDlgItemHex(disHwnd, IDC_REG_A3, 8, reg_vals->a3);
    if (currentControlFocus != IDC_REG_A4)
        UpdateDlgItemHex(disHwnd, IDC_REG_A4, 8, reg_vals->a4);
    if (currentControlFocus != IDC_REG_A5)
        UpdateDlgItemHex(disHwnd, IDC_REG_A5, 8, reg_vals->a5);
    if (currentControlFocus != IDC_REG_A6)
        UpdateDlgItemHex(disHwnd, IDC_REG_A6, 8, reg_vals->a6);
    if (currentControlFocus != IDC_REG_A7)
        UpdateDlgItemHex(disHwnd, IDC_REG_A7, 8, reg_vals->a7);

    if (currentControlFocus != IDC_REG_PC)
        UpdateDlgItemHex(disHwnd, IDC_REG_PC, 8, reg_vals->pc);
    if (currentControlFocus != IDC_REG_SP)
        UpdateDlgItemHex(disHwnd, IDC_REG_SP, 8, reg_vals->sp);
    if (currentControlFocus != IDC_REG_PPC)
        UpdateDlgItemHex(disHwnd, IDC_REG_PPC, 8, reg_vals->ppc);

    dbg_req->req_type = REQ_NO_REQUEST;
}

static void set_listing_text()
{
    CHARRANGE cr, cr_old;
    cr.cpMin = -0;
    cr.cpMax = -1;

    LockWindowUpdate(listHwnd);
    SendMessage(listHwnd, EM_EXGETSEL, 0, (LPARAM)&cr_old);
    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessage(listHwnd, EM_REPLACESEL, 0, (LPARAM)cliptext.c_str());
    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr_old);
    LockWindowUpdate(NULL);
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
    uint64_t start_addr = max((int)(pc - BYTES_BEFORE_PC), (pc < 0xA00000) ? 0 : 0xFF0000);
    uint64_t address = start_addr;

    int lines = 0;

    bool ep_fixed = false;

    while (code_size && cs_disasm_iter(cs_handle, &code_ptr, &code_size, &address, insn))
    {
        if (!ep_fixed && insn->address >= pc)
        {
            unsigned int pc_data_offset = pc;
            pc_data_offset = (pc_data_offset < 0xA00000) ? (pc_data_offset - start_addr) : ((pc_data_offset - start_addr) & 0xFFFF);

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

static void update_disasm_listing(unsigned int pc)
{
    if (pc < 0xA00000)
    {
        pc = max(pc - BYTES_BEFORE_PC, 0);
        dbg_req->data.mem_data.address = pc;
        dbg_req->data.mem_data.size = DISASM_LISTING_BYTES;
        dbg_req->req_type = REQ_READ_68K_ROM;
        send_dbg_request();

        get_disasm_listing_pc(pc, dbg_req->data.mem_data.data.m68k_rom, dbg_req->data.mem_data.size);
    }
}

static void do_game_started(unsigned int pc)
{

}

static void do_game_paused(unsigned int pc)
{
    pausedResumed = true;
    update_regs();
    update_disasm_listing(pc);
}

static void do_game_stopped()
{

}

static void check_debugger_events()
{
    if (!is_debugger_active())
        return;

    recv_dbg_event();

    debugger_event_t *dbg_event = &dbg_req->dbg_evt;

    switch (dbg_event->type)
    {
    case DBG_EVT_STARTED: do_game_started(dbg_event->pc); break;
    case DBG_EVT_PAUSED: do_game_paused(dbg_event->pc); break;
    case DBG_EVT_STOPPED: do_game_stopped(); break;
    default:
        break;
    }

    dbg_event->type = DBG_EVT_NO_EVENT;
}

LRESULT CALLBACK DisasseblerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        SetTimer(hWnd, UPDATE_DISASM_TIMER, 10, NULL);
    } break;
    case WM_TIMER:
    {
        check_debugger_events();
    } break;
    case WM_SIZE:
    {
        resize_func();
        break;
    }
    case WM_DESTROY:
    {
        KillTimer(hWnd, UPDATE_DISASM_TIMER);
        PostQuitMessage(0);
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
    openCapstone();

    MSG messages;
    HMODULE hRich = LoadLibrary("Riched32.dll");

    disHwnd = CreateDialog(dbg_wnd_hinst, MAKEINTRESOURCE(IDD_DISASSEMBLER), dbg_window, (DLGPROC)DisasseblerWndProc);
    ShowWindow(disHwnd, SW_SHOW);
    UpdateWindow(disHwnd);

    listHwnd = GetDlgItem(disHwnd, IDC_DISASM_LIST);
    SetFocus(listHwnd);
    set_listing_font("Liberation Mono", 16);

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
    start_debugging();
    hThread = CreateThread(0, NULL, ThreadProc, NULL, NULL, NULL);
}

void destroy_disassembler()
{
    DestroyWindow(disHwnd);
    TerminateThread(hThread, 0);
    CloseHandle(hThread);
    stop_debugging();
}

void update_disassembler()
{
    if (is_debugger_active())
        handle_dbg_commands();
}