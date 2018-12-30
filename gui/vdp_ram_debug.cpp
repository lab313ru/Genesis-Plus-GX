// A few notes about this implementation of a RAM dump window:
//
// Speed of update was one of the highest priories.
// This is because I wanted the RAM search window to be able to
// update every single value in RAM every single frame,
// without causing the emulation to run noticeably slower than normal.
//

#include <Windows.h>
#include <Commctrl.h>
#include <Windowsx.h>
#include <string>
#include <vector>

#include "resource.h"

#include "edit_fields.h"

#include "shared.h"
#include "vdp_ctrl.h"
#include "genesis.h"

#include "gui.h"
#include "vdp_ram_debug.h"

static HWND VDPRamHWnd = NULL;
static HANDLE hThread = NULL;

static int VDPRamPal, VDPRamTile;
static bool IsVRAM;

#define VDP_PAL_COUNT 4
#define VDP_PAL_COLORS 16
#define VDP_TILES_IN_ROW 16
#define VDP_TILES_IN_COL 24
#define VDP_TILE_W 8
#define VDP_TILE_H 8
#define VDP_TILE_ZOOM 2
#define VDP_BLOCK_W (VDP_TILE_W * VDP_TILE_ZOOM)
#define VDP_BLOCK_H (VDP_TILE_H * VDP_TILE_ZOOM)
#define VDP_SCROLL_MAX (sizeof(vram) / (VDP_TILES_IN_ROW * 0x20) - VDP_TILES_IN_COL)

struct TabInfo
{
    TabInfo(const std::string& atabName, int adialogID, DLGPROC adialogProc)
        :tabName(atabName), dialogID(adialogID), dialogProc(adialogProc), hwndDialog(NULL)
    {}

    std::string tabName;
    int dialogID;
    DLGPROC dialogProc;
    HWND hwndDialog;
};

std::string previousText;
unsigned int currentControlFocus;
HWND activeTabWindow;
std::vector<TabInfo> tabItems;

void WndProcDialogImplementSaveFieldWhenLostFocus(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        //Make sure no textbox is selected on startup, and remove focus from textboxes when
        //the user clicks an unused area of the window.
    case WM_LBUTTONDOWN:
    case WM_SHOWWINDOW:
        SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(0, EN_SETFOCUS), 0);
        SetFocus(NULL);
        break;
    }
}

inline unsigned int mask(unsigned char bit_idx, unsigned char bits_cnt = 1)
{
    return (((1 << bits_cnt) - 1) << bit_idx);
}

