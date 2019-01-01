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

#include "debug_wrap.h"

namespace cap
{
#include "capstone/capstone.h"
}

#define DBG_EVENTS_TIMER 1
#define DBG_WHEN_IDA_UPDATE 2
#define BYTES_BEFORE_PC 0x30
#define LINES_BEFORE_PC 15
#define LINES_MAX 25
#define DISASM_LISTING_BYTES 0x200
#define ROM_CODE_START_ADDR ((unsigned int)0x200)
#define RAM_START_ADDR ((unsigned int)0xFF0000)
#define DISASM_LISTING_BKGN (RGB(0xCC, 0xFF, 0xFF))
#define CURR_PC_COLOR (RGB(0x60, 0xD0, 0xFF))
#define BPT_COLOR (RGB(0xFF, 0, 0))
#define BPT_COLOR_DISABLED (RGB(0, 0xFF, 0))
#define BPT_COLOR_PC (RGB(0xA0, 0xA0, 0xFF))
#define BPT_COLOR_PC_DISABLED (RGB(0x20, 0x80, 0x40))

static dbg_request_t *dbg_req = NULL;
static HANDLE hThread = NULL;

static HMODULE hRich = NULL;
static HWND disHwnd = NULL, listHwnd = NULL;
static std::string cliptext;
static cap::csh cs_handle;
static std::map<unsigned int, int> linesMap;
static bool paused = false;
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
static std::vector<extra_selection_t> extraSelections;

static void init_highlighter()
{
    highlightingRules.clear();

    highlight_rule_t rule;

    keywordFormat.cbSize = sizeof(CHARFORMAT2);
    keywordFormat.crTextColor = RGB(0, 0, 0x8B); // darkBlue
    keywordFormat.dwMask = CFM_BOLD | CFM_COLOR;
    keywordFormat.dwEffects = CFE_BOLD;

    addressFormat.cbSize = sizeof(CHARFORMAT2);
    addressFormat.crTextColor = RGB(0x8B, 0, 0x8B); // darkMagenta
    addressFormat.dwMask = CFM_BOLD | CFM_COLOR;
    addressFormat.dwEffects = CFE_BOLD;
    rule.pattern = std::regex("\\$\\b[A-Fa-f0-9]{1,8}\\b", std::regex::optimize | std::regex::nosubs);
    rule.format = addressFormat;
    highlightingRules.push_back(rule);

    hexValueFormat.cbSize = sizeof(CHARFORMAT2);
    hexValueFormat.crTextColor = RGB(0, 0x64, 0); // darkGreen
    hexValueFormat.dwMask = CFM_BOLD | CFM_COLOR;
    hexValueFormat.dwEffects = CFE_BOLD;
    rule.pattern = std::regex("\\#\\$\\b[A-Fa-f0-9]{1,8}\\b", std::regex::optimize | std::regex::nosubs);
    rule.format = hexValueFormat;
    highlightingRules.push_back(rule);

    lineAddrFormat.cbSize = sizeof(CHARFORMAT2);
    lineAddrFormat.crTextColor = RGB(0, 0, 0); // black
    lineAddrFormat.dwMask = CFM_BOLD | CFM_COLOR;
    lineAddrFormat.dwEffects = CFE_BOLD;
    rule.pattern = std::regex("^\\b[A-Fa-f0-9]{6,8}\\b", std::regex::optimize | std::regex::nosubs);
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

    const char *text = cliptext.c_str();
    const char *text_end = text + cliptext.length();
    for (const highlight_rule_t &rule : highlightingRules)
    {
        for (std::cregex_iterator i = std::cregex_iterator(text, text_end, rule.pattern); i != std::cregex_iterator(); ++i)
        {
            std::cmatch m = *i;
            set_selection_format((int)m.position(0), (int)m.length(0), &rule.format);
        }
    }
    
    SendMessage(listHwnd, EM_HIDESELECTION, 0, 0);
    LockWindowUpdate(NULL);
}

