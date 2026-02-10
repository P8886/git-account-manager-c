#pragma once
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>

// Shared Constants
#define MAX_ACCOUNTS 50
#define ID_LEN 64
#define NAME_LEN 256
#define EMAIL_LEN 256
#define PATH_LEN 1024

// Colors
#define COLOR_DARK_BG RGB(32, 32, 32)
#define COLOR_DARK_CONTROL RGB(40, 40, 40)
#define COLOR_DARK_TEXT RGB(220, 220, 220)

// Globals shared across files
extern BOOL isDarkMode;
extern HBRUSH hBrushDark;
extern HBRUSH hBrushControlDark;
extern HBRUSH hBrushLight;
extern HFONT hGlobalFont;

// Forward declaration of helper functions in main.c
wchar_t* U8ToW(const char* utf8);
char* WToU8(const wchar_t* wstr);
void SetTitleBarTheme(HWND hwnd, BOOL dark);
int ShowMessage(HWND owner, LPCWSTR text, LPCWSTR title, UINT type);
BOOL CALLBACK SetChildFont(HWND hwndChild, LPARAM lParam);
