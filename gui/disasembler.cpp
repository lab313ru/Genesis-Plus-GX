#include <Windows.h>
#include <CommCtrl.h>
#include <Richedit.h>
#include <process.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>
#include <list>

#include "gui.h"
#include "disassembler.h"

#include "edit_fields.h"

#include "resource.h"

#include "debug.h"

namespace cap
{
#include "capstone/capstone.h"
}

#define DBG_EVENTS_TIMER 1
#define UPDATE_DISASM_TIMER 2
#define BYTES_BEFORE_PC 0x30
#define LINES_BEFORE_PC 15
#define LINES_MAX 25
#define DISASM_LISTING_BYTES 0x100
#define ROM_CODE_START_ADDR 0x200
#define DISASM_LISTING_BKGN (RGB(0xCC, 0xFF, 0xFF))

static HANDLE hThread = NULL;

static HWND disHwnd = NULL, listHwnd = NULL;
static std::string cliptext;
static cap::csh cs_handle;
static std::map<unsigned int, int> linesMap;
static bool pausedResumed = false;
static unsigned int last_pc = ROM_CODE_START_ADDR;

static std::string previousText;
static unsigned int currentControlFocus;

typedef struct {
    std::regex pattern;
    CHARFORMAT2 format;
} highlight_rule_t;

typedef struct {
    int line_index;
    COLORREF color;
} extra_selection_t;

static std::vector<highlight_rule_t> highlightingRules;
static CHARFORMAT2 keywordFormat, addressFormat, hexValueFormat, lineAddrFormat;
static std::list<extra_selection_t> extraSelections;

