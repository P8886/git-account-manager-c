#pragma once
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>

// 共享常量
#define MAX_ACCOUNTS 50
#define ID_LEN 64
#define NAME_LEN 256
#define EMAIL_LEN 256
#define PATH_LEN 1024

// 颜色定义
#define COLOR_DARK_BG RGB(32, 32, 32)
#define COLOR_DARK_CONTROL RGB(40, 40, 40)
#define COLOR_DARK_TEXT RGB(220, 220, 220)

// 跨文件共享的全局变量
extern BOOL isDarkMode;
extern HBRUSH hBrushDark;
extern HBRUSH hBrushControlDark;
extern HBRUSH hBrushLight;
extern HFONT hGlobalFont;
extern float g_dpiScale; // DPI 缩放比例

// DPI 缩放辅助函数
int DPI(int value);

// main.c 中辅助函数的如前声明
wchar_t* U8ToW(const char* utf8);
char* WToU8(const wchar_t* wstr);
void SetTitleBarTheme(HWND hwnd, BOOL dark);
int ShowMessage(HWND owner, LPCWSTR text, LPCWSTR title, UINT type);
BOOL CALLBACK SetChildFont(HWND hwndChild, LPARAM lParam);
