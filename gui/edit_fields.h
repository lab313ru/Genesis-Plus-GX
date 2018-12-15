#pragma once

#include <Windows.h>
#include <string>

std::string make_hex_string(unsigned int width, unsigned int data);
std::string GetDlgItemString(HWND hwnd, int controlID);
unsigned int GetDlgItemHex(HWND hwnd, int controlID);
void UpdateDlgItemHex(HWND hwnd, int controlID, unsigned int width, unsigned int data);
void UpdateDlgItemBin(HWND hwnd, int controlID, unsigned int data);