static void init_highlighter()
{
    highlight_rule_t rule;

    keywordFormat.cbSize = sizeof(CHARFORMAT2);
    keywordFormat.crTextColor = RGB(0, 0, 0x8B); // darkBlue
    keywordFormat.dwMask = CFM_BOLD | CFM_COLOR;
    keywordFormat.dwEffects = CFE_BOLD;

    std::list<std::string> keywordPatterns;
    keywordPatterns.push_back("\\babcd\\b");
    keywordPatterns.push_back("\\badd\\b");
    keywordPatterns.push_back("\\badda\\b");
    keywordPatterns.push_back("\\baddi\\b");
    keywordPatterns.push_back("\\baddq\\b");
    keywordPatterns.push_back("\\baddx\\b");
    keywordPatterns.push_back("\\band\\b");
    keywordPatterns.push_back("\\bandi\\b");
    keywordPatterns.push_back("\\basl\\b");
    keywordPatterns.push_back("\\basr\\b");
    keywordPatterns.push_back("\\bbchg\\b");
    keywordPatterns.push_back("\\bbclr\\b");
    keywordPatterns.push_back("\\bbset\\b");
    keywordPatterns.push_back("\\bbtst\\b");
    keywordPatterns.push_back("\\bchk\\b");
    keywordPatterns.push_back("\\bclr\\b");
    keywordPatterns.push_back("\\bcmp\\b");
    keywordPatterns.push_back("\\bcmpa\\b");
    keywordPatterns.push_back("\\bcmpi\\b");
    keywordPatterns.push_back("\\bcmpm\\b");
    keywordPatterns.push_back("\\bdivs\\b");
    keywordPatterns.push_back("\\bdivu\\b");
    keywordPatterns.push_back("\\beor\\b");
    keywordPatterns.push_back("\\beori\\b");
    keywordPatterns.push_back("\\bexg\\b");
    keywordPatterns.push_back("\\bext\\b");
    keywordPatterns.push_back("\\bextb\\b");
    keywordPatterns.push_back("\\billegal\\b");
    keywordPatterns.push_back("\\blea\\b");
    keywordPatterns.push_back("\\blink\\b");
    keywordPatterns.push_back("\\blsl\\b");
    keywordPatterns.push_back("\\blsr\\b");
    keywordPatterns.push_back("\\bmove\\b");
    keywordPatterns.push_back("\\bmovea\\b");
    keywordPatterns.push_back("\\bmovem\\b");
    keywordPatterns.push_back("\\bmovep\\b");
    keywordPatterns.push_back("\\bmoveq\\b");
    keywordPatterns.push_back("\\bmuls\\b");
    keywordPatterns.push_back("\\bmulu\\b");
    keywordPatterns.push_back("\\bnbcd\\b");
    keywordPatterns.push_back("\\bneg\\b");
    keywordPatterns.push_back("\\bnegx\\b");
    keywordPatterns.push_back("\\bnop\\b");
    keywordPatterns.push_back("\\bnot\\b");
    keywordPatterns.push_back("\\bor\\b");
    keywordPatterns.push_back("\\bori\\b");
    keywordPatterns.push_back("\\breset\\b");
    keywordPatterns.push_back("\\brol\\b");
    keywordPatterns.push_back("\\bror\\b");
    keywordPatterns.push_back("\\broxl\\b");
    keywordPatterns.push_back("\\broxr\\b");
    keywordPatterns.push_back("\\brte\\b");
    keywordPatterns.push_back("\\brtr\\b");
    keywordPatterns.push_back("\\brts\\b");
    keywordPatterns.push_back("\\bsbcd\\b");
    keywordPatterns.push_back("\\bscc\\b");
    keywordPatterns.push_back("\\bsge\\b");
    keywordPatterns.push_back("\\bsls\\b");
    keywordPatterns.push_back("\\bspl\\b");
    keywordPatterns.push_back("\\bscs\\b");
    keywordPatterns.push_back("\\bsgt\\b");
    keywordPatterns.push_back("\\bslt\\b");
    keywordPatterns.push_back("\\bst\\b");
    keywordPatterns.push_back("\\bseq\\b");
    keywordPatterns.push_back("\\bshi\\b");
    keywordPatterns.push_back("\\bsmi\\b");
    keywordPatterns.push_back("\\bsvc\\b");
    keywordPatterns.push_back("\\bsf\\b");
    keywordPatterns.push_back("\\bsle\\b");
    keywordPatterns.push_back("\\bsne\\b");
    keywordPatterns.push_back("\\bsvs\\b");
    keywordPatterns.push_back("\\bstop\\b");
    keywordPatterns.push_back("\\bsub\\b");
    keywordPatterns.push_back("\\bsuba\\b");
    keywordPatterns.push_back("\\bsubi\\b");
    keywordPatterns.push_back("\\bsubq\\b");
    keywordPatterns.push_back("\\bsubx\\b");
    keywordPatterns.push_back("\\bswap\\b");
    keywordPatterns.push_back("\\btas\\b");
    keywordPatterns.push_back("\\btrap\\b");
    keywordPatterns.push_back("\\btrapv\\b");
    keywordPatterns.push_back("\\btst\\b");
    keywordPatterns.push_back("\\bunlk\\b");
    keywordPatterns.push_back("\\bb\\b");
    keywordPatterns.push_back("\\bw\\b");
    keywordPatterns.push_back("\\bl\\b");
    keywordPatterns.push_back("\\bs\\b");

    keywordPatterns.push_back("\\bd0\\b");
    keywordPatterns.push_back("\\bd1\\b");
    keywordPatterns.push_back("\\bd2\\b");
    keywordPatterns.push_back("\\bd3\\b");
    keywordPatterns.push_back("\\bd4\\b");
    keywordPatterns.push_back("\\bd5\\b");
    keywordPatterns.push_back("\\bd6\\b");
    keywordPatterns.push_back("\\bd7\\b");
    keywordPatterns.push_back("\\ba0\\b");
    keywordPatterns.push_back("\\ba1\\b");
    keywordPatterns.push_back("\\ba2\\b");
    keywordPatterns.push_back("\\ba3\\b");
    keywordPatterns.push_back("\\ba4\\b");
    keywordPatterns.push_back("\\ba5\\b");
    keywordPatterns.push_back("\\ba6\\b");
    keywordPatterns.push_back("\\ba7\\b");
    keywordPatterns.push_back("\\bpc\\b");
    keywordPatterns.push_back("\\bsr\\b");
    keywordPatterns.push_back("\\bccr\\b");
    keywordPatterns.push_back("\\bsp\\b");
    keywordPatterns.push_back("\\busp\\b");
    keywordPatterns.push_back("\\bssp\\b");
    keywordPatterns.push_back("\\bisp\\b");
    keywordPatterns.push_back("\\bbcc\\b");
    keywordPatterns.push_back("\\bbhs\\b");
    keywordPatterns.push_back("\\bbge\\b");
    keywordPatterns.push_back("\\bbls\\b");
    keywordPatterns.push_back("\\bbpl\\b");
    keywordPatterns.push_back("\\bbcs\\b");
    keywordPatterns.push_back("\\bblo\\b");
    keywordPatterns.push_back("\\bbgt\\b");
    keywordPatterns.push_back("\\bblt\\b");
    keywordPatterns.push_back("\\bbeq\\b");
    keywordPatterns.push_back("\\bbhi\\b");
    keywordPatterns.push_back("\\bbmi\\b");
    keywordPatterns.push_back("\\bbvc\\b");
    keywordPatterns.push_back("\\bble\\b");
    keywordPatterns.push_back("\\bbne\\b");
    keywordPatterns.push_back("\\bbvs\\b");
    keywordPatterns.push_back("\\bbra\\b");
    keywordPatterns.push_back("\\bbsr\\b");
    keywordPatterns.push_back("\\bdbra\\b");
    keywordPatterns.push_back("\\bdbcc\\b");
    keywordPatterns.push_back("\\bdbge\\b");
    keywordPatterns.push_back("\\bdbls\\b");
    keywordPatterns.push_back("\\bdbpl\\b");
    keywordPatterns.push_back("\\bdbcs\\b");
    keywordPatterns.push_back("\\bdbgt\\b");
    keywordPatterns.push_back("\\bdblt\\b");
    keywordPatterns.push_back("\\bdbt\\b");
    keywordPatterns.push_back("\\bdbeq\\b");
    keywordPatterns.push_back("\\bdbhi\\b");
    keywordPatterns.push_back("\\bdbmi\\b");
    keywordPatterns.push_back("\\bdbvc\\b");
    keywordPatterns.push_back("\\bdbf\\b");
    keywordPatterns.push_back("\\bdble\\b");
    keywordPatterns.push_back("\\bdbne\\b");
    keywordPatterns.push_back("\\bdbvs\\b");
    keywordPatterns.push_back("\\bjmp\\b");
    keywordPatterns.push_back("\\bjsr\\b");

    for (std::string &pattern : keywordPatterns)
    {
        rule.pattern = std::regex(pattern);
        rule.format = keywordFormat;
        highlightingRules.push_back(rule);
    }

    addressFormat.cbSize = sizeof(CHARFORMAT2);
    addressFormat.crTextColor = RGB(0x8B, 0, 0x8B); // darkMagenta
    addressFormat.dwMask = CFM_BOLD | CFM_COLOR;
    addressFormat.dwEffects = CFE_BOLD;
    rule.pattern = std::regex("\\$\\b[A-Fa-f0-9]{1,8}\\b");
    rule.format = addressFormat;
    highlightingRules.push_back(rule);

    hexValueFormat.cbSize = sizeof(CHARFORMAT2);
    hexValueFormat.crTextColor = RGB(0, 0x64, 0); // darkGreen
    hexValueFormat.dwMask = CFM_BOLD | CFM_COLOR;
    hexValueFormat.dwEffects = CFE_BOLD;
    rule.pattern = std::regex("\\#\\$\\b[A-Fa-f0-9]{1,8}\\b");
    rule.format = hexValueFormat;
    highlightingRules.push_back(rule);

    lineAddrFormat.cbSize = sizeof(CHARFORMAT2);
    lineAddrFormat.crTextColor = RGB(0, 0, 0); // black
    lineAddrFormat.dwMask = CFM_BOLD | CFM_COLOR;
    lineAddrFormat.dwEffects = CFE_BOLD;
    rule.pattern = std::regex("^\\b[A-Fa-f0-9]{6}\\b");
    rule.format = lineAddrFormat;
    highlightingRules.push_back(rule);
}

