#include <sstream>
#include <iomanip>

#include "edit_fields.h"

std::string GetDlgItemString(HWND hwnd, int controlID)
{
    std::string result;

    const unsigned int maxTextLength = 1024;
    char currentTextTemp[maxTextLength];
    if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
    {
        currentTextTemp[0] = '\0';
    }
    result = currentTextTemp;

    return result;
}

unsigned int GetDlgItemHex(HWND hwnd, int controlID)
{
    unsigned int value = 0;

    const unsigned int maxTextLength = 1024;
    char currentTextTemp[maxTextLength];
    if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
    {
        currentTextTemp[0] = '\0';
    }
    std::stringstream buffer;
    buffer << std::hex << currentTextTemp;
    buffer >> value;

    return value;
}

std::string make_hex_string(unsigned int width, unsigned int data)
{
    std::string result;
    std::stringstream text;
    text << std::setw(width) << std::setfill('0') << std::hex << std::uppercase;
    text << data;
    return text.str();
}

void UpdateDlgItemHex(HWND hwnd, int controlID, unsigned int width, unsigned int data)
{
    const unsigned int maxTextLength = 1024;
    char currentTextTemp[maxTextLength];
    if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
    {
        currentTextTemp[0] = '\0';
    }
    std::string currentText = currentTextTemp;
    std::string result = make_hex_string(width, data);

    if (result != currentText)
    {
        SetDlgItemText(hwnd, controlID, result.c_str());
    }
}

void UpdateDlgItemBin(HWND hwnd, int controlID, unsigned int data)
{
    const unsigned int maxTextLength = 1024;
    char currentTextTemp[maxTextLength];
    if (GetDlgItemText(hwnd, controlID, currentTextTemp, maxTextLength) == 0)
    {
        currentTextTemp[0] = '\0';
    }
    std::string currentText = currentTextTemp;
    std::stringstream text;
    text << data;
    if (text.str() != currentText)
    {
        SetDlgItemText(hwnd, controlID, text.str().c_str());
    }
}