static void disable_when_ida()
{
    if (dbg_req == NULL)
        return;

    EnableWindow(listHwnd, dbg_req->is_ida ? FALSE : TRUE);

    for (int i = 0; i <= (IDC_REG_D7 - IDC_REG_D0); ++i)
    {
        HWND hDL = GetDlgItem(disHwnd, IDC_REG_D0_L + i);
        HWND hDE = GetDlgItem(disHwnd, IDC_REG_D0 + i);
        HWND hAL = GetDlgItem(disHwnd, IDC_REG_A0_L + i);
        HWND hAE = GetDlgItem(disHwnd, IDC_REG_A0 + i);
        EnableWindow(hDL, dbg_req->is_ida ? FALSE : TRUE);
        EnableWindow(hDE, dbg_req->is_ida ? FALSE : TRUE);
        EnableWindow(hAL, dbg_req->is_ida ? FALSE : TRUE);
        EnableWindow(hAE, dbg_req->is_ida ? FALSE : TRUE);
    }

    EnableWindow(GetDlgItem(disHwnd, IDC_REG_PC_L), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_PC), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_SP_L), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_SP), dbg_req->is_ida ? FALSE : TRUE);

    EnableWindow(GetDlgItem(disHwnd, IDC_REG_PPC_L), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_PPC), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_SR_L), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_SR), dbg_req->is_ida ? FALSE : TRUE);

    EnableWindow(GetDlgItem(disHwnd, IDC_SR_T), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_0E), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_S), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_M), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_0B), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_I_VAL), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_07), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_06), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_05), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_X), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_N), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_Z), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_V), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_SR_C), dbg_req->is_ida ? FALSE : TRUE);

    EnableWindow(GetDlgItem(disHwnd, IDC_STEP_INTO), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_STEP_OVER), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_RUN_EMU), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_PAUSE_EMU), dbg_req->is_ida ? FALSE : TRUE);

    EnableWindow(GetDlgItem(disHwnd, IDC_REG_DMA_LEN), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_DMA_SRC), dbg_req->is_ida ? FALSE : TRUE);
    EnableWindow(GetDlgItem(disHwnd, IDC_REG_DMA_DST), dbg_req->is_ida ? FALSE : TRUE);
}

static void update_sr_view(unsigned short sr)
{
    char tmp[4] = "000";

    CheckDlgButton(disHwnd, IDC_SR_T, (sr  & (1 << 0x0F)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_0E, (sr  & (1 << 0x0E)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_S, (sr  & (1 << 0x0D)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_M, (sr  & (1 << 0x0C)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_0B, (sr  & (1 << 0x0B)) ? BST_CHECKED : BST_UNCHECKED);

    int bin = (sr  & (7 << 0x08)) >> 0x08;
    for (int i = 0; i < 3; ++i)
    {
        tmp[2 - i] = '0' + (bin & 1);
        bin >>= 1;
    }
    SetDlgItemText(disHwnd, IDC_SR_I_VAL, tmp);

    CheckDlgButton(disHwnd, IDC_SR_07, (sr  & (1 << 0x07)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_06, (sr  & (1 << 0x06)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_05, (sr  & (1 << 0x05)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_X, (sr  & (1 << 0x04)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_N, (sr  & (1 << 0x03)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_Z, (sr  & (1 << 0x02)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_V, (sr  & (1 << 0x01)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(disHwnd, IDC_SR_C, (sr  & (1 << 0x00)) ? BST_CHECKED : BST_UNCHECKED);
}

static unsigned short update_sr_reg()
{
    unsigned short sr = 0;

    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_T) ? 1 : 0) << 0x0F;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_0E) ? 1 : 0) << 0x0E;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_S) ? 1 : 0) << 0x0D;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_M) ? 1 : 0) << 0x0C;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_0B) ? 1 : 0) << 0x0B;

    std::string val = GetDlgItemString(disHwnd, IDC_SR_I_VAL);
    int bin = strtol(val.c_str(), NULL, 2);
    sr |= (bin & 7) << 0x08;

    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_07) ? 1 : 0) << 0x07;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_06) ? 1 : 0) << 0x06;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_05) ? 1 : 0) << 0x05;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_X) ? 1 : 0) << 0x04;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_N) ? 1 : 0) << 0x03;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_Z) ? 1 : 0) << 0x02;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_V) ? 1 : 0) << 0x01;
    sr |= (IsDlgButtonChecked(disHwnd, IDC_SR_C) ? 1 : 0) << 0x00;

    return sr;
}

static void set_m68k_reg(int reg_index, unsigned int value)
{
    dbg_req->regs_data.type = REG_TYPE_M68K;
    dbg_req->regs_data.any_reg.index = reg_index;
    dbg_req->regs_data.any_reg.val = value;
    send_dbg_request(dbg_req, REQ_SET_REG);
}