//----------------------------------------------------------------------------------------
void msgModeRegistersUPDATE(HWND hwnd)
{
    //Mode registers
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_VSI, (reg[0] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_HSI, (reg[0] & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LCB, (reg[0] & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_IE1, (reg[0] & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_SS, (reg[0] & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_PS, (reg[0] & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M2, (reg[0] & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_ES, (reg[0] & mask(0)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_EVRAM, (reg[1] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_DISP, (reg[1] & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_IE0, (reg[1] & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M1, (reg[1] & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M3, (reg[1] & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M5, (reg[1] & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_SZ, (reg[1] & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_MAG, (reg[1] & mask(0)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B7, (reg[11] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B6, (reg[11] & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B5, (reg[11] & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B4, (reg[11] & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_IE2, (reg[11] & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_VSCR, (reg[11] & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_HSCR, (reg[11] & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LSCR, (reg[11] & mask(0)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_RS0, (reg[12] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_U1, (reg[12] & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_U2, (reg[12] & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_U3, (reg[12] & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_STE, (reg[12] & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LSM1, (reg[12] & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LSM0, (reg[12] & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_RS1, (reg[12] & mask(0)) ? BST_CHECKED : BST_UNCHECKED);
}

#define GET_BITS(number, n, c) ((number & mask(n, c)) >> n)
//----------------------------------------------------------------------------------------
void msgOtherRegistersUPDATE(HWND hwnd)
{
    unsigned int value = 0;
    bool mode4Enabled = !(reg[1] & mask(2));
    int extendedVRAMModeEnabled = (reg[1] & mask(7));
    int h40ModeActive = (reg[12] & mask(0));

    //Other registers
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_077, (reg[7] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_076, (reg[7] & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
    if (currentControlFocus != IDC_VDP_REGISTERS_BACKGROUNDPALETTEROW)
    {
        value = GET_BITS(reg[7], 4, 2);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_BACKGROUNDPALETTEROW, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_BACKGROUNDPALETTECOLUMN)
    {
        value = GET_BITS(reg[7], 0, 4);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_BACKGROUNDPALETTECOLUMN, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_BACKGROUNDSCROLLX)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_BACKGROUNDSCROLLX, 2, reg[8]);
    if (currentControlFocus != IDC_VDP_REGISTERS_BACKGROUNDSCROLLY)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_BACKGROUNDSCROLLY, 2, reg[9]);
    if (currentControlFocus != IDC_VDP_REGISTERS_HINTLINECOUNTER)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_HINTLINECOUNTER, 2, reg[10]);
    if (currentControlFocus != IDC_VDP_REGISTERS_AUTOINCREMENT)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_AUTOINCREMENT, 2, reg[15]);
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLABASE)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLABASE, 2, reg[2]);
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLABASE_E)
    {
        value = GET_BITS(reg[2], 3, (extendedVRAMModeEnabled) ? 4 : 3) << 13;
        if (mode4Enabled)
        {
            value = GET_BITS(reg[2], 1, 3) << 11;
        }

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLABASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_WINDOWBASE)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_WINDOWBASE, 2, reg[3]);
    if (currentControlFocus != IDC_VDP_REGISTERS_WINDOWBASE_E)
    {
        value = GET_BITS(reg[3], 1, (extendedVRAMModeEnabled) ? 6 : 5) << 11;
        if (h40ModeActive)
        {
            value = GET_BITS(reg[3], 2, (extendedVRAMModeEnabled) ? 5 : 4) << 12;
        }
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_WINDOWBASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLBBASE)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLBBASE, 2, reg[4]);
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLBBASE_E)
    {
        value = GET_BITS(reg[4], 0, (extendedVRAMModeEnabled) ? 4 : 3) << 13;
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLBBASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_SPRITEBASE)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SPRITEBASE, 2, reg[5]);
    if (currentControlFocus != IDC_VDP_REGISTERS_SPRITEBASE_E)
    {
        value = GET_BITS(reg[5], 0, (extendedVRAMModeEnabled) ? 8 : 7) << 9;

        if (mode4Enabled)
        {
            value = GET_BITS(reg[5], 1, 6) << 8;
        }
        else if (h40ModeActive)
        {
            value = GET_BITS(reg[5], 1, (extendedVRAMModeEnabled) ? 7 : 6) << 10;
        }

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SPRITEBASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_SPRITEPATTERNBASE)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SPRITEPATTERNBASE, 2, reg[6]);
    if (currentControlFocus != IDC_VDP_REGISTERS_SPRITEPATTERNBASE_E)
    {
        if (mode4Enabled)
        {
            value = GET_BITS(reg[6], 2, 1) << 13;
        }
        else if (extendedVRAMModeEnabled)
        {
            value = GET_BITS(reg[6], 5, 1) << 16;
        }

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SPRITEPATTERNBASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_HSCROLLBASE)
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_HSCROLLBASE, 2, reg[13]);
    if (currentControlFocus != IDC_VDP_REGISTERS_HSCROLLBASE_E)
    {
        value = GET_BITS(reg[13], 0, (extendedVRAMModeEnabled) ? 7 : 6) << 10;
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_HSCROLLBASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_DMALENGTH)
    {
        value = reg[19];
        value += reg[20] << 8;

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_DMALENGTH, 4, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_DMASOURCE)
    {
        value = reg[21] << 1;
        value += reg[22] << 9;
        value += GET_BITS(reg[23], 0, 7) << 17;

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_DMASOURCE, 6, value >> 1);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_DMASOURCE_E)
    {
        value = reg[21] << 1;
        value += reg[22] << 9;
        value += GET_BITS(reg[23], 0, 7) << 17;

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_DMASOURCE_E, 6, value);
    }
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_DMD1, (reg[23] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_DMD0, (reg[23] & mask(6)) ? BST_CHECKED : BST_UNCHECKED);

    if (currentControlFocus != IDC_VDP_REGISTERS_0E57)
    {
        value = GET_BITS(reg[14], 5, 3);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_0E57, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLAPATTERNBASE)
    {
        value = reg[14] & 0x0F;
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLAPATTERNBASE, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLAPATTERNBASE_E)
    {
        if (extendedVRAMModeEnabled)
        {
            value = GET_BITS(reg[14], 0, 1) << 16;
        }

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLAPATTERNBASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_0E13)
    {
        value = GET_BITS(reg[14], 1, 3);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_0E13, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLBPATTERNBASE)
    {
        value = (reg[14] >> 4) & 0x0F;
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLBPATTERNBASE, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_SCROLLBPATTERNBASE_E)
    {
        if (extendedVRAMModeEnabled)
        {
            value = (GET_BITS(reg[14], 0, 1) << 16) & (GET_BITS(reg[14], 4, 1) << 16);
        }

        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_SCROLLBPATTERNBASE_E, 5, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_1067)
    {
        value = GET_BITS(reg[16], 6, 2);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_1067, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_VSZ)
    {
        value = GET_BITS(reg[16], 4, 2);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_VSZ, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_1023)
    {
        value = GET_BITS(reg[16], 2, 2);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_1023, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_HSZ)
    {
        value = GET_BITS(reg[16], 0, 2);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_HSZ, 1, value);
    }
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_WINDOWRIGHT, (reg[17] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    if (currentControlFocus != IDC_VDP_REGISTERS_1156)
    {
        value = GET_BITS(reg[17], 5, 2);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_1156, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_WINDOWBASEX)
    {
        value = GET_BITS(reg[17], 0, 5);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_WINDOWBASEX, 1, value);
    }
    CheckDlgButton(hwnd, IDC_VDP_REGISTERS_WINDOWDOWN, (reg[18] & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
    if (currentControlFocus != IDC_VDP_REGISTERS_1256)
    {
        value = GET_BITS(reg[18], 5, 2);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_1256, 1, value);
    }
    if (currentControlFocus != IDC_VDP_REGISTERS_WINDOWBASEY)
    {
        value = GET_BITS(reg[18], 0, 5);
        UpdateDlgItemHex(hwnd, IDC_VDP_REGISTERS_WINDOWBASEY, 1, value);
    }

    unsigned int screenSizeCellsH = 0x20 + (reg[16] & 0x3) * 32;
    unsigned int screenSizeCellsV = 0x20 + ((reg[16] >> 4) & 0x3) * 32;

    UpdateDlgItemBin(hwnd, IDC_VDP_REGISTERS_HSZ_E, screenSizeCellsH);
    UpdateDlgItemBin(hwnd, IDC_VDP_REGISTERS_VSZ_E, screenSizeCellsV);
}
#undef GET_BITS

#define SET_BIT(number, n, x) (number = (number & ~mask(n)) | (x << n))
#define SET_BITS(number, n, c, x) (number = (number & ~mask(n, c)) | (x << n))
//----------------------------------------------------------------------------------------
INT_PTR msgModeRegistersWM_COMMAND(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
    if (HIWORD(wparam) == BN_CLICKED)
    {
        unsigned int controlID = LOWORD(wparam);
        int chk = (IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED) ? 1 : 0;
        switch (controlID)
        {
        case IDC_VDP_REGISTERS_VSI:
            SET_BIT(reg[0], 7, chk);
            break;
        case IDC_VDP_REGISTERS_HSI:
            SET_BIT(reg[0], 6, chk);
            break;
        case IDC_VDP_REGISTERS_LCB:
            SET_BIT(reg[0], 5, chk);
            break;
        case IDC_VDP_REGISTERS_IE1:
            SET_BIT(reg[0], 4, chk);
            break;
        case IDC_VDP_REGISTERS_SS:
            SET_BIT(reg[0], 3, chk);
            break;
        case IDC_VDP_REGISTERS_PS:
            SET_BIT(reg[0], 2, chk);
            break;
        case IDC_VDP_REGISTERS_M2:
            SET_BIT(reg[0], 1, chk);
            break;
        case IDC_VDP_REGISTERS_ES:
            SET_BIT(reg[0], 0, chk);
            break;
        case IDC_VDP_REGISTERS_EVRAM:
            SET_BIT(reg[1], 7, chk);
            break;
        case IDC_VDP_REGISTERS_DISP:
            SET_BIT(reg[1], 6, chk);
            break;
        case IDC_VDP_REGISTERS_IE0:
            SET_BIT(reg[1], 5, chk);
            break;
        case IDC_VDP_REGISTERS_M1:
            SET_BIT(reg[1], 4, chk);
            break;
        case IDC_VDP_REGISTERS_M3:
            SET_BIT(reg[1], 3, chk);
            break;
        case IDC_VDP_REGISTERS_M5:
            SET_BIT(reg[1], 2, chk);
            break;
        case IDC_VDP_REGISTERS_SZ:
            SET_BIT(reg[1], 1, chk);
            break;
        case IDC_VDP_REGISTERS_MAG:
            SET_BIT(reg[1], 0, chk);
            break;
        case IDC_VDP_REGISTERS_0B7:
            SET_BIT(reg[11], 7, chk);
            break;
        case IDC_VDP_REGISTERS_0B6:
            SET_BIT(reg[11], 6, chk);
            break;
        case IDC_VDP_REGISTERS_0B5:
            SET_BIT(reg[11], 5, chk);
            break;
        case IDC_VDP_REGISTERS_0B4:
            SET_BIT(reg[11], 4, chk);
            break;
        case IDC_VDP_REGISTERS_IE2:
            SET_BIT(reg[11], 3, chk);
            break;
        case IDC_VDP_REGISTERS_VSCR:
            SET_BIT(reg[11], 2, chk);
            break;
        case IDC_VDP_REGISTERS_HSCR:
            SET_BIT(reg[11], 1, chk);
            break;
        case IDC_VDP_REGISTERS_LSCR:
            SET_BIT(reg[11], 0, chk);
            break;
        case IDC_VDP_REGISTERS_RS0:
            SET_BIT(reg[12], 7, chk);
            break;
        case IDC_VDP_REGISTERS_U1:
            SET_BIT(reg[12], 6, chk);
            break;
        case IDC_VDP_REGISTERS_U2:
            SET_BIT(reg[12], 5, chk);
            break;
        case IDC_VDP_REGISTERS_U3:
            SET_BIT(reg[12], 4, chk);
            break;
        case IDC_VDP_REGISTERS_STE:
            SET_BIT(reg[12], 3, chk);
            break;
        case IDC_VDP_REGISTERS_LSM1:
            SET_BIT(reg[12], 2, chk);
            break;
        case IDC_VDP_REGISTERS_LSM0:
            SET_BIT(reg[12], 1, chk);
            break;
        case IDC_VDP_REGISTERS_RS1:
            SET_BIT(reg[12], 0, chk);
            break;
        }
    }

    return TRUE;
}

//----------------------------------------------------------------------------------------
INT_PTR msgOtherRegistersWM_COMMAND(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
    if (HIWORD(wparam) == BN_CLICKED)
    {
        unsigned int controlID = LOWORD(wparam);
        int chk = (IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED) ? 1 : 0;
        switch (controlID)
        {
        case IDC_VDP_REGISTERS_077:
            SET_BIT(reg[7], 7, chk);
            break;
        case IDC_VDP_REGISTERS_076:
            SET_BIT(reg[7], 6, chk);
            break;
        case IDC_VDP_REGISTERS_DMD1:
            SET_BIT(reg[23], 7, chk);
            break;
        case IDC_VDP_REGISTERS_DMD0:
            SET_BIT(reg[23], 6, chk);
            break;
        case IDC_VDP_REGISTERS_WINDOWRIGHT:
            SET_BIT(reg[17], 7, chk);
            break;
        case IDC_VDP_REGISTERS_WINDOWDOWN:
            SET_BIT(reg[18], 7, chk);
            break;
        }
    }
    else if ((HIWORD(wparam) == EN_CHANGE))
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
            case IDC_VDP_REGISTERS_BACKGROUNDPALETTEROW:
                SET_BITS(reg[7], 4, 2, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_BACKGROUNDPALETTECOLUMN:
                SET_BITS(reg[7], 0, 4, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_BACKGROUNDSCROLLX:
                reg[8] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_BACKGROUNDSCROLLY:
                reg[9] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_HINTLINECOUNTER:
                reg[10] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_AUTOINCREMENT:
                reg[15] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_SCROLLABASE:
                reg[2] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_SCROLLABASE_E:
                reg[2] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 10) & 0xFF;
                break;
            case IDC_VDP_REGISTERS_WINDOWBASE:
                reg[3] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_WINDOWBASE_E:
                reg[3] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 10) & 0xFF;
                break;
            case IDC_VDP_REGISTERS_SCROLLBBASE:
                reg[4] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_SCROLLBBASE_E:
                reg[4] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 13) & 0xFF;
                break;
            case IDC_VDP_REGISTERS_SPRITEBASE:
                reg[5] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_SPRITEBASE_E:
                if (!(reg[1] & mask(2)))
                {
                    reg[5] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 7) & 0xFF;
                }
                else
                {
                    reg[5] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 9) & 0xFF;
                }
                break;
            case IDC_VDP_REGISTERS_SPRITEPATTERNBASE:
                reg[6] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_SPRITEPATTERNBASE_E:
                if (!(reg[1] & mask(2)))
                {
                    reg[6] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 13) & 0xFF;
                }
                else
                {
                    reg[6] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 16) & 0xFF;
                }
                break;
            case IDC_VDP_REGISTERS_HSCROLLBASE:
                reg[13] = GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_HSCROLLBASE_E:
                reg[13] = (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 10) & 0xFF;
                break;
            case IDC_VDP_REGISTERS_DMALENGTH:
            {
                unsigned short w = GetDlgItemHex(hwnd, LOWORD(wparam));
                reg[19] = (w & 0xFF);
                reg[20] = (w >> 8) & 0xFF;
            } break;
            case IDC_VDP_REGISTERS_DMASOURCE:
            {
                unsigned int l = GetDlgItemHex(hwnd, LOWORD(wparam)) << 1;
                reg[21] = (l >> 1) & 0xFF;
                reg[22] = (l >> 9) & 0xFF;
                reg[23] = (l >> 17) & mask(0, 7);
            } break;
            case IDC_VDP_REGISTERS_DMASOURCE_E:
            {
                unsigned int l = GetDlgItemHex(hwnd, LOWORD(wparam));
                reg[21] = (l >> 1) & 0xFF;
                reg[22] = (l >> 9) & 0xFF;
                reg[23] = (l >> 17) & mask(0, 7);
            } break;
            case IDC_VDP_REGISTERS_0E57:
                SET_BITS(reg[14], 5, 3, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_SCROLLAPATTERNBASE:
                reg[14] = (reg[14] & 0xF0) | GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_SCROLLAPATTERNBASE_E:
                SET_BITS(reg[14], 4, 1, (GetDlgItemHex(hwnd, LOWORD(wparam)) >> 16));
                break;
            case IDC_VDP_REGISTERS_0E13:
                SET_BITS(reg[14], 1, 3, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_SCROLLBPATTERNBASE:
                reg[14] = (reg[14] & 0x0F) | GetDlgItemHex(hwnd, LOWORD(wparam));
                break;
            case IDC_VDP_REGISTERS_SCROLLBPATTERNBASE_E:
            {
                unsigned int newData = GetDlgItemHex(hwnd, LOWORD(wparam)) >> 16;
                SET_BITS(reg[14], 0, 1, newData);
                if (newData != 0)
                {
                    SET_BITS(reg[14], 4, 1, newData);
                }
            } break;
            case IDC_VDP_REGISTERS_1067:
                SET_BITS(reg[16], 6, 2, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_VSZ:
                SET_BITS(reg[16], 4, 2, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_1023:
                SET_BITS(reg[16], 2, 2, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_HSZ:
                SET_BITS(reg[16], 0, 2, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_1156:
                SET_BITS(reg[17], 5, 2, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_WINDOWBASEX:
                SET_BITS(reg[17], 0, 5, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_1256:
                SET_BITS(reg[18], 5, 2, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            case IDC_VDP_REGISTERS_WINDOWBASEY:
                SET_BITS(reg[18], 0, 5, GetDlgItemHex(hwnd, LOWORD(wparam)));
                break;
            }
        }
    }

    return TRUE;
}
#undef SET_BIT
#undef SET_BITS

//----------------------------------------------------------------------------------------
INT_PTR WndProcModeRegisters(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WndProcDialogImplementSaveFieldWhenLostFocus(hwnd, msg, wparam, lparam);
    switch (msg)
    {
    case WM_COMMAND:
        return msgModeRegistersWM_COMMAND(hwnd, wparam, lparam);
    }
    return FALSE;
}

//----------------------------------------------------------------------------------------
INT_PTR WndProcOtherRegisters(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    WndProcDialogImplementSaveFieldWhenLostFocus(hwnd, msg, wparam, lparam);
    switch (msg)
    {
    case WM_COMMAND:
        return msgOtherRegistersWM_COMMAND(hwnd, wparam, lparam);
    }
    return FALSE;
}

INT_PTR CALLBACK WndProcModeRegistersStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    //Obtain the object pointer
    int state = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    //Process the message
    switch (msg)
    {
    case WM_INITDIALOG:
        //Set the object pointer
        state = (int)lparam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(state));

        //Pass this message on to the member window procedure function
        if (state != 0)
        {
            return WndProcModeRegisters(hwnd, msg, wparam, lparam);
        }
        break;
    case WM_DESTROY:
        if (state != 0)
        {
            //Pass this message on to the member window procedure function
            INT_PTR result = WndProcModeRegisters(hwnd, msg, wparam, lparam);

            //Discard the object pointer
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)0);

            //Return the result from processing the message
            return result;
        }
        break;
    }

    //Pass this message on to the member window procedure function
    INT_PTR result = FALSE;
    if (state != 0)
    {
        result = WndProcModeRegisters(hwnd, msg, wparam, lparam);
    }
    return result;
}

INT_PTR CALLBACK WndProcOtherRegistersStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    //Obtain the object pointer
    int state = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    //Process the message
    switch (msg)
    {
    case WM_INITDIALOG:
        //Set the object pointer
        state = (int)lparam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(state));

        //Pass this message on to the member window procedure function
        if (state != 0)
        {
            return WndProcOtherRegisters(hwnd, msg, wparam, lparam);
        }
        break;
    case WM_DESTROY:
        if (state != 0)
        {
            //Pass this message on to the member window procedure function
            INT_PTR result = WndProcOtherRegisters(hwnd, msg, wparam, lparam);

            //Discard the object pointer
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)0);

            //Return the result from processing the message
            return result;
        }
        break;
    }

    //Pass this message on to the member window procedure function
    INT_PTR result = FALSE;
    if (state != 0)
    {
        result = WndProcOtherRegisters(hwnd, msg, wparam, lparam);
    }
    return result;
}

INT_PTR msgRegistersWM_INITDIALOG(HWND hDlg, WPARAM wparam, LPARAM lparam)
{
    //Add our set of tab items to the list of tabs
    tabItems.clear();
    tabItems.push_back(TabInfo("Mode Registers", IDD_VDP_REGISTERS_MODEREGISTERS, WndProcModeRegistersStatic));
    tabItems.push_back(TabInfo("Other Registers", IDD_VDP_REGISTERS_OTHERREGISTERS, WndProcOtherRegistersStatic));

    //Insert our tabs into the tab control
    for (unsigned int i = 0; i < (unsigned int)tabItems.size(); ++i)
    {
        TCITEM tabItem;
        tabItem.mask = TCIF_TEXT;
        tabItem.pszText = (LPSTR)tabItems[i].tabName.c_str();
        SendMessage(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), TCM_INSERTITEM, i, (LPARAM)&tabItem);
    }

    //Create each window associated with each tab, and calculate the required size of the
    //client area of the tab control to fit the largest tab window.
    int requiredTabClientWidth = 0;
    int requiredTabClientHeight = 0;
    for (unsigned int i = 0; i < (unsigned int)tabItems.size(); ++i)
    {
        //Create the dialog window for this tab
        DLGPROC dialogWindowProc = tabItems[i].dialogProc;
        LPCSTR dialogTemplateName = MAKEINTRESOURCE(tabItems[i].dialogID);
        tabItems[i].hwndDialog = CreateDialogParam(dbg_wnd_hinst, dialogTemplateName, GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), dialogWindowProc, (LPARAM)1);

        //Calculate the required size of the window for this tab in pixel units
        RECT rect;
        GetClientRect(tabItems[i].hwndDialog, &rect);
        int tabWidth = rect.right;
        int tabHeight = rect.bottom;

        //Increase the required size of the client area for the tab control to accommodate
        //the contents of this tab, if required.
        requiredTabClientWidth = (tabWidth > requiredTabClientWidth) ? tabWidth : requiredTabClientWidth;
        requiredTabClientHeight = (tabHeight > requiredTabClientHeight) ? tabHeight : requiredTabClientHeight;
    }

    //Save the original size of the tab control
    RECT tabControlOriginalRect;
    GetClientRect(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), &tabControlOriginalRect);
    int tabControlOriginalSizeX = tabControlOriginalRect.right - tabControlOriginalRect.left;
    int tabControlOriginalSizeY = tabControlOriginalRect.bottom - tabControlOriginalRect.top;

    //Calculate the exact required pixel size of the tab control to fully display the
    //content in each tab
    RECT tabControlRect;
    tabControlRect.left = 0;
    tabControlRect.top = 0;
    tabControlRect.right = requiredTabClientWidth;
    tabControlRect.bottom = requiredTabClientHeight;
    SendMessage(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), TCM_ADJUSTRECT, (WPARAM)TRUE, (LPARAM)&tabControlRect);
    int tabControlRequiredSizeX = tabControlRect.right - tabControlRect.left;
    int tabControlRequiredSizeY = tabControlRect.bottom - tabControlRect.top;

    //Resize the tab control
    SetWindowPos(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), NULL, 0, 0, tabControlRequiredSizeX, tabControlRequiredSizeY, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE);

    //Calculate the required pixel size and position of each tab window
    RECT currentTabControlRect;
    GetWindowRect(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), &currentTabControlRect);
    SendMessage(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), TCM_ADJUSTRECT, (WPARAM)FALSE, (LPARAM)&currentTabControlRect);
    POINT tabContentPoint;
    tabContentPoint.x = currentTabControlRect.left;
    tabContentPoint.y = currentTabControlRect.top;
    ScreenToClient(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), &tabContentPoint);
    int tabRequiredPosX = tabContentPoint.x;
    int tabRequiredPosY = tabContentPoint.y;
    int tabRequiredSizeX = currentTabControlRect.right - currentTabControlRect.left;
    int tabRequiredSizeY = currentTabControlRect.bottom - currentTabControlRect.top;

    //Position and size each tab window
    for (unsigned int i = 0; i < (unsigned int)tabItems.size(); ++i)
    {
        SetWindowPos(tabItems[i].hwndDialog, NULL, tabRequiredPosX, tabRequiredPosY, tabRequiredSizeX, tabRequiredSizeY, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
    }

    //Calculate the current size of the owning window
    RECT mainDialogRect;
    GetWindowRect(hDlg, &mainDialogRect);
    int currentMainDialogWidth = mainDialogRect.right - mainDialogRect.left;
    int currentMainDialogHeight = mainDialogRect.bottom - mainDialogRect.top;

    //Resize the owning window to the required size
    int newMainDialogWidth = currentMainDialogWidth + (tabControlRequiredSizeX - tabControlOriginalSizeX);
    int newMainDialogHeight = currentMainDialogHeight + (tabControlRequiredSizeY - tabControlOriginalSizeY);
    SetWindowPos(hDlg, NULL, 0, 0, newMainDialogWidth, newMainDialogHeight, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE);

    //Explicitly select and show the first tab
    activeTabWindow = tabItems[0].hwndDialog;
    ShowWindow(activeTabWindow, SW_SHOWNA);

    return TRUE;
}

static void redraw_vdp_view()
{
    if (!VDPRamHWnd) return;

    RedrawWindow(GetDlgItem(VDPRamHWnd, IDC_VDP_PALETTE), NULL, NULL, RDW_INVALIDATE);
    RedrawWindow(GetDlgItem(VDPRamHWnd, IDC_VDP_TILES), NULL, NULL, RDW_INVALIDATE);
    RedrawWindow(GetDlgItem(VDPRamHWnd, IDC_VDP_TILE_VIEW), NULL, NULL, RDW_INVALIDATE);

    msgModeRegistersUPDATE(tabItems[0].hwndDialog);
    msgOtherRegistersUPDATE(tabItems[1].hwndDialog);
}

BOOL CALLBACK MoveGroupCallback(HWND hChild, LPARAM lParam)
{
    RECT rChild;
    LPRECT r = (LPRECT)lParam;

    GetWindowRect(hChild, &rChild);
    OffsetRect(&rChild, -r->left, -r->top);
    MapWindowPoints(HWND_DESKTOP, GetParent(hChild), (LPPOINT)&rChild, 2);

    SetWindowPos(hChild, NULL,
        rChild.left,
        rChild.top,
        0,
        0,
        SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    return TRUE;
}

typedef struct RgbColor
{
	unsigned char red;
	unsigned char green;
	unsigned char blue;
} RgbColor;

typedef struct HsvColor
{
	int16_t hue; // hue = 0 to 3600 (i.e. 1/10 of a degree)
	int16_t saturation; // saturation = 0 to 1000
	int16_t value; // value = 0 to 1000
} HsvColor;

static RgbColor hsv2rgb(HsvColor hsv)
{
	RgbColor rgb;
	//---------------------------------------------------------------------
	// convert hue, saturation and value (HSV) to red, green and blue
	//---------------------------------------------------------------------

	rgb.red = 0;
	rgb.green = 0;
	rgb.blue = 0;

	if (hsv.saturation == 0)
	{
		rgb.red = (uint8_t)((255 * hsv.value) / 1000);
		rgb.green = rgb.red;
		rgb.blue = rgb.red;
	}
	else
	{
		int16_t h = hsv.hue / 600;
		int16_t f = ((hsv.hue % 600) * 1000) / 600;
		int16_t p = (hsv.value*(1000 - hsv.saturation)) / 1000;
		int16_t q = (hsv.value*(1000 - ((hsv.saturation*f) / 1000))) / 1000;
		int16_t t = (hsv.value*(1000 - ((hsv.saturation*(1000 - f)) / 1000))) / 1000;

		switch (h)
		{
		case 0:

			rgb.red = (uint8_t)((255 * hsv.value) / 1000);
			rgb.green = (uint8_t)((255 * t) / 1000);
			rgb.blue = (uint8_t)((255 * p) / 1000);
			break;

		case 1:

			rgb.red = (uint8_t)((255 * q) / 1000);
			rgb.green = (uint8_t)((255 * hsv.value) / 1000);
			rgb.blue = (uint8_t)((255 * p) / 1000);
			break;

		case 2:

			rgb.red = (uint8_t)((255 * p) / 1000);
			rgb.green = (uint8_t)((255 * hsv.value) / 1000);
			rgb.blue = (uint8_t)((255 * t) / 1000);
			break;

		case 3:

			rgb.red = (uint8_t)((255 * p) / 1000);
			rgb.green = (uint8_t)((255 * q) / 1000);
			rgb.blue = (uint8_t)((255 * hsv.value) / 1000);
			break;

		case 4:

			rgb.red = (uint8_t)((255 * t) / 1000);
			rgb.green = (uint8_t)((255 * p) / 1000);
			rgb.blue = (uint8_t)((255 * hsv.value) / 1000);
			break;

		case 5:

			rgb.red = (uint8_t)((255 * hsv.value) / 1000);
			rgb.green = (uint8_t)((255 * p) / 1000);
			rgb.blue = (uint8_t)((255 * q) / 1000);
			break;

		}
	}

	return rgb;
}

COLORREF cram_9b_to_colorref(unsigned short p)
{
    COLORREF col;
    p = cram_9b_to_16b(p);
    col = normal_pal[(p >> 8) & 0xF] << 0;
    col |= normal_pal[(p >> 4) & 0xF] << 8;
    col |= normal_pal[(p >> 0) & 0xF] << 16;
    return col;
}

LRESULT CALLBACK ButtonsProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        switch (wParam)
        {
        case IDC_VDP_PAL_DUMP:
        {
            char fname[2048];
            strcpy(fname, "pal.bin");
            if (select_file_load(fname, ".", "Save Dump Pal As...", "All Files\0*.*\0\0", "*.*", hWnd))
            {
                FILE *out = fopen(fname, "wb+");
                fwrite(cram, 1, sizeof(cram), out);
                fclose(out);
            }

            return FALSE;
        } break;
        case IDC_VDP_PAL_LOAD:
        {
            char fname[2048];
            strcpy(fname, "pal.bin");
            if (select_file_load(fname, ".", "Load Dump Pal As...", "All Files\0*.*\0\0", "*.*", hWnd))
            {
                FILE *in = fopen(fname, "rb");
                fread(cram, 1, sizeof(cram), in);
                fclose(in);
                redraw_vdp_view();
            }
            return FALSE;
        } break;
        case IDC_VDP_PAL_YY:
        {
            char fname[2048];
            strcpy(fname, "pal.pal");
            if (select_file_save(fname, ".", "Save YY-CHR Pal As...", "All Files\0*.*\0\0", "*.*", hWnd))
            {
                FILE *out = fopen(fname, "wb");
                int i = 0;
                for (i = 0; i < VDP_PAL_COLORS * VDP_PAL_COUNT; ++i)
                {
                    fwrite((const void *)cram_9b_to_colorref(*(unsigned short *)&cram[i]), 1, 3, out);
                }
                *((DWORD*)fname) = 0;
                for (; i < 256; ++i)
                    fwrite(fname, 1, 3, out);
                fclose(out);
            }
            return FALSE;
        } break;
		case IDC_VDP_PAL_RNB:
		{
			for (int i = 0; i < VDP_PAL_COLORS; ++i)
			{
				uint16_t w = 0;
				uint8_t c = (i * VDP_PAL_COLORS) & 0xFF;
				w |= (uint16_t)(((c >> 4) & 0xE) << 0);
				w |= (uint16_t)(((c >> 4) & 0xE) << 4);
				w |= (uint16_t)(((c >> 4) & 0xE) << 8);

                w = cram_16b_to_9b(w);
				((char*)&cram)[i * 2 + 0] = (w >> 0) & 0xFF;
				((char*)&cram)[i * 2 + 1] = (w >> 8) & 0xFF;
			}

			for (int i = 0; i < VDP_PAL_COLORS; ++i)
			{
				uint16_t w = 0;
				uint8_t c = ((255 - i) * VDP_PAL_COLORS) & 0xFF;
				w |= (uint16_t)(((c >> 4) & 0xE) << 0);
				w |= (uint16_t)(((c >> 4) & 0xE) << 4);
				w |= (uint16_t)(((c >> 4) & 0xE) << 8);

                w = cram_16b_to_9b(w);
				((char*)&cram)[(VDP_PAL_COLORS + i) * 2 + 0] = (w >> 0) & 0xFF;
				((char*)&cram)[(VDP_PAL_COLORS + i) * 2 + 1] = (w >> 8) & 0xFF;
			}

			for (int i = 0; i < VDP_PAL_COLORS; ++i)
			{
				HsvColor _hsv;
				_hsv.hue = (((double)i / (double)VDP_PAL_COLORS)) * 3600;
				_hsv.saturation = 850;
				_hsv.value = 1000;
				RgbColor _rgb = hsv2rgb(_hsv);

				uint16_t w = 0;
				w |= (uint16_t)((((int)_rgb.red >> 4) & 0xE) << 0);
				w |= (uint16_t)((((int)_rgb.green >> 4) & 0xE) << 4);
				w |= (uint16_t)((((int)_rgb.blue >> 4) & 0xE) << 8);

                w = cram_16b_to_9b(w);
				((char*)&cram)[(VDP_PAL_COLORS * 2 + i) * 2 + 0] = (w >> 0) & 0xFF;
				((char*)&cram)[(VDP_PAL_COLORS * 2 + i) * 2 + 1] = (w >> 8) & 0xFF;
			}
            redraw_vdp_view();

			return FALSE;
		} break;
        case IDC_VDP_VRAM_DUMP:
        {
            char fname[2048];
            strcpy(fname, "vram.bin");
            if (select_file_save(fname, ".", "Save Dump VRAM As...", "All Files\0*.*\0\0", "*.*", hWnd))
            {
                FILE *out = fopen(fname, "wb");
                fwrite(vram, 1, sizeof(vram), out);
                fclose(out);
            }
            return FALSE;
        } break;
        case IDC_VDP_VRAM_LOAD:
        {
            char fname[2048];
            strcpy(fname, "vram.bin");
            if (select_file_load(fname, ".", "Load Dump VRAM As...", "All Files\0*.*\0\0", "*.*", hWnd))
            {
                FILE *in = fopen(fname, "rb");
                fread(vram, 1, sizeof(vram), in);
                fclose(in);
                redraw_vdp_view();
            }
            return FALSE;
        } break;
        case IDC_VDP_VIEW_VRAM:
        {
            IsVRAM = true;
            redraw_vdp_view();
            return FALSE;
        } break;
        case IDC_VDP_VIEW_RAM:
        {
            IsVRAM = false;
            redraw_vdp_view();
            return FALSE;
        } break;
        }
    } break;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK VDPRamProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT r, r2, r3;
    int dx1, dy1, dx2, dy2;
    static int watchIndex = 0;

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        IsVRAM = true;
        CheckRadioButton(hDlg, IDC_VDP_VIEW_VRAM, IDC_VDP_VIEW_RAM, IDC_VDP_VIEW_VRAM);

        VDPRamHWnd = hDlg;

        memset(&r, 0, sizeof(r));

        GetWindowRect(dbg_window, &r);
        dx1 = (r.right - r.left) / 2;
        dy1 = (r.bottom - r.top) / 2;

        GetWindowRect(hDlg, &r2);
        dx2 = (r2.right - r2.left) / 2;
        dy2 = (r2.bottom - r2.top) / 2;

        // push it away from the main window if we can
        const int width = (r.right - r.left);
        const int width2 = (r2.right - r2.left);
        if (r.left + width2 + width < GetSystemMetrics(SM_CXSCREEN))
        {
            r.right += width;
            r.left += width;
        }
        else if ((int)r.left - (int)width2 > 0)
        {
            r.right -= width2;
            r.left -= width2;
        }

        SetWindowPos(hDlg, NULL, r.left, r.top, NULL, NULL, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

        // Palette view
        HWND hPalette = GetDlgItem(hDlg, IDC_VDP_PALETTE);
        SetWindowPos(hPalette, NULL,
            5,
            5,
            VDP_PAL_COLORS * VDP_BLOCK_W,
            VDP_PAL_COUNT * VDP_BLOCK_H,
            SWP_NOZORDER | SWP_NOACTIVATE);
        // Palette view

        // Tiles view
        HWND hTiles = GetDlgItem(hDlg, IDC_VDP_TILES);
        GetWindowRect(hPalette, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        SetWindowPos(hTiles, NULL,
            r.left,
            r.bottom + 5,
            VDP_TILES_IN_ROW * VDP_BLOCK_W,
            VDP_TILES_IN_COL * VDP_BLOCK_H,
            SWP_NOZORDER | SWP_NOACTIVATE);
        // Tiles view

        // Scrollbar
        GetWindowRect(hTiles, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        HWND hScrollbar = GetDlgItem(hDlg, IDC_VDP_TILES_SCROLLBAR);
        SetWindowPos(hScrollbar, NULL,
            r.right + 1,
            r.top,
            VDP_BLOCK_W,
            (r.bottom - r.top),
            SWP_NOZORDER | SWP_NOACTIVATE);
        SetScrollRange(hScrollbar, SB_CTL, 0, VDP_SCROLL_MAX, TRUE);
        // Scrollbar

        // Palette group
        HWND hPalGroup = GetDlgItem(hDlg, IDC_VDP_PAL_GROUP);
        GetWindowRect(hScrollbar, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        GetWindowRect(hPalette, &r2);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r2, 2);
        GetWindowRect(hPalGroup, &r3);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r3, 2);

        SetParent(GetDlgItem(hDlg, IDC_VDP_PAL_DUMP), hPalGroup);
        SetParent(GetDlgItem(hDlg, IDC_VDP_PAL_LOAD), hPalGroup);
        SetParent(GetDlgItem(hDlg, IDC_VDP_PAL_YY), hPalGroup);
		SetParent(GetDlgItem(hDlg, IDC_VDP_PAL_RNB), hPalGroup);

        SetWindowPos(hPalGroup, NULL,
            r.right + 5,
            r2.top,
            0,
            0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        GetWindowRect(hPalGroup, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        SubtractRect(&r3, &r3, &r);
        EnumChildWindows(hPalGroup, MoveGroupCallback, (LPARAM)&r3);

        SetWindowSubclass(hPalGroup, ButtonsProc, 0, 0);
        // Palette group

        // VRAM group
        HWND hVramGroup = GetDlgItem(hDlg, IDC_VDP_VRAM_GROUP);
        GetWindowRect(hVramGroup, &r3);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r3, 2);

        SetParent(GetDlgItem(hDlg, IDC_VDP_VRAM_DUMP), hVramGroup);
        SetParent(GetDlgItem(hDlg, IDC_VDP_VRAM_LOAD), hVramGroup);

        SetWindowPos(hVramGroup, NULL,
            r.left,
            r.bottom + 5,
            0,
            0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        GetWindowRect(hVramGroup, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        SubtractRect(&r3, &r3, &r);
        EnumChildWindows(hVramGroup, MoveGroupCallback, (LPARAM)&r3);

        SetWindowSubclass(hVramGroup, ButtonsProc, 0, 0);
        // VRAM group

        // View mode group
        HWND hViewMode = GetDlgItem(hDlg, IDC_VDP_VIEW_MODE);
        GetWindowRect(hViewMode, &r3);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r3, 2);

        SetParent(GetDlgItem(hDlg, IDC_VDP_VIEW_VRAM), hViewMode);
        SetParent(GetDlgItem(hDlg, IDC_VDP_VIEW_RAM), hViewMode);

        SetWindowPos(hViewMode, NULL,
            r.left,
            r.bottom + 5,
            0,
            0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        GetWindowRect(hViewMode, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        SubtractRect(&r3, &r3, &r);
        EnumChildWindows(hViewMode, MoveGroupCallback, (LPARAM)&r3);

        SetWindowSubclass(hViewMode, ButtonsProc, 0, 0);
        // View mode group

        // Tile view
        HWND hTileView = GetDlgItem(hDlg, IDC_VDP_TILE_VIEW);
        GetWindowRect(hViewMode, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        SetWindowPos(hTileView, NULL,
            r.left,
            r.bottom + 10,
            r.right - r.left,
            r.right - r.left,
            SWP_NOZORDER | SWP_NOACTIVATE);
        // Tile view

        // Tile info
        HWND hTileInfo = GetDlgItem(hDlg, IDC_VDP_TILE_INFO);
        GetWindowRect(hTileView, &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);
        SetWindowPos(hTileInfo, NULL,
            r.left,
            r.bottom + 10,
            r.right - r.left,
            50,
            SWP_NOZORDER | SWP_NOACTIVATE);
        // Tile info

        // Exodus VDP Regs window init
        msgRegistersWM_INITDIALOG(hDlg, wParam, lParam);

        GetWindowRect(hDlg, &r);
        GetWindowRect(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), &r3);
        SetWindowPos(hDlg, NULL,
            0,
            0,
            r3.right - r.left + 5,
            r3.bottom - r.top + 5,
            SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        return true;
    } break;

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;

        if ((UINT)wParam == IDC_VDP_PALETTE)
        {
            BYTE* pdst;
            BITMAPINFOHEADER bmih = { sizeof(BITMAPINFOHEADER), VDP_PAL_COLORS, -VDP_PAL_COUNT, 1, 32 };

            HDC hSmallDC = CreateCompatibleDC(di->hDC);
            HBITMAP hSmallBmp = CreateDIBSection(hSmallDC, (BITMAPINFO *)&bmih, DIB_RGB_COLORS, (void **)&pdst, NULL, NULL);
            HBITMAP hOldSmallBmp = (HBITMAP)SelectObject(hSmallDC, hSmallBmp);

            COLORREF * cr = (COLORREF*)&pdst[0];
            unsigned short * pal = (unsigned short *)(&cram[0]);

            for (int y = 0; y < VDP_PAL_COUNT; ++y)
                for (int x = 0; x < VDP_PAL_COLORS; ++x)
                {
                    *cr++ = cram_9b_to_colorref(*pal++);
                }

            StretchDIBits(
                di->hDC,
                0,
                0,
                di->rcItem.right - di->rcItem.left,
                di->rcItem.bottom - di->rcItem.top,
                0,
                0,
                VDP_PAL_COLORS,
                VDP_PAL_COUNT,
                pdst,
                (const BITMAPINFO *)&bmih,
                DIB_RGB_COLORS,
                SRCCOPY
            );

            SelectObject(hSmallDC, hOldSmallBmp);
            DeleteObject(hSmallBmp);
            DeleteDC(hSmallDC);

            r.left = di->rcItem.left;
            r.right = r.left + VDP_PAL_COLORS * VDP_BLOCK_W;
            r.top = di->rcItem.top + VDPRamPal * VDP_BLOCK_H;
            r.bottom = r.top + VDP_BLOCK_H;
            DrawFocusRect(di->hDC, &r);

            return TRUE;
        }
        else if ((UINT)wParam == IDC_VDP_TILES)
        {
            int scroll = GetScrollPos(GetDlgItem(hDlg, IDC_VDP_TILES_SCROLLBAR), SB_CTL);
            int start = scroll * VDP_TILES_IN_ROW;
            int end = start + VDP_TILES_IN_ROW * VDP_TILES_IN_COL;
            int tiles = end - start;

            BYTE* pdst;
            BITMAPINFOHEADER bmih = { sizeof(BITMAPINFOHEADER), VDP_TILE_W * VDP_TILES_IN_ROW, -VDP_TILE_H * VDP_TILES_IN_COL, 1, 32 };

            HDC hSmallDC = CreateCompatibleDC(di->hDC);
            HBITMAP hSmallBmp = CreateDIBSection(hSmallDC, (BITMAPINFO *)&bmih, DIB_RGB_COLORS, (void **)&pdst, NULL, NULL);
            HBITMAP hOldSmallBmp = (HBITMAP)SelectObject(hSmallDC, hSmallBmp);

            COLORREF * cr = (COLORREF*)&pdst[0];
            unsigned short * pal = (unsigned short *)(&cram[0]);

            BYTE *ptr = (BYTE *)(IsVRAM ? vram : work_ram);
            for (int i = 0; i < tiles; ++i)
            {
                for (int y = 0; y < VDP_TILE_H; ++y)
                {
                    for (int x = 0; x < (VDP_TILE_W / 2); ++x)
                    {
                        int _x1 = (i % VDP_TILES_IN_ROW) * VDP_TILE_W + x * 2 + 0;
                        int _x2 = (i % VDP_TILES_IN_ROW) * VDP_TILE_W + x * 2 + 1;
                        int _y = (i / VDP_TILES_IN_ROW) * VDP_TILE_H + y;

                        BYTE t = ptr[(start + i) * 0x20 + y * (VDP_TILE_W / 2) + (x ^ 1)];
                        COLORREF c1 = cram_9b_to_colorref(pal[VDP_PAL_COLORS * VDPRamPal + (t >> 4)]);
                        COLORREF c2 = cram_9b_to_colorref(pal[VDP_PAL_COLORS * VDPRamPal + (t & 0xF)]);

                        cr[(_y * VDP_TILES_IN_ROW * VDP_TILE_W + _x1) + 0] = c1;
                        cr[(_y * VDP_TILES_IN_ROW * VDP_TILE_W + _x2) + 0] = c2;
                    }
                }
            }

            StretchDIBits(
                di->hDC,
                0,
                0,
                di->rcItem.right - di->rcItem.left,
                di->rcItem.bottom - di->rcItem.top,
                0,
                0,
                VDP_TILE_W * VDP_TILES_IN_ROW,
                VDP_TILE_H * VDP_TILES_IN_COL,
                pdst,
                (const BITMAPINFO *)&bmih,
                DIB_RGB_COLORS,
                SRCCOPY
            );

            SelectObject(hSmallDC, hOldSmallBmp);
            DeleteObject(hSmallBmp);
            DeleteDC(hSmallDC);

            r.left = di->rcItem.left + (VDPRamTile % VDP_TILES_IN_ROW) * VDP_BLOCK_W;
            r.right = r.left + VDP_BLOCK_W;
            int row = (VDPRamTile / VDP_TILES_IN_ROW) - scroll;
            r.top = di->rcItem.top + row * VDP_BLOCK_H;
            r.bottom = r.top + VDP_BLOCK_H;

            if (row >= 0 && row < VDP_TILES_IN_COL)
                DrawFocusRect(di->hDC, &r);

            return TRUE;
        }
        else if ((UINT)wParam == IDC_VDP_TILE_VIEW)
        {
            BYTE* pdst;
            BITMAPINFOHEADER bmih = { sizeof(BITMAPINFOHEADER), VDP_TILE_W, -VDP_TILE_H, 1, 32 };

            HDC hSmallDC = CreateCompatibleDC(di->hDC);
            HBITMAP hSmallBmp = CreateDIBSection(hSmallDC, (BITMAPINFO *)&bmih, DIB_RGB_COLORS, (void **)&pdst, NULL, NULL);
            HBITMAP hOldSmallBmp = (HBITMAP)SelectObject(hSmallDC, hSmallBmp);

            COLORREF * cr = (COLORREF*)&pdst[0];
            unsigned short * pal = (unsigned short *)(&cram[0]);

            BYTE *ptr = (BYTE *)(IsVRAM ? vram : work_ram);
            for (int y = 0; y < VDP_TILE_H; ++y)
            {
                for (int x = 0; x < (VDP_TILE_W / 2); ++x)
                {
                    BYTE t = ptr[VDPRamTile * 0x20 + y * (VDP_TILE_W / 2) + (x ^ 1)];

                    COLORREF c1 = cram_9b_to_colorref(pal[VDP_PAL_COLORS * VDPRamPal + (t >> 4)]);
                    COLORREF c2 = cram_9b_to_colorref(pal[VDP_PAL_COLORS * VDPRamPal + (t & 0xF)]);

                    cr[y * VDP_TILE_W + x * 2 + 0] = c1;
                    cr[y * VDP_TILE_W + x * 2 + 1] = c2;
                }
            }

            StretchDIBits(
                di->hDC,
                0,
                0,
                di->rcItem.right - di->rcItem.left,
                di->rcItem.bottom - di->rcItem.top,
                0,
                0,
                VDP_TILE_W,
                VDP_TILE_H,
                pdst,
                (const BITMAPINFO *)&bmih,
                DIB_RGB_COLORS,
                SRCCOPY
            );

            SelectObject(hSmallDC, hOldSmallBmp);
            DeleteObject(hSmallBmp);
            DeleteDC(hSmallDC);

            char buff[30];
            sprintf(buff, "Offset: %04X\r\nId: %03X", (VDPRamTile * 0x20) | (IsVRAM ? 0x0000 : 0xFF0000), VDPRamTile);
            SetDlgItemText(hDlg, IDC_VDP_TILE_INFO, buff);

            return TRUE;
        }
    } break;

    case WM_NOTIFY:
    {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->idFrom == IDC_VDP_REGISTERS_TABCONTROL)
        {
            if ((nmhdr->code == TCN_SELCHANGE))
            {
                HDWP deferWindowPosSession = BeginDeferWindowPos(2);

                if (activeTabWindow != NULL)
                {
                    DeferWindowPos(deferWindowPosSession, activeTabWindow, NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_HIDEWINDOW);
                    activeTabWindow = NULL;
                }

                int currentlySelectedTab = (int)SendMessage(nmhdr->hwndFrom, TCM_GETCURSEL, 0, 0);
                if ((currentlySelectedTab < 0) || (currentlySelectedTab >= (int)tabItems.size()))
                {
                    currentlySelectedTab = 0;
                }
                activeTabWindow = tabItems[currentlySelectedTab].hwndDialog;
                DeferWindowPos(deferWindowPosSession, activeTabWindow, NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);

                EndDeferWindowPos(deferWindowPosSession);
            }
        }
        return TRUE;
    } break;

    case WM_VSCROLL:
    {
        int CurPos = GetScrollPos(GetDlgItem(hDlg, IDC_VDP_TILES_SCROLLBAR), SB_CTL);
        int nSBCode = LOWORD(wParam);
        int nPos = HIWORD(wParam);
        switch (nSBCode)
        {
        case SB_LEFT:      // Scroll to far left.
            CurPos = 0;
            break;

        case SB_RIGHT:      // Scroll to far right.
            CurPos = VDP_SCROLL_MAX;
            break;

        case SB_ENDSCROLL:   // End scroll.
            break;

        case SB_LINELEFT:      // Scroll left.
            if (CurPos > 0)
                CurPos--;
            break;

        case SB_LINERIGHT:   // Scroll right.
            if (CurPos < VDP_SCROLL_MAX)
                CurPos++;
            break;

        case SB_PAGELEFT:    // Scroll one page left.
            CurPos -= VDP_TILES_IN_COL;
            if (CurPos < 0)
                CurPos = 0;
            break;

        case SB_PAGERIGHT:      // Scroll one page righ
            CurPos += VDP_TILES_IN_COL;
            if (CurPos >= VDP_SCROLL_MAX)
                CurPos = VDP_SCROLL_MAX - 1;
            break;

        case SB_THUMBTRACK:   // Drag scroll box to specified position. nPos is the
        case SB_THUMBPOSITION: // Scroll to absolute position. nPos is the position
        {
            SCROLLINFO si;
            ZeroMemory(&si, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;

            // Call GetScrollInfo to get current tracking
            //    position in si.nTrackPos

            if (!GetScrollInfo(GetDlgItem(hDlg, IDC_VDP_TILES_SCROLLBAR), SB_CTL, &si))
                return 1; // GetScrollInfo failed
            CurPos = si.nTrackPos;
        } break;
        }
        SetScrollPos(GetDlgItem(hDlg, IDC_VDP_TILES_SCROLLBAR), SB_CTL, CurPos, TRUE);
        redraw_vdp_view();
    } break;

    case WM_LBUTTONDOWN:
    {
        RECT r;
        POINT pt;

        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        GetWindowRect(GetDlgItem(hDlg, IDC_VDP_PALETTE), &r);
        MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);

        if (PtInRect(&r, pt))
        {
            VDPRamPal = (pt.y - r.top) / VDP_BLOCK_H;
            redraw_vdp_view();
        }
        else
        {
            GetWindowRect(GetDlgItem(hDlg, IDC_VDP_TILES), &r);
            MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&r, 2);

            if (PtInRect(&r, pt))
            {
                int scroll = GetScrollPos(GetDlgItem(hDlg, IDC_VDP_TILES_SCROLLBAR), SB_CTL);
                int row = (pt.y - r.top) / VDP_BLOCK_H + scroll;
                int col = (pt.x - r.left) / VDP_BLOCK_W;
                VDPRamTile = row * VDP_TILES_IN_ROW + col;

                redraw_vdp_view();
            }
        }
    } break;

    case UpdateMSG:
    {
        redraw_vdp_view();
    } break;

    case WM_CLOSE:
    {
        if (activeTabWindow != NULL)
        {
            DestroyWindow(activeTabWindow);
            activeTabWindow = NULL;
        }

        VDPRamHWnd = NULL;
        PostQuitMessage(0);
        EndDialog(hDlg, 0);
        return TRUE;
    } break;
    }

    return FALSE;
}

static DWORD WINAPI ThreadProc(LPVOID lpParam)
{
    MSG messages;

    VDPRamHWnd = CreateDialog(dbg_wnd_hinst, MAKEINTRESOURCE(IDD_VDPRAM), dbg_window, (DLGPROC)VDPRamProc);

    while (GetMessage(&messages, NULL, 0, 0))
    {
        TranslateMessage(&messages);
        DispatchMessage(&messages);
	}

    return 1;
}

void create_vdp_ram_debug()
{
    hThread = CreateThread(0, NULL, ThreadProc, NULL, NULL, NULL);
}

void destroy_vdp_ram_debug()
{
    SendMessage(VDPRamHWnd, WM_CLOSE, 0, 0);

    CloseHandle(hThread);
}

void update_vdp_ram_debug()
{
    if (VDPRamHWnd)
        SendMessage(VDPRamHWnd, UpdateMSG, 0, 0);
}