static void clear_background_color()
{
    CHARRANGE cr, cr_old;
    cr.cpMin = INT_MAX;
    cr.cpMax = INT_MAX;
    SendMessage(listHwnd, EM_HIDESELECTION, 1, 0);
    SendMessage(listHwnd, EM_EXGETSEL, 0, (LPARAM)&cr_old);
    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr);

    SendMessage(listHwnd, EM_SETBKGNDCOLOR, 0, DISASM_LISTING_BKGN);
    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr_old);
    SendMessage(listHwnd, EM_HIDESELECTION, 0, 0);
}

static void set_selection_format(int start_pos, int length, const CHARFORMAT2 *cf)
{
    CHARRANGE cr_old;
    SendMessage(listHwnd, EM_EXGETSEL, 0, (LPARAM)&cr_old);
    SendMessage(listHwnd, EM_SETSEL, start_pos, start_pos + length);

    SendMessage(listHwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)cf);
    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr_old);
}

static void highligh_blocks()
{
    LockWindowUpdate(listHwnd);
    SendMessage(listHwnd, EM_HIDESELECTION, 1, 0);
    for (const highlight_rule_t &rule : highlightingRules)
    {
        for (std::sregex_iterator i = std::sregex_iterator(cliptext.cbegin(), cliptext.cend(), rule.pattern); i != std::sregex_iterator(); ++i)
        {
            std::smatch m = *i;
            set_selection_format(m.position(0), m.length(0), &rule.format);
        }
    }
    SendMessage(listHwnd, EM_HIDESELECTION, 0, 0);
    LockWindowUpdate(NULL);
}

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
    HWND hSRL = GetDlgItem(disHwnd, IDC_REG_SR_L);
    HWND hSRE = GetDlgItem(disHwnd, IDC_REG_SR);

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

    SetWindowPos(hSRL, NULL,
        left + widthL + 3 + widthE + 5,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + (height + 5),
        widthL,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hSRL, NULL, FALSE);

    SetWindowPos(hSRE, NULL,
        left + widthL + 3 + widthE + 5 + widthL + 3,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + (height + 5),
        widthE,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hSRE, NULL, FALSE);

    HWND F7 = GetDlgItem(disHwnd, IDC_STEP_INTO);
    HWND F8 = GetDlgItem(disHwnd, IDC_STEP_OVER);
    HWND F9 = GetDlgItem(disHwnd, IDC_RUN_PAUSE);
    GetClientRect(F7, &r2);

    SetWindowPos(F7, NULL,
        left,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + height + 10 + (height + 5) * 2,
        r2.right,
        r2.bottom,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(F7, NULL, FALSE);

    SetWindowPos(F8, NULL,
        left + (r2.right + 2) * 1,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + height + 10 + (height + 5) * 2,
        r2.right,
        r2.bottom,
        SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(F8, NULL, FALSE);

    SetWindowPos(F9, NULL,
        left + (r2.right + 2) * 2,
        top + 5 + (IDC_REG_A0 - IDC_REG_D0) * (height + 5) + 3 + height + 10 + (height + 5) * 2,
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
            dbg_req->data.regs_data.type = REG_TYPE_M68K;
            switch (LOWORD(wparam))
            {
            case IDC_REG_D0:
                dbg_req->data.regs_data.data.regs_68k.values.d0 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_D1:
                dbg_req->data.regs_data.data.regs_68k.values.d1 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_D2:
                dbg_req->data.regs_data.data.regs_68k.values.d2 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_D3:
                dbg_req->data.regs_data.data.regs_68k.values.d3 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_D4:
                dbg_req->data.regs_data.data.regs_68k.values.d4 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_D5:
                dbg_req->data.regs_data.data.regs_68k.values.d5 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_D6:
                dbg_req->data.regs_data.data.regs_68k.values.d6 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_D7:
                dbg_req->data.regs_data.data.regs_68k.values.d7 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A0:
                dbg_req->data.regs_data.data.regs_68k.values.a0 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A1:
                dbg_req->data.regs_data.data.regs_68k.values.a1 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A2:
                dbg_req->data.regs_data.data.regs_68k.values.a2 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A3:
                dbg_req->data.regs_data.data.regs_68k.values.a3 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A4:
                dbg_req->data.regs_data.data.regs_68k.values.a4 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A5:
                dbg_req->data.regs_data.data.regs_68k.values.a5 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A6:
                dbg_req->data.regs_data.data.regs_68k.values.a6 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_A7:
                dbg_req->data.regs_data.data.regs_68k.values.a7 = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_PC:
                dbg_req->data.regs_data.data.regs_68k.values.pc = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_SP:
                dbg_req->data.regs_data.data.regs_68k.values.sp = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_PPC:
                dbg_req->data.regs_data.data.regs_68k.values.ppc = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_REG_SR:
                dbg_req->data.regs_data.data.regs_68k.values.sr = GetDlgItemHex(hwnd, LOWORD(wparam));
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

    last_pc = reg_vals->pc;

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
    if (currentControlFocus != IDC_REG_SR)
        UpdateDlgItemHex(disHwnd, IDC_REG_SR, 4, reg_vals->sr);
}

static void set_listing_text()
{
    CHARRANGE cr_old;
    SendMessage(listHwnd, EM_HIDESELECTION, 1, 0);
    SendMessage(listHwnd, EM_EXGETSEL, 0, (LPARAM)&cr_old);
    SendMessage(listHwnd, WM_SETTEXT, 0, (LPARAM)cliptext.c_str());
    SendMessage(listHwnd, EM_EXSETSEL, 0, (LPARAM)&cr_old);
    SendMessage(listHwnd, EM_HIDESELECTION, 0, 0);
}

static void set_listing_font(const char *strFont, int nSize)
{
    CHARFORMAT cfFormat;
    memset(&cfFormat, 0, sizeof(cfFormat));
    cfFormat.cbSize = sizeof(cfFormat);
    cfFormat.crTextColor = RGB(0, 0, 0x8B);
    cfFormat.dwMask = CFM_CHARSET | CFM_FACE | CFM_SIZE | CFM_COLOR;
    cfFormat.bCharSet = ANSI_CHARSET;
    cfFormat.bPitchAndFamily = FIXED_PITCH | FF_DONTCARE;
    cfFormat.yHeight = (nSize * 1440) / 72;
    strcpy(cfFormat.szFaceName, strFont);
    SendMessage(listHwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cfFormat);

    clear_background_color();
}

static void applyExtraSelection()
{
    clear_background_color();

    SendMessage(listHwnd, EM_HIDESELECTION, 1, 0);
    for (extra_selection_t &selection : extraSelections)
    {
        int start_pos = SendMessage(listHwnd, EM_LINEINDEX, selection.line_index, 0);
        int end_pos = SendMessage(listHwnd, EM_LINELENGTH, start_pos, 0);

        CHARFORMAT2 cf;
        memset(&cf, 0, sizeof cf);
        cf.cbSize = sizeof cf;
        cf.dwMask = CFM_BACKCOLOR;
        cf.crBackColor = selection.color;

        set_selection_format(start_pos, end_pos, &cf);
    }
    SendMessage(listHwnd, EM_HIDESELECTION, 0, 0);
}

static void clearExtraSelection()
{
    extraSelections.clear();
    applyExtraSelection();
}

static void addExtraSelection(unsigned int address, COLORREF color)
{
    std::map<unsigned int, int>::const_iterator i = linesMap.cbegin();
    while (i != linesMap.end())
    {
        if (i->first != address)
        {
            ++i;
            continue;
        }

        extra_selection_t extra = { i->second, color };
        extraSelections.push_back(extra);
        applyExtraSelection();
        break;
    }
}

static void scrollToAddress(unsigned int address)
{
    std::map<unsigned int, int>::const_iterator i = linesMap.cbegin();
    while (i != linesMap.end())
    {
        if (i->first != address)
        {
            ++i;
            continue;
        }

        //int start_pos = SendMessage(listHwnd, EM_LINEINDEX, max(0, i->second - 15), 0);
        //int end_pos = SendMessage(listHwnd, EM_LINELENGTH, start_pos, 0);

        SendMessage(listHwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, max(0, i->second - 15)), 0);
        break;
    }
}

static void get_disasm_listing_pc(unsigned int pc, const unsigned char *code, size_t code_size, int max_lines)
{
    linesMap.clear();
    cliptext.clear();

    cap::cs_insn *insn = cap::cs_malloc(cs_handle);

    const uint8_t *code_ptr = (const uint8_t *)code;
    uint64_t start_addr = max(pc - BYTES_BEFORE_PC, (pc < 0xA00000) ? ROM_CODE_START_ADDR : 0xFF0000);
    uint64_t address = start_addr;

    int lines = 0, pc_line = 0;

    bool ep_fixed = false;

    while (lines < max_lines && code_size && cs_disasm_iter(cs_handle, &code_ptr, &code_size, &address, insn))
    {
        if (!ep_fixed && insn->address >= pc)
        {
            pc_line = lines;

            unsigned int pc_data_offset = pc;
            pc_data_offset = (pc_data_offset < 0xA00000) ? (pc_data_offset - start_addr) : ((pc_data_offset - start_addr) & 0xFFFF);

            code_ptr = (const uint8_t *)(&code[pc_data_offset]);
            address = pc;
            code_size = (code_size >= pc_data_offset) ? (code_size - pc_data_offset) : code_size;
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
    highligh_blocks();
    clearExtraSelection();

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
        unsigned int real_pc = pc;
        pc = max(pc - BYTES_BEFORE_PC, ROM_CODE_START_ADDR);
        dbg_req->data.mem_data.address = pc;
        dbg_req->data.mem_data.size = DISASM_LISTING_BYTES;
        dbg_req->req_type = REQ_READ_68K_ROM;
        send_dbg_request();

        get_disasm_listing_pc(real_pc, &dbg_req->data.mem_data.data.m68k_rom[pc], dbg_req->data.mem_data.size, LINES_MAX);
    }
}

static void update_dbg_window_info()
{
    update_regs();
    update_disasm_listing(last_pc);
}

static void do_game_started(unsigned int pc)
{
}

static void do_game_paused(unsigned int pc)
{
    pausedResumed = true;
    update_dbg_window_info();

    SetForegroundWindow(disHwnd);
}

static void do_game_stopped()
{
}

static void check_debugger_events()
{
    if (!is_debugger_active())
        return;

    if (!recv_dbg_event(0))
        return;

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
        listHwnd = GetDlgItem(hWnd, IDC_DISASM_LIST);
        SetFocus(listHwnd);
        set_listing_font("Liberation Mono", 8);

        CheckDlgButton(hWnd, IDC_EXEC_BPT, BST_CHECKED);
        CheckDlgButton(hWnd, IDC_BPT_IS_READ, BST_CHECKED);
        CheckDlgButton(hWnd, IDC_BPT_IS_WRITE, BST_CHECKED);

        HWND bp_sizes = GetDlgItem(hWnd, IDC_BPT_SIZE);
        SendMessage(bp_sizes, CB_ADDSTRING, 0, (LPARAM)"1");
        SendMessage(bp_sizes, CB_ADDSTRING, 0, (LPARAM)"2");
        SendMessage(bp_sizes, CB_ADDSTRING, 0, (LPARAM)"4");
        SendMessage(bp_sizes, CB_SETCURSEL, 0, 0);

        UpdateDlgItemHex(hWnd, IDC_BPT_ADDR, 6, ROM_CODE_START_ADDR);

        SetTimer(hWnd, DBG_EVENTS_TIMER, 10, NULL);
        SetTimer(hWnd, UPDATE_DISASM_TIMER, 2000, NULL);
    } break;
    case WM_TIMER:
    {
        switch (LOWORD(wParam))
        {
        case DBG_EVENTS_TIMER: check_debugger_events(); break;
        case UPDATE_DISASM_TIMER:
        {
            if (!pausedResumed)
                update_dbg_window_info();
        } break;
        }
        return FALSE;
    } break;
    case WM_SIZE:
    {
        resize_func();
        break;
    }
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_STEP_INTO:
        case IDC_STEP_INTO_HK:
            dbg_req->req_type = REQ_STEP_INTO;
            send_dbg_request();
            break;
        case IDC_STEP_OVER:
        case IDC_STEP_OVER_HK:
            dbg_req->req_type = REQ_STEP_OVER;
            send_dbg_request();
            break;
        case IDC_RUN_PAUSE:
        case IDC_RUN_PAUSE_HK:
            dbg_req->req_type = pausedResumed ? REQ_RESUME : REQ_PAUSE;
            send_dbg_request();
            pausedResumed = !pausedResumed;
            break;
        }

        return TRUE;
    } break;
    case WM_DESTROY:
    {
        KillTimer(hWnd, UPDATE_DISASM_TIMER);
        KillTimer(hWnd, DBG_EVENTS_TIMER);
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
    skipdata.mnemonic = "dc.b";
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

    HACCEL hAccelTable = LoadAccelerators(dbg_wnd_hinst, MAKEINTRESOURCE(ACCELERATOR_RESOURCE_ID));
    MSG messages;
    HMODULE hRich = LoadLibrary("Riched32.dll");

    disHwnd = CreateDialog(dbg_wnd_hinst, MAKEINTRESOURCE(IDD_DISASSEMBLER), dbg_window, (DLGPROC)DisasseblerWndProc);
    ShowWindow(disHwnd, SW_SHOW);
    UpdateWindow(disHwnd);

    init_highlighter();

    resize_func();

    while (GetMessage(&messages, NULL, 0, 0))
    {
        if (!TranslateAccelerator(disHwnd, hAccelTable, &messages))
        {
            TranslateMessage(&messages);
            DispatchMessage(&messages);
        }
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
    stop_debugging();
    DestroyWindow(disHwnd);
    TerminateThread(hThread, 0);
    CloseHandle(hThread);
}

void update_disassembler()
{
    if (is_debugger_active())
        handle_dbg_commands();
}