static void update_regs()
{
    dbg_req->regs_data.type = REG_TYPE_M68K | REG_TYPE_VDP;
    send_dbg_request(dbg_req, REQ_GET_REGS);

    regs_68k_data_t *reg_vals = &dbg_req->regs_data.regs_68k;
    vdp_regs_t *vdp_regs = &dbg_req->regs_data.vdp_regs;

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
    if (currentControlFocus != IDC_REG_SR)
    {
        UpdateDlgItemHex(disHwnd, IDC_REG_SR, 4, reg_vals->sr);
        update_sr_view(reg_vals->sr);
    }
    if (currentControlFocus != IDC_REG_SP)
        UpdateDlgItemHex(disHwnd, IDC_REG_SP, 8, reg_vals->sp);
    if (currentControlFocus != IDC_REG_PPC)
        UpdateDlgItemHex(disHwnd, IDC_REG_PPC, 8, reg_vals->ppc);
    if (currentControlFocus != IDC_REG_DMA_LEN)
        UpdateDlgItemHex(disHwnd, IDC_REG_DMA_LEN, 6, vdp_regs->dma_len);
    if (currentControlFocus != IDC_REG_DMA_SRC)
        UpdateDlgItemHex(disHwnd, IDC_REG_DMA_SRC, 8, vdp_regs->dma_src);
    if (currentControlFocus != IDC_REG_DMA_DST)
        UpdateDlgItemHex(disHwnd, IDC_REG_DMA_DST, 8, vdp_regs->dma_dst);
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
    cfFormat.dwMask = CFM_CHARSET | CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD;
    cfFormat.dwEffects = CFE_BOLD;
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
        int start_pos = (int)SendMessage(listHwnd, EM_LINEINDEX, selection.line_index, 0);
        int end_pos = (int)SendMessage(listHwnd, EM_LINELENGTH, start_pos, 0);

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

static unsigned int lineIndexToPc(int lineIndex)
{
    std::map<unsigned int, int>::const_iterator i = linesMap.cbegin();
    while (i != linesMap.end())
    {
        if (i->second != lineIndex)
        {
            ++i;
            continue;
        }
        return i->first;
    }

    return -1;
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

static void changeExtraSelection(unsigned int address, COLORREF newColor)
{
    std::map<unsigned int, int>::const_iterator i = linesMap.cbegin();
    while (i != linesMap.end())
    {
        if (i->first != address)
        {
            ++i;
            continue;
        }

        for (int j = 0; j < extraSelections.size(); ++j)
        {
            extra_selection_t selection = extraSelections[j];

            if (selection.line_index == i->second)
            {
                selection.color = newColor;
                extraSelections.erase(extraSelections.begin() + j);
                extraSelections.push_back(selection);
                break;
            }
        }

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

        SendMessage(listHwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, max(0, i->second - 15)), 0);
        break;
    }
}

static void update_disasm_with_bpt_list(unsigned int pc)
{
    SCROLLINFO si;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    GetScrollInfo(listHwnd, SB_VERT, &si);

    addExtraSelection(pc, CURR_PC_COLOR);

    for (int i = 0; i < dbg_req->bpt_list.count; ++i)
    {
        bpt_data_t *bpt_data = &dbg_req->bpt_list.breaks[i];

        if (bpt_data->type == BPT_M68K_E)
            addExtraSelection(bpt_data->address, bpt_data->enabled ? BPT_COLOR : BPT_COLOR_DISABLED);

        if (bpt_data->type == BPT_M68K_E && bpt_data->address == pc)
            changeExtraSelection(pc, bpt_data->enabled ? BPT_COLOR_PC : BPT_COLOR_PC_DISABLED);
    }

    SetScrollInfo(listHwnd, SB_VERT, &si, TRUE);
}

static void get_disasm_listing_pc(unsigned int pc, const unsigned char *code, size_t code_size, int max_lines)
{
    linesMap.clear();
    cliptext.clear();

    cap::cs_insn *insn = cap::cs_malloc(cs_handle);

    const uint8_t *code_ptr = (const uint8_t *)code;
    uint64_t start_addr = max(pc - BYTES_BEFORE_PC, (pc < MAXROMSIZE) ? ROM_CODE_START_ADDR : RAM_START_ADDR);
    uint64_t address = start_addr;

    int lines = 0, pc_line = 0;

    bool ep_fixed = false;

    while (lines < max_lines && code_size)
    {
        while (!cs_disasm_iter(cs_handle, &code_ptr, &code_size, &address, insn))
        {
            code_ptr += 2;
            address += 2;
            code_size -= 2;
        }


        if (!ep_fixed && insn->address >= pc)
        {
            pc_line = lines;

            unsigned int pc_data_offset = pc;
            pc_data_offset = (unsigned int)((pc_data_offset < MAXROMSIZE) ? (pc_data_offset - start_addr) : ((pc_data_offset - start_addr) & 0xFFFF));

            code_ptr = (const uint8_t *)(&code[pc_data_offset]);
            address = pc;
            code_size = (code_size >= pc_data_offset) ? (code_size - pc_data_offset) : code_size;
            ep_fixed = true;
            continue;
        }

        linesMap.insert(std::make_pair((unsigned int)insn->address, lines));

        std::string line;

        std::string addrString = make_hex_string(6, (unsigned int)insn->address);
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

    update_disasm_with_bpt_list(pc);
    scrollToAddress(pc);
}

static void update_disasm_listing(unsigned int pc)
{
    unsigned int real_pc = pc;

    pc = max(pc - BYTES_BEFORE_PC, (pc < MAXROMSIZE) ? ROM_CODE_START_ADDR : RAM_START_ADDR);
    dbg_req->mem_data.address = pc;
    dbg_req->mem_data.size = DISASM_LISTING_BYTES;
    send_dbg_request(dbg_req, (pc < MAXROMSIZE) ? REQ_READ_68K_ROM : REQ_READ_68K_RAM);

    get_disasm_listing_pc(
        real_pc,
        (pc < MAXROMSIZE) ? &dbg_req->mem_data.m68k_rom[pc] : &dbg_req->mem_data.m68k_ram[(pc - RAM_START_ADDR) & 0xFFFF],
        dbg_req->mem_data.size,
        LINES_MAX);
}

static void update_bpt_list()
{
    send_dbg_request(dbg_req, REQ_LIST_BREAKS);

    ListView_SetItemCount(GetDlgItem(disHwnd, IDC_BPT_LIST), dbg_req->bpt_list.count);
}

static void update_dbg_window_info(bool update_registers, bool update_bpts, bool update_listing)
{
    LockWindowUpdate(disHwnd);
    if (update_registers)
        update_regs();
    if (update_bpts)
        update_bpt_list();
    if (update_listing)
        update_disasm_listing(last_pc);
    LockWindowUpdate(NULL);
}

static void do_game_started(unsigned int pc)
{
}

static void do_game_paused(unsigned int pc)
{
    paused = true;
    update_dbg_window_info(true, false, true);

    SetForegroundWindow(disHwnd);
}

static void do_game_stopped()
{
}

static void check_debugger_events()
{
    if (!dbg_req->dbg_active)
        return;

    int event_index = recv_dbg_event(dbg_req, 0);
    if (event_index == -1)
        return;

    debugger_event_t *dbg_event = &dbg_req->dbg_events[event_index];

    switch (dbg_event->type)
    {
    case DBG_EVT_STARTED: do_game_started(dbg_event->pc); break;
    case DBG_EVT_PAUSED:
    case DBG_EVT_STEP:
    case DBG_EVT_BREAK:
        do_game_paused(dbg_event->pc); break;
    case DBG_EVT_STOPPED: do_game_stopped(); break;
    default:
        break;
    }

    dbg_event->type = DBG_EVT_NO_EVENT;
}

static void delete_breakpoint_from_list(int index)
{
    if (index != -1)
    {
        bpt_data_t *bpt_item = &dbg_req->bpt_list.breaks[index];

        bpt_data_t *bpt_data = &dbg_req->bpt_data;
        bpt_data->address = bpt_item->address;
        bpt_data->type = bpt_item->type;
        send_dbg_request(dbg_req, REQ_DEL_BREAK);
        update_dbg_window_info(false, true, (bpt_item->type == BPT_M68K_E) ? true : false);
    }
}

LRESULT CALLBACK BptListHandler(HWND hWnd, UINT uMsg, WPARAM wParam,
    LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) & 0x8000) return FALSE;

        switch (wParam)
        {
        case VK_DELETE:
        {
            int index = ListView_GetNextItem(GetDlgItem(disHwnd, IDC_BPT_LIST), -1, LVNI_SELECTED);
            delete_breakpoint_from_list(index);
        } break;
        }
        return TRUE;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK DisasseblerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        listHwnd = GetDlgItem(hWnd, IDC_DISASM_LIST);
        
        SetFocus(listHwnd);
        set_listing_font("Liberation Mono", 9);

        CheckDlgButton(hWnd, IDC_EXEC_BPT, BST_CHECKED);
        CheckDlgButton(hWnd, IDC_BPT_IS_READ, BST_CHECKED);
        CheckDlgButton(hWnd, IDC_BPT_IS_WRITE, BST_CHECKED);

        HWND bp_sizes = GetDlgItem(hWnd, IDC_BPT_SIZE);
        SendMessage(bp_sizes, CB_ADDSTRING, 0, (LPARAM)"1");
        SendMessage(bp_sizes, CB_ADDSTRING, 0, (LPARAM)"2");
        SendMessage(bp_sizes, CB_ADDSTRING, 0, (LPARAM)"4");
        SendMessage(bp_sizes, CB_SETCURSEL, 0, 0);

        UpdateDlgItemHex(hWnd, IDC_BPT_ADDR, 6, ROM_CODE_START_ADDR);

        HWND bpt_list = GetDlgItem(hWnd, IDC_BPT_LIST);
        ListView_SetExtendedListViewStyle(bpt_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

        SetWindowSubclass(bpt_list, BptListHandler, 0, 0);

        LV_COLUMN column;
        memset(&column, 0, sizeof(column));
        
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = (LPSTR)"";
        column.cx = 25;
        ListView_InsertColumn(bpt_list, 0, &column);
        
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = (LPSTR)"Address";
        column.cx = 0x42;
        column.iSubItem++;
        ListView_InsertColumn(bpt_list, 1, &column);
        
        column.cx = 0x30;
        column.pszText = (LPSTR)"Size";
        column.iSubItem++;
        ListView_InsertColumn(bpt_list, 2, &column);

        column.cx = 0x52;
        column.pszText = (LPSTR)"Type";
        column.iSubItem++;
        ListView_InsertColumn(bpt_list, 3, &column);

        HWND header = ListView_GetHeader(bpt_list);

        HDITEM hdi = { 0 };
        hdi.mask = HDI_FORMAT;
        Header_GetItem(header, 0, &hdi);
        hdi.fmt |= HDF_FIXEDWIDTH;
        Header_SetItem(header, 0, &hdi);

        SendMessage(GetDlgItem(hWnd, IDC_SR_I_SPIN), UDM_SETRANGE, 0, MAKELPARAM(100,0));

        SetTimer(hWnd, DBG_EVENTS_TIMER, 10, NULL);
        SetTimer(hWnd, DBG_WHEN_IDA_UPDATE, 500, NULL);

        send_dbg_request(dbg_req, REQ_ATTACH);
    } break;
    case WM_TIMER:
    {
        switch (LOWORD(wParam))
        {
        case DBG_EVENTS_TIMER:
            disable_when_ida();
            if (!dbg_req->is_ida)
                check_debugger_events();
            break;
        case DBG_WHEN_IDA_UPDATE:
            if (dbg_req && dbg_req->is_ida && (dbg_req->dbg_events_count > 0 || dbg_req->dbg_paused))
                update_dbg_window_info(true, true, true);
            break;
        }
        return FALSE;
    } break;
    case WM_NOTIFY:
    {
        switch (((LPNMHDR)lParam)->code)
        {
        case NM_CLICK:
        {
            HWND bpt_list = GetDlgItem(hWnd, IDC_BPT_LIST);
            LPNMLISTVIEW nmlist = (LPNMLISTVIEW)lParam;

            LVHITTESTINFO hitinfo;
            hitinfo.pt = nmlist->ptAction;

            int item = ListView_HitTest(bpt_list, &hitinfo);

            if (item != -1)
            {
                if ((hitinfo.flags == LVHT_ONITEMSTATEICON))
                {
                    bpt_data_t *bpt_data = &dbg_req->bpt_list.breaks[nmlist->iItem];
                    ListView_SetCheckState(bpt_list, nmlist->iItem, !bpt_data->enabled);
                    ListView_RedrawItems(bpt_list, nmlist->iItem, nmlist->iItem);

                    bpt_data_t *bpt_data_new = &dbg_req->bpt_data;
                    bpt_data_new->address = bpt_data->address;
                    bpt_data_new->type = bpt_data->type;
                    bpt_data_new->width = bpt_data->width;

                    send_dbg_request(dbg_req, REQ_TOGGLE_BREAK);
                    update_dbg_window_info(false, true, true);
                }
            }
            return 0;
        } break;
        case LVN_GETDISPINFO:
        {
            NMLVDISPINFO *plvdi = (NMLVDISPINFO *)lParam;
            NMLISTVIEW *nmList = (NMLISTVIEW *)lParam;
            HWND bpt_list = GetDlgItem(hWnd, IDC_BPT_LIST);

            char tmp[32];

            bpt_data_t *bpt_data = &dbg_req->bpt_list.breaks[plvdi->item.iItem];
            switch (plvdi->item.iSubItem)
            {
            case 0: // Enabled

                if (plvdi->item.mask & LVIF_IMAGE)
                {
                    plvdi->item.mask |= LVIF_STATE;
                    plvdi->item.stateMask = LVIS_STATEIMAGEMASK;
                    plvdi->item.state = INDEXTOSTATEIMAGEMASK(bpt_data->enabled ? 2 : 1);
                    return 0;
                }
                break;
            case 1: // Address
            {
                snprintf(tmp, sizeof(tmp), "%.6X", bpt_data->address & 0xFFFFFF);
                plvdi->item.pszText = tmp;
            } break;
            case 2: // Size
            {
                snprintf(tmp, sizeof(tmp), "%d", bpt_data->width);
                plvdi->item.pszText = tmp;
            } break;
            case 3: // Type
            {
                switch (bpt_data->type)
                {
                case BPT_M68K_E: snprintf(tmp, sizeof(tmp), "%s", "M68K_E"); break;
                case BPT_Z80_E: snprintf(tmp, sizeof(tmp), "%s", "Z80_E"); break;
                case BPT_M68K_R: snprintf(tmp, sizeof(tmp), "%s", "M68K_R"); break;
                case BPT_M68K_W: snprintf(tmp, sizeof(tmp), "%s", "M68K_W"); break;
                case BPT_M68K_RW: snprintf(tmp, sizeof(tmp), "%s", "M68K_RW"); break;

                // VDP
                case BPT_VRAM_R: snprintf(tmp, sizeof(tmp), "%s", "VRAM_R"); break;
                case BPT_VRAM_W: snprintf(tmp, sizeof(tmp), "%s", "VRAM_W"); break;
                case BPT_VRAM_RW: snprintf(tmp, sizeof(tmp), "%s", "VRAM_RW"); break;

                case BPT_CRAM_R: snprintf(tmp, sizeof(tmp), "%s", "CRAM_R"); break;
                case BPT_CRAM_W: snprintf(tmp, sizeof(tmp), "%s", "CRAM_W"); break;
                case BPT_CRAM_RW: snprintf(tmp, sizeof(tmp), "%s", "CRAM_RW"); break;

                case BPT_VSRAM_R: snprintf(tmp, sizeof(tmp), "%s", "VSRAM_R"); break;
                case BPT_VSRAM_W: snprintf(tmp, sizeof(tmp), "%s", "VSRAM_W"); break;
                case BPT_VSRAM_RW: snprintf(tmp, sizeof(tmp), "%s", "VSRAM_RW"); break;

                // Z80
                case BPT_Z80_R: snprintf(tmp, sizeof(tmp), "%s", "Z80_R"); break;
                case BPT_Z80_W: snprintf(tmp, sizeof(tmp), "%s", "Z80_W"); break;
                case BPT_Z80_RW: snprintf(tmp, sizeof(tmp), "%s", "Z80_RW"); break;

                // REGS
                case BPT_VDP_REG:
                case BPT_M68K_REG:
                    break;
                }
                plvdi->item.pszText = tmp;
            } break;
            default:
                break;
            }
            return TRUE;
        } break;
        case UDN_DELTAPOS:
        {
            char tmp[4];
            LPNMUPDOWN lpnmud = (LPNMUPDOWN)lParam;

            std::string val = GetDlgItemString(disHwnd, IDC_SR_I_VAL);
            int bin = strtol(val.c_str(), NULL, 2);

            bin = (bin + lpnmud->iDelta) & 7;

            for (int i = 0; i < 3; ++i)
            {
                tmp[2 - i] = '0' + (bin & 1);
                bin >>= 1;
            }
            tmp[3] = '\0';

            SetDlgItemText(disHwnd, IDC_SR_I_VAL, tmp);

            unsigned short sr = update_sr_reg();
            UpdateDlgItemHex(disHwnd, IDC_REG_SR, 4, sr);

            set_m68k_reg(17, sr); // m68k.h -> M68K_REG_SR = 17
        } break;
        }
    } break;
    case WM_COMMAND:
    {
        if ((HIWORD(wParam) == EN_CHANGE))
        {
            return FALSE;
        }
        else if ((HIWORD(wParam) == EN_SETFOCUS))
        {
            previousText = GetDlgItemString(hWnd, LOWORD(wParam));
            currentControlFocus = LOWORD(wParam);
        }
        else if ((HIWORD(wParam) == EN_KILLFOCUS))
        {
            std::string newText = GetDlgItemString(hWnd, LOWORD(wParam));
            if (newText != previousText)
            {
                if (!paused)
                    break;

                int value = GetDlgItemHex(hWnd, LOWORD(wParam));
                switch (LOWORD(wParam))
                {
                case IDC_REG_D0:
                    set_m68k_reg(REG_68K_D0, value);
                    break;
                case IDC_REG_D1:
                    set_m68k_reg(REG_68K_D1, value);
                    break;
                case IDC_REG_D2:
                    set_m68k_reg(REG_68K_D2, value);
                    break;
                case IDC_REG_D3:
                    set_m68k_reg(REG_68K_D3, value);
                    break;
                case IDC_REG_D4:
                    set_m68k_reg(REG_68K_D4, value);
                    break;
                case IDC_REG_D5:
                    set_m68k_reg(REG_68K_D5, value);
                    break;
                case IDC_REG_D6:
                    set_m68k_reg(REG_68K_D6, value);
                    break;
                case IDC_REG_D7:
                    set_m68k_reg(REG_68K_D7, value);
                    break;

                case IDC_REG_A0:
                    set_m68k_reg(REG_68K_A0, value);
                    break;
                case IDC_REG_A1:
                    set_m68k_reg(REG_68K_A1, value);
                    break;
                case IDC_REG_A2:
                    set_m68k_reg(REG_68K_A2, value);
                    break;
                case IDC_REG_A3:
                    set_m68k_reg(REG_68K_A3, value);
                    break;
                case IDC_REG_A4:
                    set_m68k_reg(REG_68K_A4, value);
                    break;
                case IDC_REG_A5:
                    set_m68k_reg(REG_68K_A5, value);
                    break;
                case IDC_REG_A6:
                    set_m68k_reg(REG_68K_A6, value);
                    break;
                case IDC_REG_A7:
                    set_m68k_reg(REG_68K_A7, value);
                    break;

                case IDC_REG_PC:
                    set_m68k_reg(REG_68K_PC, value);
                    break;
                case IDC_REG_SR:
                    set_m68k_reg(REG_68K_SR, value);
                    break;
                case IDC_REG_SP:
                    set_m68k_reg(REG_68K_SP, value);
                    break;
                }
            }
            return TRUE;
        }
        switch (LOWORD(wParam))
        {
        case IDC_SR_T:
        case IDC_SR_0E:
        case IDC_SR_S:
        case IDC_SR_M:
        case IDC_SR_0B:
        case IDC_SR_07:
        case IDC_SR_06:
        case IDC_SR_05:
        case IDC_SR_X:
        case IDC_SR_N:
        case IDC_SR_Z:
        case IDC_SR_V:
        case IDC_SR_C:
        {
            if (!paused)
                break;

            unsigned short sr = update_sr_reg();
            UpdateDlgItemHex(disHwnd, IDC_REG_SR, 4, sr);

            set_m68k_reg(17, sr);
        } break;
        case IDC_STEP_INTO:
        case IDC_STEP_INTO_HK:
            if (dbg_req->is_ida)
                break;

            dbg_req->dbg_active = 1;
            send_dbg_request(dbg_req, REQ_STEP_INTO);
            break;
        case IDC_STEP_OVER:
        case IDC_STEP_OVER_HK:
            if (dbg_req->is_ida)
                break;

            dbg_req->dbg_active = 1;
            send_dbg_request(dbg_req, REQ_STEP_OVER);
            break;
        case IDC_RUN_EMU:
        case IDC_RUN_EMU_HK:
            if (dbg_req->is_ida)
                break;

            dbg_req->dbg_active = 1;
            send_dbg_request(dbg_req, REQ_RESUME);
            paused = false;
            changeExtraSelection(last_pc, DISASM_LISTING_BKGN);
            break;
        case IDC_PAUSE_EMU:
        case IDC_PAUSE_EMU_HK:
            if (dbg_req->is_ida)
                break;

            dbg_req->dbg_active = 1;
            send_dbg_request(dbg_req, REQ_PAUSE);
            paused = true;
            break;
        case IDC_ADD_BREAK_POS_HK:
        {
            int lineIndex = (int)SendMessage(listHwnd, EM_LINEFROMCHAR, -1, 0);
            unsigned int address = lineIndexToPc(lineIndex);

            bool was_deleted = false;
            for (int i = 0; i < dbg_req->bpt_list.count; ++i)
            {
                if (address == dbg_req->bpt_list.breaks[i].address)
                {
                    bpt_data_t *bpt_data = &dbg_req->bpt_data;
                    bpt_data->address = address;
                    bpt_data->type = BPT_M68K_E;
                    bpt_data->width = 1;
                    send_dbg_request(dbg_req, REQ_DEL_BREAK);
                    was_deleted = true;
                    break;
                }
            }

            if (!was_deleted)
            {
                bpt_data_t *bpt_data = &dbg_req->bpt_data;
                bpt_data->address = address;
                bpt_data->type = BPT_M68K_E;
                bpt_data->width = 1;
                bpt_data->enabled = 1;
                send_dbg_request(dbg_req, REQ_ADD_BREAK);
            }

            update_dbg_window_info(false, true, true);
        } break;
        case IDC_ADD_BREAK:
        {
            bpt_data_t *bpt_data = &dbg_req->bpt_data;
            bpt_data->address = GetDlgItemHex(disHwnd, IDC_BPT_ADDR);
            int bpt_type = (int)SendMessage(GetDlgItem(disHwnd, IDC_BPT_SIZE), CB_GETCURSEL, 0, 0);

            switch (bpt_type)
            {
            case 1: bpt_data->width = 2; break;
            case 2: bpt_data->width = 4; break;
            default: bpt_data->width = 1; break;
            }

            if (IsDlgButtonChecked(disHwnd, IDC_EXEC_BPT))
                bpt_data->type = BPT_M68K_E;
            else if (IsDlgButtonChecked(disHwnd, IDC_68K_RAM_BPT))
            {
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_READ))
                    bpt_data->type = BPT_M68K_R;
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_WRITE))
                    bpt_data->type = (bpt_type_t)((int)bpt_data->type | (int)BPT_M68K_W);
            }
            else if (IsDlgButtonChecked(disHwnd, IDC_VRAM_BPT))
            {
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_READ))
                    bpt_data->type = BPT_VRAM_R;
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_WRITE))
                    bpt_data->type = (bpt_type_t)((int)bpt_data->type | (int)BPT_VRAM_W);
            }
            else if (IsDlgButtonChecked(disHwnd, IDC_CRAM_BPT))
            {
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_READ))
                    bpt_data->type = BPT_CRAM_R;
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_WRITE))
                    bpt_data->type = (bpt_type_t)((int)bpt_data->type | (int)BPT_CRAM_W);
            }
            else if (IsDlgButtonChecked(disHwnd, IDC_VSRAM_BPT))
            {
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_READ))
                    bpt_data->type = BPT_VSRAM_R;
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_WRITE))
                    bpt_data->type = (bpt_type_t)((int)bpt_data->type | (int)BPT_VSRAM_W);
            }
            else if (IsDlgButtonChecked(disHwnd, IDC_Z80_RAM_BPT))
            {
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_READ))
                    bpt_data->type = BPT_Z80_R;
                if (IsDlgButtonChecked(disHwnd, IDC_BPT_IS_WRITE))
                    bpt_data->type = (bpt_type_t)((int)bpt_data->type | (int)BPT_Z80_W);
            }

            bpt_data->enabled = 1;
            send_dbg_request(dbg_req, REQ_ADD_BREAK);

            update_dbg_window_info(false, true, (bpt_data->type == BPT_M68K_E) ? true : false);
        } break;
        case IDC_DEL_BREAK:
        {
            int index = ListView_GetNextItem(GetDlgItem(disHwnd, IDC_BPT_LIST), -1, LVNI_SELECTED);
            delete_breakpoint_from_list(index);
        } break;
        case IDC_CLEAR_BREAKS:
            send_dbg_request(dbg_req, REQ_CLEAR_BREAKS);
            update_dbg_window_info(false, true, true);
        } break;

        return TRUE;
    } break;
    case WM_CLOSE:
    {
        KillTimer(disHwnd, DBG_EVENTS_TIMER);
        KillTimer(disHwnd, DBG_WHEN_IDA_UPDATE);
        send_dbg_request(dbg_req, REQ_STOP);
        disHwnd = NULL;

        PostQuitMessage(0);
        EndDialog(hWnd, 0);
    } break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

static bool openCapstone()
{
    return (cap::cs_open(cap::CS_ARCH_M68K, cap::CS_MODE_M68K_000, &cs_handle) == cap::CS_ERR_OK);
}

static void closeCapstone()
{
    cap::cs_close(&cs_handle);
}

static DWORD WINAPI ThreadProc(LPVOID lpParam)
{
    openCapstone();

    HACCEL hAccelTable = LoadAccelerators(dbg_wnd_hinst, MAKEINTRESOURCE(ACCELERATOR_RESOURCE_ID));
    MSG msg;
    hRich = LoadLibrary("Riched32.dll");

    disHwnd = CreateDialog(dbg_wnd_hinst, MAKEINTRESOURCE(IDD_DISASSEMBLER), dbg_window, (DLGPROC)DisasseblerWndProc);
    ShowWindow(disHwnd, SW_SHOW);
    UpdateWindow(disHwnd);

    init_highlighter();

    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(disHwnd, hAccelTable, &msg) && !IsDialogMessage(disHwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    FreeLibrary(hRich);
    closeCapstone();

    return 1;
}

void create_disassembler()
{
    dbg_req = open_shared_mem();
    hThread = CreateThread(0, NULL, ThreadProc, NULL, NULL, NULL);
}

void destroy_disassembler()
{
    SendMessage(disHwnd, WM_CLOSE, 0, 0);

    CloseHandle(hThread);

    close_shared_mem(&dbg_req);
}
