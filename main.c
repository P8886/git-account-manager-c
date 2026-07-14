#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dwmapi.h> // 引入 DWM API
#include <shlobj.h> // 用于 SHGetFolderPath
#include <shellapi.h>
#include "shared.h"
#include "logic.h"
#include "ui_draw.h"
#include "ui_gen_key.h"
#include "ui_taskbar.h"
#include "ui_tray.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

// 控件 ID 定义
#define ID_LIST 101
#define ID_EDIT_NAME 102
#define ID_EDIT_EMAIL 103
#define ID_COMBO_SSH 104
#define ID_BTN_BROWSE 105
#define ID_BTN_SAVE 106
#define ID_BTN_DELETE 107
#define ID_BTN_SWITCH 108
#define ID_STATUS 109
#define ID_BTN_CANCEL 110
#define ID_BTN_THEME 111
#define ID_GROUP_DETAILS 112
#define ID_LBL_DETAILS 113
#define ID_BTN_GENERATE 114
#define ID_COMBO_HOST 115
#define ID_LBL_HOST_HINT 116  // SSH Hosts 提示文字
#define ID_BTN_TASKBAR 117
#define ID_BTN_NEW 118
#define ID_HEADER_TITLE 119
#define ID_HEADER_SUBTITLE 120
#define ID_SECTION_ACCOUNTS 121
#define ID_SECTION_DETAILS 122
#define ID_SECTION_SSH 123
#define ID_HOST_COMBO_PREFIX 200  // 动态创建的host下拉框ID前缀
#define ID_HOST_DELETE_PREFIX 300 // 动态创建的host删除按钮ID前缀
#define ID_BTN_ADD_HOST 400       // 新增host按钮
#define ID_TIMER_IDENTITY 1
#define ID_TIMER_TASKBAR 2
#define UI_CONTROL_HEIGHT 32
#define UI_ACCOUNT_ITEM_HEIGHT 56

// 全局变量
HWND hMainWnd;  // 主窗口句柄
HWND hList, hName, hEmail, hSSH, hStatus, hBtnSave, hBtnCancel, hBtnDelete, hBtnSwitch, hBtnGenerate;
HWND hLblName, hLblEmail, hLblSSH, hLblHost, hHeaderTitle, hHeaderSubtitle;
HWND hSectionAccounts, hSectionDetails, hSectionSSH, hBtnTaskbar, hBtnTheme, hBtnNew;
HWND hLblHostHint;  // SSH Hosts 提示文字控件
HWND hHostControls[20];  // 存储host控件的句柄数组（下拉框和删除按钮配对，最多10对 = 20个句柄）
int hHostControlCount = 1;  // 当前host控件的数量，默认为1（初始控件）
HWND hBtnAddHost;  // 新增host按钮
Config config;
char currentEditID[ID_LEN] = "";
BOOL isDarkMode = FALSE;
BOOL g_bIgnoreEditChange = FALSE;
HBRUSH hBrushDark, hBrushLight, hBrushControlDark, hBrushControlLight;
HFONT hGlobalFont = NULL; // 全局字体句柄
HFONT hHintFont = NULL;   // 提示文字字体句柄（较小）
HFONT hTitleFont = NULL;
HFONT hSectionFont = NULL;
float g_dpiScale = 1.0f;  // DPI 缩放比例 (2K屏幕通常为 1.25 或 1.5)
HANDLE g_hMutex = NULL;   // 单实例互斥体句柄
TaskbarIdentity g_taskbarIdentity;
TrayIdentity g_trayIdentity;
BOOL g_exitRequested = FALSE;
wchar_t g_identityTitle[512] = L"Git 全局身份";
wchar_t g_identityDetail[512] = L"未配置";
char g_globalName[NAME_LEN] = "";
char g_globalEmail[EMAIL_LEN] = "";
BOOL g_isLayingOut = FALSE;
BOOL g_deferHostLayout = FALSE;
BOOL g_configLoadFailed = FALSE;

// 函数声明
void RepositionLowerControls();
void AddHostControl(const wchar_t* initialHost);
void RemoveHostControl(int index);
void ClearHostControls();
BOOL UpdateAccountHosts(Account* acc);
void PopulateHostControls(Account* acc);
void LayoutMainWindow(BOOL resizeToContent);
void UpdateIdentitySurfaces(void);

// DPI 缩放辅助函数
int DPI(int value) {
    return (int)(value * g_dpiScale);
}

// 获取系统 DPI 缩放比例
void InitDPIScale() {
    HDC hdc = GetDC(NULL);
    if (hdc) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        g_dpiScale = dpi / 96.0f; // 96 是标准 DPI
        ReleaseDC(NULL, hdc);
    }
}

// 动态加载 SetWindowTheme
typedef HRESULT (WINAPI *PSetWindowTheme)(HWND, LPCWSTR, LPCWSTR);

void MySetWindowTheme(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList) {
    HMODULE hMod = LoadLibraryW(L"uxtheme.dll");
    if (hMod) {
        PSetWindowTheme pFunc = NULL;
        FARPROC proc = GetProcAddress(hMod, "SetWindowTheme");
        if (proc) memcpy(&pFunc, &proc, sizeof(pFunc));
        if (pFunc) {
            pFunc(hwnd, pszSubAppName, pszSubIdList);
        }
        FreeLibrary(hMod);
    }
}

// --- UTF-8 与 WideChar 转换辅助函数 ---

wchar_t* U8ToW(const char* utf8) {
    static wchar_t buffer[1024]; // 减小 buffer 以节省栈/BSS
    if (!utf8) return L"";
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buffer, 1024);
    return buffer;
}

char* WToU8(const wchar_t* wstr) {
    static char buffer[1024];
    if (!wstr) return "";
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buffer, 1024, NULL, NULL);
    return buffer;
}

static BOOL U8ToWBuffer(const char* utf8, wchar_t* output, int capacity) {
    if (!output || capacity <= 0) return FALSE;
    output[0] = L'\0';
    if (!utf8) return TRUE;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                               output, capacity) > 0;
}

static BOOL WToU8Buffer(const wchar_t* wide, char* output, int capacity) {
    if (!output || capacity <= 0) return FALSE;
    output[0] = '\0';
    if (!wide) return TRUE;
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, output, capacity,
                               NULL, NULL) > 0;
}

static HFONT CreateUiFont(int points, int weight) {
    int dpi = (int)(96.0f * g_dpiScale + 0.5f);
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                       L"Microsoft YaHei UI");
}

#define CENTERED_EDIT_PROC_PROP L"GAM_CenteredEditOriginalProc"
#define MODERN_COMBO_PROC_PROP L"GAM_ModernComboOriginalProc"
#define MODERN_COMBO_EDIT_PROC_PROP L"GAM_ModernComboEditOriginalProc"

static void SetRoundedControlRegion(HWND control, int radius) {
    if (!control || radius <= 0) return;
    RECT client;
    if (!GetClientRect(control, &client)) return;
    HRGN region = CreateRoundRectRgn(
        0, 0, client.right + 1, client.bottom + 1, radius, radius);
    if (region && !SetWindowRgn(control, region, TRUE)) DeleteObject(region);
}

void CenterEditText(HWND edit) {
    if (!edit) return;
    RECT client;
    if (!GetClientRect(edit, &client)) return;
    HFONT font = (HFONT)SendMessageW(edit, WM_GETFONT, 0, 0);
    HDC dc = GetDC(edit);
    if (!dc) return;
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : NULL;
    TEXTMETRICW metrics;
    GetTextMetricsW(dc, &metrics);
    if (oldFont) SelectObject(dc, oldFont);
    ReleaseDC(edit, dc);

    int clientHeight = client.bottom - client.top;
    int top = (clientHeight - metrics.tmHeight) / 2;
    if (top < 0) top = 0;
    RECT format = client;
    format.left += DPI(8);
    format.right -= DPI(8);
    format.top = top;
    format.bottom = top + metrics.tmHeight + DPI(1);
    SendMessageW(edit, EM_SETRECTNP, 0, (LPARAM)&format);
    SetRoundedControlRegion(edit, DPI(4));
    InvalidateRect(edit, NULL, TRUE);
}

static WNDPROC GetOriginalEditProc(HWND edit) {
    LONG_PTR value = (LONG_PTR)GetPropW(edit, CENTERED_EDIT_PROC_PROP);
    WNDPROC proc = NULL;
    memcpy(&proc, &value, sizeof(proc));
    return proc;
}

static void RemoveLineBreaksFromEdit(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    if (length <= 0 || length >= PATH_LEN) return;
    wchar_t text[PATH_LEN];
    GetWindowTextW(edit, text, PATH_LEN);
    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessageW(edit, EM_GETSEL, (WPARAM)&selectionStart, (LPARAM)&selectionEnd);
    BOOL changed = FALSE;
    int removedBeforeStart = 0;
    int removedBeforeEnd = 0;
    int writeIndex = 0;
    for (int readIndex = 0; text[readIndex]; readIndex++) {
        if (text[readIndex] == L'\r' || text[readIndex] == L'\n') {
            changed = TRUE;
            if ((DWORD)readIndex < selectionStart) removedBeforeStart++;
            if ((DWORD)readIndex < selectionEnd) removedBeforeEnd++;
            continue;
        }
        text[writeIndex++] = text[readIndex];
    }
    if (changed) {
        text[writeIndex] = L'\0';
        SetWindowTextW(edit, text);
        SendMessageW(edit, EM_SETSEL,
            selectionStart - (DWORD)removedBeforeStart,
            selectionEnd - (DWORD)removedBeforeEnd);
    }
}

static LRESULT CALLBACK CenteredEditProc(HWND edit, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
    WNDPROC original = GetOriginalEditProc(edit);
    if (!original) return DefWindowProcW(edit, message, wParam, lParam);
    if (message == WM_CHAR && (wParam == L'\r' || wParam == L'\n')) return 0;

    LRESULT result = CallWindowProcW(original, edit, message, wParam, lParam);
    if (message == WM_PASTE) RemoveLineBreaksFromEdit(edit);
    if (message == WM_SIZE || message == WM_SETFONT) CenterEditText(edit);
    if (message == WM_NCDESTROY) RemovePropW(edit, CENTERED_EDIT_PROC_PROP);
    return result;
}

void EnableVerticallyCenteredEdit(HWND edit) {
    if (!edit || GetPropW(edit, CENTERED_EDIT_PROC_PROP)) return;
    WNDPROC centered = CenteredEditProc;
    LONG_PTR centeredValue = 0;
    memcpy(&centeredValue, &centered, sizeof(centered));
    LONG_PTR original = SetWindowLongPtrW(edit, GWLP_WNDPROC, centeredValue);
    if (original) {
        SetPropW(edit, CENTERED_EDIT_PROC_PROP, (HANDLE)original);
        CenterEditText(edit);
    }
}

void SetComboBoxClosedHeight(HWND combo, int targetHeight) {
    if (!combo || targetHeight <= 0) return;
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, DPI(28));
    RECT rect;
    if (!GetWindowRect(combo, &rect)) return;
    int currentHeight = rect.bottom - rect.top;
    int itemHeight = (int)SendMessageW(combo, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
    int chromeHeight = currentHeight - itemHeight;
    if (chromeHeight < 1 || chromeHeight > DPI(12)) chromeHeight = DPI(4);
    int targetItemHeight = targetHeight - chromeHeight;
    if (targetItemHeight > 0) {
        SendMessageW(combo, CB_SETITEMHEIGHT, (WPARAM)-1, targetItemHeight);
    }
}

static WNDPROC GetOriginalComboProc(HWND combo) {
    LONG_PTR value = (LONG_PTR)GetPropW(combo, MODERN_COMBO_PROC_PROP);
    WNDPROC proc = NULL;
    memcpy(&proc, &value, sizeof(proc));
    return proc;
}

static WNDPROC GetOriginalComboEditProc(HWND edit) {
    LONG_PTR value = (LONG_PTR)GetPropW(edit, MODERN_COMBO_EDIT_PROC_PROP);
    WNDPROC proc = NULL;
    memcpy(&proc, &value, sizeof(proc));
    return proc;
}

static void PaintComboEditEdges(HWND edit) {
    RECT client;
    if (!edit || !GetClientRect(edit, &client)) return;
    HDC dc = GetDC(edit);
    if (!dc) return;
    const UI_PALETTE* palette = GetUiPalette(isDarkMode);
    HBRUSH brush = CreateSolidBrush(palette->surface);
    int edge = DPI(1) + 1;
    RECT strip = {client.left, client.top, client.right, client.top + edge};
    FillRect(dc, &strip, brush);
    strip.top = client.bottom - edge;
    strip.bottom = client.bottom;
    FillRect(dc, &strip, brush);
    strip.left = client.left;
    strip.top = client.top;
    strip.right = client.left + edge;
    FillRect(dc, &strip, brush);
    DeleteObject(brush);
    ReleaseDC(edit, dc);
}

static LRESULT CALLBACK ModernComboEditProc(HWND edit, UINT message,
                                            WPARAM wParam, LPARAM lParam) {
    WNDPROC original = GetOriginalComboEditProc(edit);
    if (!original) return DefWindowProcW(edit, message, wParam, lParam);
    LRESULT result = CallWindowProcW(original, edit, message, wParam, lParam);
    if (message == WM_PAINT || message == WM_NCPAINT ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        PaintComboEditEdges(edit);
    }
    if (message == WM_NCDESTROY) {
        RemovePropW(edit, MODERN_COMBO_EDIT_PROC_PROP);
    }
    return result;
}

static void EnableModernComboEdit(HWND edit) {
    if (!edit || GetPropW(edit, MODERN_COMBO_EDIT_PROC_PROP)) return;
    WNDPROC modern = ModernComboEditProc;
    LONG_PTR modernValue = 0;
    memcpy(&modernValue, &modern, sizeof(modern));
    LONG_PTR original = SetWindowLongPtrW(edit, GWLP_WNDPROC, modernValue);
    if (original) {
        SetPropW(edit, MODERN_COMBO_EDIT_PROC_PROP, (HANDLE)original);
        InvalidateRect(edit, NULL, TRUE);
    }
}

static void PaintComboArrow(HWND combo) {
    RECT client;
    if (!combo || !GetClientRect(combo, &client) || client.right <= 0) return;
    HDC dc = GetDC(combo);
    if (!dc) return;

    const UI_PALETTE* palette = GetUiPalette(isDarkMode);
    BOOL dropped = (BOOL)SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0);
    int arrowWidth = DPI(20);
    RECT button = client;
    button.left = button.right - arrowWidth;
    button.top += 1;
    button.right -= 1;
    button.bottom -= 1;
    HBRUSH background = CreateSolidBrush(
        dropped ? palette->surfacePressed : palette->surface);
    FillRect(dc, &button, background);
    DeleteObject(background);

    HPEN divider = CreatePen(PS_SOLID, 1, palette->border);
    HGDIOBJ oldPen = SelectObject(dc, divider);
    MoveToEx(dc, button.left, button.top, NULL);
    LineTo(dc, button.left, button.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(divider);

    int centerX = button.left + (button.right - button.left) / 2;
    int centerY = button.top + (button.bottom - button.top) / 2;
    int half = DPI(3);
    HPEN chevron = CreatePen(PS_SOLID, 1, palette->textSecondary);
    oldPen = SelectObject(dc, chevron);
    MoveToEx(dc, centerX - half, centerY - DPI(1), NULL);
    LineTo(dc, centerX, centerY + DPI(2));
    LineTo(dc, centerX + half + 1, centerY - DPI(1));
    SelectObject(dc, oldPen);
    DeleteObject(chevron);

    HPEN outline = CreatePen(PS_SOLID, 1, palette->border);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    oldPen = SelectObject(dc, outline);
    RoundRect(dc, client.left, client.top, client.right - 1,
              client.bottom - 1, DPI(8), DPI(8));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(outline);
    ReleaseDC(combo, dc);
}

static LRESULT CALLBACK ModernComboProc(HWND combo, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
    WNDPROC original = GetOriginalComboProc(combo);
    if (!original) return DefWindowProcW(combo, message, wParam, lParam);
    LRESULT result = CallWindowProcW(original, combo, message, wParam, lParam);
    if (message == WM_SIZE) SetRoundedControlRegion(combo, DPI(4));
    if (message == WM_PAINT || message == WM_NCPAINT ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS ||
        message == WM_ENABLE || message == CB_SHOWDROPDOWN) {
        PaintComboArrow(combo);
    }
    if (message == WM_NCDESTROY) RemovePropW(combo, MODERN_COMBO_PROC_PROP);
    return result;
}

void EnableModernComboBox(HWND combo) {
    if (!combo || GetPropW(combo, MODERN_COMBO_PROC_PROP)) return;
    WNDPROC modern = ModernComboProc;
    LONG_PTR modernValue = 0;
    memcpy(&modernValue, &modern, sizeof(modern));
    LONG_PTR original = SetWindowLongPtrW(combo, GWLP_WNDPROC, modernValue);
    if (original) {
        SetPropW(combo, MODERN_COMBO_PROC_PROP, (HANDLE)original);
        COMBOBOXINFO info = {0};
        info.cbSize = sizeof(info);
        if (GetComboBoxInfo(combo, &info) && info.hwndItem &&
            info.hwndItem != combo) {
            LONG_PTR style = GetWindowLongPtrW(info.hwndItem, GWL_STYLE);
            LONG_PTR exStyle = GetWindowLongPtrW(info.hwndItem, GWL_EXSTYLE);
            SetWindowLongPtrW(info.hwndItem, GWL_STYLE, style & ~WS_BORDER);
            SetWindowLongPtrW(info.hwndItem, GWL_EXSTYLE,
                              exStyle & ~WS_EX_CLIENTEDGE);
            MySetWindowTheme(info.hwndItem, L"", L"");
            EnableModernComboEdit(info.hwndItem);
            SetWindowPos(info.hwndItem, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        SetRoundedControlRegion(combo, DPI(4));
        InvalidateRect(combo, NULL, TRUE);
    }
}

static void ClearComboEditSelection(HWND combo) {
    COMBOBOXINFO info = {0};
    info.cbSize = sizeof(info);
    if (!combo || !GetComboBoxInfo(combo, &info) || !info.hwndItem ||
        info.hwndItem == combo) return;
    int textLength = GetWindowTextLengthW(info.hwndItem);
    SendMessageW(info.hwndItem, EM_SETSEL, textLength, textLength);
}

// 删除指定索引的host控件（不能删除第一个初始控件）
void RemoveHostControl(int index) {
    if (index < 1 || index >= hHostControlCount) return; // 不能删除第一个控件（index 0），只允许删除后续添加的控件

    // 销毁控件
    if (hHostControls[index * 2] != NULL) {      // 下拉框
        DestroyWindow(hHostControls[index * 2]);
    }
    if (hHostControls[index * 2 + 1] != NULL) {  // 删除按钮
        DestroyWindow(hHostControls[index * 2 + 1]);
    }

    // 将后面的控件前移，覆盖要删除的控件
    for (int i = index; i < hHostControlCount - 1; i++) {
        hHostControls[i * 2] = hHostControls[(i + 1) * 2];
        hHostControls[i * 2 + 1] = hHostControls[(i + 1) * 2 + 1];
    }

    // 清空最后的控件位置
    hHostControls[(hHostControlCount - 1) * 2] = NULL;
    hHostControls[(hHostControlCount - 1) * 2 + 1] = NULL;
    hHostControlCount--;

    // 重新定位下方的控件
    RepositionLowerControls();
}

void LayoutMainWindow(BOOL resizeToContent) {
    if (!hMainWnd || g_isLayingOut) return;
    g_isLayingOut = TRUE;

    const int margin = DPI(24);
    const int ctrlH = DPI(UI_CONTROL_HEIGHT);
    const int smallButtonW = DPI(32);
    const int headerTop = DPI(16);
    const int contentTop = DPI(78);
    const int leftWidth = DPI(216);
    const int columnGap = DPI(24);
    const int rightX = margin + leftWidth + columnGap;
    const int hostRow = DPI(44);
    const int statusH = DPI(48);

    RECT client;
    GetClientRect(hMainWnd, &client);
    int clientWidth = client.right;
    int rightWidth = clientWidth - rightX - margin;
    if (rightWidth < DPI(360)) rightWidth = DPI(360);

    int detailsTitleY = contentTop;
    int fieldLabelY = detailsTitleY + DPI(26);
    int fieldY = fieldLabelY + DPI(20);
    int fieldGap = DPI(16);
    int fieldWidth = (rightWidth - fieldGap) / 2;
    int sshTitleY = fieldY + ctrlH + DPI(24);
    int keyLabelY = sshTitleY + DPI(26);
    int keyButtonY = keyLabelY - DPI(7);
    int keyY = keyButtonY + ctrlH + DPI(8);
    int hostLabelY = keyY + ctrlH + DPI(20);
    int hostY = hostLabelY + DPI(20);
    int actionsY = hostY + hostRow * hHostControlCount + DPI(8);
    int requiredClientHeight = actionsY + ctrlH + DPI(16) + statusH + margin;

    if (resizeToContent && client.bottom < requiredClientHeight) {
        RECT windowRect;
        GetWindowRect(hMainWnd, &windowRect);
        int nonClient = (windowRect.bottom - windowRect.top) - client.bottom;
        SetWindowPos(hMainWnd, NULL, 0, 0, windowRect.right - windowRect.left,
                     requiredClientHeight + nonClient,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetClientRect(hMainWnd, &client);
        clientWidth = client.right;
        rightWidth = clientWidth - rightX - margin;
        fieldWidth = (rightWidth - fieldGap) / 2;
    }

    int statusY = client.bottom - margin - statusH;
    if (statusY < actionsY + ctrlH + DPI(16)) statusY = actionsY + ctrlH + DPI(16);
    int listTop = contentTop + DPI(26);
    int listBottom = statusY - DPI(16);

    SetWindowPos(hHeaderTitle, NULL, margin, headerTop, clientWidth - DPI(180), DPI(26), SWP_NOZORDER);
    SetWindowPos(hHeaderSubtitle, NULL, margin, headerTop + DPI(28), clientWidth - DPI(180), DPI(18), SWP_NOZORDER);
    SetWindowPos(hBtnTheme, NULL, clientWidth - margin - DPI(64), headerTop + DPI(2), DPI(64), ctrlH, SWP_NOZORDER);

    SetWindowPos(hSectionAccounts, NULL, margin, contentTop, leftWidth - DPI(80), DPI(22), SWP_NOZORDER);
    SetWindowPos(hBtnNew, NULL, margin + leftWidth - DPI(72), contentTop - DPI(5), DPI(72), ctrlH, SWP_NOZORDER);
    SetWindowPos(hList, NULL, margin, listTop, leftWidth, listBottom - listTop, SWP_NOZORDER);
    SetRoundedControlRegion(hList, DPI(8));

    SetWindowPos(hSectionDetails, NULL, rightX, detailsTitleY, rightWidth, DPI(22), SWP_NOZORDER);
    SetWindowPos(hLblName, NULL, rightX, fieldLabelY, fieldWidth, DPI(18), SWP_NOZORDER);
    SetWindowPos(hName, NULL, rightX, fieldY, fieldWidth, ctrlH, SWP_NOZORDER);
    SetWindowPos(hLblEmail, NULL, rightX + fieldWidth + fieldGap, fieldLabelY, fieldWidth, DPI(18), SWP_NOZORDER);
    SetWindowPos(hEmail, NULL, rightX + fieldWidth + fieldGap, fieldY, fieldWidth, ctrlH, SWP_NOZORDER);

    SetWindowPos(hSectionSSH, NULL, rightX, sshTitleY, rightWidth, DPI(22), SWP_NOZORDER);
    SetWindowPos(hLblSSH, NULL, rightX, keyLabelY, rightWidth - DPI(100), DPI(18), SWP_NOZORDER);
    SetWindowPos(hBtnGenerate, NULL, rightX + rightWidth - DPI(96), keyButtonY, DPI(96), ctrlH, SWP_NOZORDER);
    SetWindowPos(hSSH, NULL, rightX, keyY, rightWidth - smallButtonW - DPI(8), DPI(180), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hMainWnd, ID_BTN_BROWSE), NULL,
                 rightX + rightWidth - smallButtonW, keyY, smallButtonW, ctrlH, SWP_NOZORDER);

    SetWindowPos(hLblHost, NULL, rightX, hostLabelY, DPI(92), DPI(18), SWP_NOZORDER);
    SetWindowPos(hLblHostHint, NULL, rightX + DPI(92), hostLabelY, rightWidth - DPI(92), DPI(18), SWP_NOZORDER);
    for (int i = 0; i < hHostControlCount; i++) {
        int rowY = hostY + hostRow * i;
        SetWindowPos(hHostControls[i * 2], NULL, rightX, rowY,
                     rightWidth - smallButtonW - DPI(8), DPI(180), SWP_NOZORDER);
        HWND sideButton = i == 0 ? hBtnAddHost : hHostControls[i * 2 + 1];
        if (sideButton) SetWindowPos(sideButton, NULL, rightX + rightWidth - smallButtonW,
                                     rowY, smallButtonW, ctrlH, SWP_NOZORDER);
    }

    const int switchW = DPI(152);
    const int saveW = DPI(96);
    const int cancelW = DPI(72);
    SetWindowPos(hBtnDelete, NULL, rightX, actionsY, DPI(72), ctrlH, SWP_NOZORDER);
    SetWindowPos(hBtnSwitch, NULL, rightX + rightWidth - switchW, actionsY, switchW, ctrlH, SWP_NOZORDER);
    SetWindowPos(hBtnSave, NULL, rightX + rightWidth - switchW - DPI(8) - saveW,
                 actionsY, saveW, ctrlH, SWP_NOZORDER);
    SetWindowPos(hBtnCancel, NULL,
                 rightX + rightWidth - switchW - DPI(16) - saveW - cancelW,
                 actionsY, cancelW, ctrlH, SWP_NOZORDER);

    int trayButtonW = DPI(168);
    SetWindowPos(hStatus, NULL, margin, statusY,
                 clientWidth - margin * 2 - trayButtonW - DPI(10), statusH, SWP_NOZORDER);
    SetWindowPos(hBtnTaskbar, NULL, clientWidth - margin - trayButtonW,
                 statusY, trayButtonW, statusH, SWP_NOZORDER);

    RedrawWindow(hMainWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    g_isLayingOut = FALSE;
}

void RepositionLowerControls() {
    if (g_deferHostLayout) return;
    LayoutMainWindow(TRUE);
}

// 添加一个host控件（下拉框+删除按钮）
void AddHostControl(const wchar_t* initialHost) {
    if (hHostControlCount >= 10) return;  // 限制最大数量，包括初始的1个
    int index = hHostControlCount;
    HWND hCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
        CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
        0, 0, DPI(200), DPI(180), hMainWnd, (HMENU)(INT_PTR)(ID_HOST_COMBO_PREFIX + index), NULL, NULL);
    // 添加常见的Git服务
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"github.com");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"gitlab.com");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"gitee.com");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"bitbucket.org");
    if (initialHost && wcslen(initialHost) > 0) {
        SetWindowTextW(hCombo, initialHost);
    }

    HWND hDeleteBtn = CreateWindowW(L"BUTTON", L"–", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 
        0, 0, DPI(32), DPI(UI_CONTROL_HEIGHT), hMainWnd, (HMENU)(INT_PTR)ID_HOST_DELETE_PREFIX, NULL, NULL);

    hHostControls[index * 2] = hCombo;
    hHostControls[index * 2 + 1] = hDeleteBtn;
    hHostControlCount++;

    SendMessageW(hCombo, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
    SendMessageW(hDeleteBtn, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
    EnableModernComboBox(hCombo);
    MySetWindowTheme(hCombo, isDarkMode ? L"DarkMode_Explorer" : NULL, NULL);

    RepositionLowerControls();
    SetComboBoxClosedHeight(hCombo, DPI(UI_CONTROL_HEIGHT));
}

// 清空所有host控件，保留第一个初始控件
void ClearHostControls() {
    // 销毁从第二个控件开始的所有控件
    for (int i = hHostControlCount - 1; i >= 1; i--) {  // 从最后一个开始删除，跳过第一个
        if (hHostControls[i * 2] != NULL) {      // 下拉框
            DestroyWindow(hHostControls[i * 2]);
        }
        if (hHostControls[i * 2 + 1] != NULL) {  // 删除按钮
            DestroyWindow(hHostControls[i * 2 + 1]);
        }
        hHostControls[i * 2] = NULL;
        hHostControls[i * 2 + 1] = NULL;
    }
    hHostControlCount = 1; // 重置为只有第一个控件

    // 清空第一个控件的内容
    if (hHostControls[0] != NULL) {
        SetWindowTextW(hHostControls[0], L"");
    }

    // 重新定位下方的控件
    RepositionLowerControls();
}

// 从控件中获取hosts并更新到账户对象
BOOL UpdateAccountHosts(Account* acc) {
    acc->host_count = 0;
    for (int i = 0; i < hHostControlCount && i < 10; i++) {
        if (hHostControls[i * 2] != NULL) {  // 下拉框存在
            wchar_t wHost[HOST_LEN];
            GetWindowTextW(hHostControls[i * 2], wHost, HOST_LEN);
            if (wcslen(wHost) > 0) {
                if (!WToU8Buffer(wHost, acc->host_list[acc->host_count], HOST_LEN)) {
                    ShowMessage(hMainWnd, L"SSH Host 太长或包含无效字符", L"输入错误", MB_OK);
                    return FALSE;
                }
                acc->host_count++;
            }
        }
    }
    if (!ValidateSSHHostList((const char (*)[HOST_LEN])acc->host_list, acc->host_count)) {
        wchar_t message[512];
        U8ToWBuffer(GetLogicErrorMessage(), message, 512);
        ShowMessage(hMainWnd, message[0] ? message : L"SSH Host 格式无效", L"输入错误", MB_OK);
        return FALSE;
    }
    return TRUE;
}

// 用账户中的hosts填充控件
void PopulateHostControls(Account* acc) {
    int targetControlCount = acc && acc->host_count > 0 ? acc->host_count : 1;
    if (targetControlCount > 10) targetControlCount = 10;
    BOOL structureChanged = targetControlCount != hHostControlCount;

    if (structureChanged) {
        g_deferHostLayout = TRUE;
        SendMessageW(hMainWnd, WM_SETREDRAW, FALSE, 0);
        while (hHostControlCount > targetControlCount) {
            RemoveHostControl(hHostControlCount - 1);
        }
        while (hHostControlCount < targetControlCount) {
            AddHostControl(L"");
        }
    }

    for (int i = 0; i < hHostControlCount; i++) {
        LPCWSTR host = acc && i < acc->host_count ? U8ToW(acc->host_list[i]) : L"";
        SetWindowTextW(hHostControls[i * 2], host);
    }

    if (structureChanged) {
        g_deferHostLayout = FALSE;
        LayoutMainWindow(TRUE);
        SendMessageW(hMainWnd, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hMainWnd, NULL, TRUE);
    }
}

void OnSSHKeyChanged(const wchar_t* wSSHPath) {
    if (wSSHPath && wcslen(wSSHPath) > 0 && hHostControls[0]) {
        char u8Path[PATH_LEN];
        if (!WToU8Buffer(wSSHPath, u8Path, PATH_LEN)) return;
        char foundHost[256] = "";
        
        if (GetHostFromSSHConfig(u8Path, foundHost, sizeof(foundHost)) && strlen(foundHost) > 0) {
            SetWindowTextW(hHostControls[0], U8ToW(foundHost));
        }
    } else {
        if (hHostControls[0]) SetWindowTextW(hHostControls[0], L"");
    }
}

static BOOL IsAccountCurrent(const Account* account) {
    if (!account) return FALSE;
    if (g_globalName[0] && g_globalEmail[0]) {
        return strcmp(account->name, g_globalName) == 0 &&
               strcmp(account->email, g_globalEmail) == 0;
    }
    return strcmp(account->id, config.active_id) == 0;
}

// 刷新列表显示
void RefreshList() {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < config.account_count; i++) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s <%s>", config.accounts[i].name, config.accounts[i].email);
        
        if (IsAccountCurrent(&config.accounts[i])) {
            char activeBuffer[600];
            snprintf(activeBuffer, sizeof(activeBuffer), "[当前] %s", buffer);
            int idx = SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)U8ToW(activeBuffer));
            SendMessageW(hList, LB_SETITEMDATA, idx, i);
        } else {
            int idx = SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)U8ToW(buffer));
            SendMessageW(hList, LB_SETITEMDATA, idx, i);
        }
    }
}

// 加载 SSH Key 到 ComboBox
void LoadSSHKeysToCombo() {
    SendMessageW(hSSH, CB_RESETCONTENT, 0, 0);
    char keys[20][PATH_LEN];
    int count = GetSSHKeys(keys, 20);
    for (int i = 0; i < count; i++) {
        SendMessageW(hSSH, CB_ADDSTRING, 0, (LPARAM)U8ToW(keys[i]));
    }
}

// 清空表单
void ClearForm() {
    SetWindowTextW(hName, L"");
    SetWindowTextW(hEmail, L"");
    SetWindowTextW(hSSH, L"");
    ClearHostControls();  // 清空hosts控件
    currentEditID[0] = 0;
    SetWindowTextW(hBtnSave, L"添加账户");
    ShowWindow(hBtnCancel, SW_HIDE);
    EnableWindow(hBtnDelete, FALSE);
    EnableWindow(hBtnSwitch, FALSE);
    SendMessageW(hList, LB_SETCURSEL, -1, 0);
}

void UpdateIdentitySurfaces(void) {
    char name[NAME_LEN] = "";
    char email[EMAIL_LEN] = "";
    GetGlobalConfig(name, email);
    BOOL changed = strcmp(name, g_globalName) != 0 || strcmp(email, g_globalEmail) != 0;
    strncpy(g_globalName, name, sizeof(g_globalName) - 1);
    strncpy(g_globalEmail, email, sizeof(g_globalEmail) - 1);

    wchar_t wideName[NAME_LEN] = L"";
    wchar_t wideEmail[EMAIL_LEN] = L"";
    U8ToWBuffer(name, wideName, NAME_LEN);
    U8ToWBuffer(email, wideEmail, EMAIL_LEN);
    if (wideName[0] && wideEmail[0]) {
        _snwprintf(g_identityTitle, 511, L"%ls <%ls>", wideName, wideEmail);
        g_identityTitle[511] = L'\0';
        wcscpy(g_identityDetail, L"当前 Git 全局身份 · 提交前请确认");
        wchar_t windowTitle[512];
        _snwprintf(windowTitle, 511, L"Git Account Manager · %ls", wideName);
        windowTitle[511] = L'\0';
        SetWindowTextW(hMainWnd, windowTitle);
    } else {
        wcscpy(g_identityTitle, L"尚未配置 Git 全局身份");
        wcscpy(g_identityDetail, L"请选择账户并完成切换");
        SetWindowTextW(hMainWnd, L"Git Account Manager");
    }
    TaskbarIdentityUpdate(&g_taskbarIdentity, wideName);
    TrayIdentityUpdate(&g_trayIdentity, wideName, wideEmail);
    if (hStatus) InvalidateRect(hStatus, NULL, TRUE);
    if (changed && hList) InvalidateRect(hList, NULL, TRUE);
}

// 更新状态栏及任务栏身份提示
void UpdateStatus() {
    UpdateIdentitySurfaces();
}

// 设置 DWM 沉浸式暗黑模式标题栏
void SetTitleBarTheme(HWND hwnd, BOOL dark) {
    BOOL value = dark;
    HRESULT result = DwmSetWindowAttribute(
        hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    if (FAILED(result)) {
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD,
                              &value, sizeof(value));
    }
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    BOOL isActive = GetForegroundWindow() == hwnd || GetActiveWindow() == hwnd;
    SendMessageW(hwnd, WM_NCACTIVATE, FALSE, 0);
    SendMessageW(hwnd, WM_NCACTIVATE, isActive, 0);
    RedrawWindow(hwnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
    DwmFlush();
}

// --- 自定义消息框 (居中且支持暗黑模式) ---
#define ID_BTN_MSG_OK 201
#define ID_BTN_MSG_YES IDYES
#define ID_BTN_MSG_NO IDNO

LRESULT CALLBACK MsgBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 获取参数
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        LPCWSTR text = (LPCWSTR)pCreate->lpCreateParams;
        
        // 内容文本 (DPI 缩放)
        HWND hStatic = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER, 
            DPI(20), DPI(30), DPI(310), DPI(60), hwnd, NULL, NULL, NULL);
        SendMessageW(hStatic, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_MSG_OK || id == IDOK) {
            int* pRes = (int*)GetProp(hwnd, L"ResultPtr");
            if (pRes) *pRes = IDOK;
            DestroyWindow(hwnd);
        } else if (id == IDYES) {
            int* pRes = (int*)GetProp(hwnd, L"ResultPtr");
            if (pRes) *pRes = IDYES;
            DestroyWindow(hwnd);
        } else if (id == IDNO) {
            int* pRes = (int*)GetProp(hwnd, L"ResultPtr");
            if (pRes) *pRes = IDNO;
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        if (isDarkMode) {
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(32, 32, 32));
            return (LRESULT)hBrushDark;
        }
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawOwnerDrawButton(pDIS, isDarkMode, hBrushDark, hBrushLight);
            return TRUE;
        }
        break;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, isDarkMode ? hBrushDark : hBrushLight);
        return 1;
    }
    case WM_CLOSE:
        {
            int* pRes = (int*)GetProp(hwnd, L"ResultPtr");
            if (pRes && *pRes == 0) *pRes = IDCANCEL;
        }
        DestroyWindow(hwnd);
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ShowMessage(HWND owner, LPCWSTR text, LPCWSTR title, UINT type) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = MsgBoxProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GitManagerMsgBox";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    int width = DPI(350);
    int height = DPI(180);
    
    // 计算居中位置
    RECT rcOwner;
    GetWindowRect(owner, &rcOwner);
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - width) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - height) / 2;

    HWND hMsgBox = CreateWindowW(L"GitManagerMsgBox", title, WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_BORDER,
        x, y, width, height, owner, NULL, GetModuleHandle(NULL), (LPVOID)text);

    int result = 0;
    SetProp(hMsgBox, L"ResultPtr", &result);
    SetProp(hMsgBox, L"MsgType", (HANDLE)(UINT_PTR)type);

    // 创建按钮 (DPI 缩放)
    if (type == MB_YESNO) {
        HWND hBtnYes = CreateWindowW(L"BUTTON", L"是(Y)", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW, 
            DPI(60), DPI(100), DPI(100), DPI(26), hMsgBox, (HMENU)IDYES, NULL, NULL);
        HWND hBtnNo = CreateWindowW(L"BUTTON", L"否(N)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 
            DPI(190), DPI(100), DPI(100), DPI(26), hMsgBox, (HMENU)IDNO, NULL, NULL);
        SendMessageW(hBtnYes, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
        SendMessageW(hBtnNo, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
    } else {
        HWND hBtn = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW, 
            DPI(125), DPI(100), DPI(100), DPI(26), hMsgBox, (HMENU)ID_BTN_MSG_OK, NULL, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
    }

    // 设置主题
    SetTitleBarTheme(hMsgBox, isDarkMode);
    
    // 模态循环
    EnableWindow(owner, FALSE);
    
    MSG msg;
    while (IsWindow(hMsgBox) && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE) {
                result = IDCANCEL;
                DestroyWindow(hMsgBox);
                break;
            }
            UINT msgType = (UINT)(UINT_PTR)GetProp(hMsgBox, L"MsgType");
            if (msg.wParam == VK_RETURN) {
                result = (msgType == MB_YESNO) ? IDYES : IDOK;
                DestroyWindow(hMsgBox);
                break;
            }
            if (msgType == MB_YESNO) {
                if (msg.wParam == 'Y') {
                    result = IDYES;
                    DestroyWindow(hMsgBox);
                    break;
                } else if (msg.wParam == 'N') {
                    result = IDNO;
                    DestroyWindow(hMsgBox);
                    break;
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return result;
}

// 设置子控件字体回调
BOOL CALLBACK SetChildFont(HWND hwndChild, LPARAM lParam) {
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

static BOOL CALLBACK InvalidateChild(HWND child, LPARAM unused) {
    (void)unused;
    InvalidateRect(child, NULL, TRUE);
    return TRUE;
}

static void RestoreMainWindow(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    else ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

static void UpdateTaskbarButtonLabel(void) {
    if (!hBtnTaskbar) return;
    LPCWSTR text = L"任务栏提醒 · 关";
    if (config.show_identity_badge) {
        text = config.show_taskbar_text
            ? L"任务栏提醒 · 文字" : L"任务栏提醒 · 图标";
    }
    SetWindowTextW(hBtnTaskbar, text);
    InvalidateRect(hBtnTaskbar, NULL, TRUE);
}

static void ApplyTaskbarReminderState(HWND hwnd) {
    BOOL showText = config.show_identity_badge && config.show_taskbar_text;
    TrayIdentitySetEnabled(&g_trayIdentity, config.show_identity_badge);
    TaskbarIdentitySetEnabled(&g_taskbarIdentity, showText);
    if (showText) SetTimer(hwnd, ID_TIMER_TASKBAR, 500, NULL);
    else KillTimer(hwnd, ID_TIMER_TASKBAR);
    UpdateTaskbarButtonLabel();
}

// 应用主题颜色
void ApplyTheme(HWND hwnd) {
    // 设置标题栏主题
    SetTitleBarTheme(hwnd, isDarkMode);

    if (hBtnTheme) {
        SetWindowTextW(hBtnTheme, isDarkMode ? L"浅色" : L"深色");
    }
    UpdateTaskbarButtonLabel();

    // ListBox 和 ComboBox 使用 DarkMode_Explorer 主题（获得暗色滚动条和下拉列表）
    LPCWSTR theme = isDarkMode ? L"DarkMode_Explorer" : NULL;
    MySetWindowTheme(hList, theme, NULL);
    MySetWindowTheme(hSSH, theme, NULL);
    for (int i = 0; i < hHostControlCount; i++) {
        if (hHostControls[i * 2]) MySetWindowTheme(hHostControls[i * 2], theme, NULL);
    }
    // 切换按钮样式 (OwnerDraw) - 始终启用 OwnerDraw 以保持圆角风格一致
    int btnIds[] = {ID_BTN_BROWSE, ID_BTN_SAVE, ID_BTN_DELETE, ID_BTN_SWITCH,
                    ID_BTN_CANCEL, ID_BTN_THEME, ID_BTN_GENERATE, ID_BTN_TASKBAR,
                    ID_BTN_NEW, ID_BTN_ADD_HOST};
    for (int i = 0; i < (int)(sizeof(btnIds) / sizeof(btnIds[0])); i++) {
        HWND hBtn = GetDlgItem(hwnd, btnIds[i]);
        if (hBtn) {
            LONG_PTR style = GetWindowLongPtr(hBtn, GWL_STYLE);
            style |= BS_OWNERDRAW;
            SetWindowLongPtr(hBtn, GWL_STYLE, style);
            SetWindowPos(hBtn, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }

    // 刷新所有控件
    InvalidateRect(hwnd, NULL, TRUE);
    EnumChildWindows(hwnd, (WNDENUMPROC)SetChildFont, (LPARAM)hGlobalFont);
    if (hHeaderTitle) SendMessageW(hHeaderTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
    if (hSectionAccounts) SendMessageW(hSectionAccounts, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
    if (hSectionDetails) SendMessageW(hSectionDetails, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
    if (hSectionSSH) SendMessageW(hSectionSSH, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
    if (hHeaderSubtitle) SendMessageW(hHeaderSubtitle, WM_SETFONT, (WPARAM)hHintFont, TRUE);
    if (hLblHostHint) SendMessageW(hLblHostHint, WM_SETFONT, (WPARAM)hHintFont, TRUE);
    EnumChildWindows(hwnd, InvalidateChild, 0);
    SetComboBoxClosedHeight(hSSH, DPI(UI_CONTROL_HEIGHT));
    for (int i = 0; i < hHostControlCount; i++) {
        SetComboBoxClosedHeight(hHostControls[i * 2], DPI(UI_CONTROL_HEIGHT));
    }
    UpdateIdentitySurfaces();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 设置全局主窗口句柄
    if (msg == WM_CREATE) {
        hMainWnd = hwnd;
    }
    if ((g_taskbarIdentity.taskbar_created_message &&
         msg == g_taskbarIdentity.taskbar_created_message) ||
        (g_trayIdentity.taskbar_created_message &&
         msg == g_trayIdentity.taskbar_created_message)) {
        TaskbarIdentityRestoreAfterExplorerRestart(&g_taskbarIdentity);
        TrayIdentityRestoreAfterExplorerRestart(&g_trayIdentity);
        return 0;
    }
    
    switch (msg) {
    case WM_CREATE: {
        LoadConfig(&config);
        g_configLoadFailed = GetLogicErrorMessage()[0] != 0;
        int accountCountBeforeImport = config.account_count;
        if (!g_configLoadFailed) AutoImportGlobalIdentity(&config);
        if (accountCountBeforeImport == 0 && config.account_count > 0) SaveConfig(&config);
        isDarkMode = config.dark_mode;

        hGlobalFont = CreateUiFont(10, FW_NORMAL);
        hHintFont = CreateUiFont(9, FW_NORMAL);
        hTitleFont = CreateUiFont(15, FW_SEMIBOLD);
        hSectionFont = CreateUiFont(11, FW_SEMIBOLD);
        const UI_PALETTE* light = GetUiPalette(FALSE);
        const UI_PALETTE* dark = GetUiPalette(TRUE);
        hBrushDark = CreateSolidBrush(dark->windowBackground);
        hBrushControlDark = CreateSolidBrush(dark->surface);
        hBrushLight = CreateSolidBrush(light->windowBackground);
        hBrushControlLight = CreateSolidBrush(light->surface);

        hHeaderTitle = CreateWindowW(L"STATIC", L"Git 账户管理", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)ID_HEADER_TITLE, NULL, NULL);
        hHeaderSubtitle = CreateWindowW(L"STATIC", L"Git 身份与 SSH 密钥", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)ID_HEADER_SUBTITLE, NULL, NULL);
        hBtnTheme = CreateWindowW(L"BUTTON", L"深色", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_THEME, NULL, NULL);

        hSectionAccounts = CreateWindowW(L"STATIC", L"账户", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)ID_SECTION_ACCOUNTS, NULL, NULL);
        hBtnNew = CreateWindowW(L"BUTTON", L"+ 新建", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_NEW, NULL, NULL);
        hList = CreateWindowW(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE |
            LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_LIST, NULL, NULL);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, DPI(UI_ACCOUNT_ITEM_HEIGHT));

        hSectionDetails = CreateWindowW(L"STATIC", L"账户信息", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)ID_SECTION_DETAILS, NULL, NULL);
        hLblName = CreateWindowW(L"STATIC", L"用户名", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        hLblEmail = CreateWindowW(L"STATIC", L"邮箱", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        hName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            ES_MULTILINE | ES_AUTOHSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);
        hEmail = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
            ES_MULTILINE | ES_AUTOHSCROLL | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_EMAIL, NULL, NULL);
        SendMessageW(hName, EM_SETLIMITTEXT, NAME_LEN - 1, 0);
        SendMessageW(hEmail, EM_SETLIMITTEXT, EMAIL_LEN - 1, 0);
        SendMessageW(hName, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(DPI(8), DPI(8)));
        SendMessageW(hEmail, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(DPI(8), DPI(8)));

        hSectionSSH = CreateWindowW(L"STATIC", L"SSH 配置", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)ID_SECTION_SSH, NULL, NULL);
        hLblSSH = CreateWindowW(L"STATIC", L"私钥文件", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        hBtnGenerate = CreateWindowW(L"BUTTON", L"生成新密钥", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_GENERATE, NULL, NULL);
        hSSH = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
            CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_COMBO_SSH, NULL, NULL);
        CreateWindowW(L"BUTTON", L"…", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
        hLblHost = CreateWindowW(L"STATIC", L"SSH Hosts", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        hLblHostHint = CreateWindowW(L"STATIC", L"域名、IP 或 host:port", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)ID_LBL_HOST_HINT, NULL, NULL);

        HWND firstHost = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
            CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_HOST_COMBO_PREFIX, NULL, NULL);
        const wchar_t* commonHosts[] = {L"github.com", L"gitlab.com", L"gitee.com", L"bitbucket.org"};
        for (int i = 0; i < 4; i++) SendMessageW(firstHost, CB_ADDSTRING, 0, (LPARAM)commonHosts[i]);
        hHostControls[0] = firstHost;
        hHostControls[1] = NULL;
        hHostControlCount = 1;
        hBtnAddHost = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_ADD_HOST, NULL, NULL);

        hBtnSave = CreateWindowW(L"BUTTON", L"添加账户", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);
        hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);
        hBtnDelete = CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);
        hBtnSwitch = CreateWindowW(L"BUTTON", L"切换到选中账户", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_SWITCH, NULL, NULL);
        EnableWindow(hBtnDelete, FALSE);
        EnableWindow(hBtnSwitch, FALSE);
        hStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 0, 0, hwnd, (HMENU)ID_STATUS, NULL, NULL);
        hBtnTaskbar = CreateWindowW(L"BUTTON", L"任务栏提醒 · 关", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_TASKBAR, NULL, NULL);

        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGlobalFont);
        SendMessageW(hHeaderTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        SendMessageW(hSectionAccounts, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
        SendMessageW(hSectionDetails, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
        SendMessageW(hSectionSSH, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
        SendMessageW(hHeaderSubtitle, WM_SETFONT, (WPARAM)hHintFont, TRUE);
        SendMessageW(hLblHostHint, WM_SETFONT, (WPARAM)hHintFont, TRUE);
        MySetWindowTheme(hName, L"", L"");
        MySetWindowTheme(hEmail, L"", L"");
        EnableVerticallyCenteredEdit(hName);
        EnableVerticallyCenteredEdit(hEmail);
        EnableModernComboBox(hSSH);
        EnableModernComboBox(firstHost);

        TaskbarIdentityInitialize(&g_taskbarIdentity);
        TrayIdentityInitialize(&g_trayIdentity, hwnd);
        LayoutMainWindow(FALSE);
        ApplyTheme(hwnd);
        RefreshList();
        LoadSSHKeysToCombo();
        UpdateIdentitySurfaces();
        ApplyTaskbarReminderState(hwnd);
        SetTimer(hwnd, ID_TIMER_IDENTITY, 3000, NULL);
        if (g_configLoadFailed) {
            ShowMessage(hwnd,
                L"账户配置文件无法解析。为避免覆盖原数据，本次没有自动导入账户；请先备份或修复 accounts.json。",
                L"账户配置损坏", MB_OK);
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        int id = GetDlgCtrlID((HWND)lParam);
        const UI_PALETTE* palette = GetUiPalette(isDarkMode);
        SetTextColor(hdc, (id == ID_LBL_HOST_HINT || id == ID_HEADER_SUBTITLE)
            ? palette->textSecondary : palette->textPrimary);
        if (msg == WM_CTLCOLOREDIT) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, palette->surface);
            return (LRESULT)(isDarkMode ? hBrushControlDark : hBrushControlLight);
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)(isDarkMode ? hBrushDark : hBrushLight);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        const UI_PALETTE* palette = GetUiPalette(isDarkMode);
        SetTextColor(hdc, palette->textPrimary);
        SetBkColor(hdc, palette->surface);
        return (LRESULT)(isDarkMode ? hBrushControlDark : hBrushControlLight);
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawOwnerDrawButton(pDIS, isDarkMode, hBrushDark, hBrushLight);
            return TRUE;
        }
        if (pDIS->CtlType == ODT_COMBOBOX) {
            DrawOwnerDrawComboItem(pDIS, isDarkMode);
            return TRUE;
        }
        if (pDIS->CtlType == ODT_LISTBOX && pDIS->CtlID == ID_LIST) {
            if (pDIS->itemID == (UINT)-1 || pDIS->itemData >= (ULONG_PTR)config.account_count) {
                FillRect(pDIS->hDC, &pDIS->rcItem,
                         isDarkMode ? hBrushControlDark : hBrushControlLight);
                return TRUE;
            }
            Account* account = &config.accounts[pDIS->itemData];
            wchar_t name[NAME_LEN] = L"";
            wchar_t email[EMAIL_LEN] = L"";
            U8ToWBuffer(account->name, name, NAME_LEN);
            U8ToWBuffer(account->email, email, EMAIL_LEN);
            DrawOwnerDrawAccountListItem(pDIS, isDarkMode, name, email,
                IsAccountCurrent(account));
            return TRUE;
        }
        if (pDIS->CtlType == ODT_STATIC && pDIS->CtlID == ID_STATUS) {
            FillRect(pDIS->hDC, &pDIS->rcItem, isDarkMode ? hBrushDark : hBrushLight);
            DrawRoundedIdentityCard(pDIS->hDC, &pDIS->rcItem, isDarkMode,
                hGlobalFont, g_identityTitle, g_identityDetail,
                g_globalName[0] && g_globalEmail[0]
                    ? UI_IDENTITY_ACTIVE : UI_IDENTITY_WARNING);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            int idx = SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                int accIdx = SendMessageW(hList, LB_GETITEMDATA, idx, 0);
                if (accIdx < 0 || accIdx >= config.account_count) break;
                Account* acc = &config.accounts[accIdx];
                SetWindowTextW(hName, U8ToW(acc->name));
                SetWindowTextW(hEmail, U8ToW(acc->email));
                
                g_bIgnoreEditChange = TRUE;
                SetWindowTextW(hSSH, U8ToW(acc->ssh_key_path));
                g_bIgnoreEditChange = FALSE;
                
                strncpy(currentEditID, acc->id, sizeof(currentEditID) - 1);
                currentEditID[sizeof(currentEditID) - 1] = 0;
                SetWindowTextW(hBtnSave, L"更新账户");
                ShowWindow(hBtnCancel, SW_SHOW);
                EnableWindow(hBtnDelete, TRUE);
                EnableWindow(hBtnSwitch, TRUE);
                PopulateHostControls(acc);  // 填充hosts控件
                ClearComboEditSelection(hSSH);
            }
        }
        else if (id == ID_HOST_DELETE_PREFIX) {
            int index = -1;
            HWND clicked = (HWND)lParam;
            for (int i = 1; i < hHostControlCount; i++) {
                if (hHostControls[i * 2 + 1] == clicked) {
                    index = i;
                    break;
                }
            }
            if (index != -1) RemoveHostControl(index);
        }
        else if (id == ID_BTN_ADD_HOST) {
            AddHostControl(L"");  // 添加一个空的host控件
        }
        else if (id == ID_BTN_NEW) {
            ClearForm();
            SetFocus(hName);
        }
        else if (id == ID_COMBO_SSH &&
                 (HIWORD(wParam) == CBN_EDITCHANGE || HIWORD(wParam) == CBN_SELCHANGE)) {
            if (g_bIgnoreEditChange) break;
            wchar_t buffer[PATH_LEN];
            GetWindowTextW(hSSH, buffer, PATH_LEN);
            OnSSHKeyChanged(buffer);
        }
        else if (id == ID_BTN_BROWSE) {
            wchar_t buffer[PATH_LEN] = L"";
            wchar_t profile[MAX_PATH] = L"";
            wchar_t sshDir[MAX_PATH] = L"";
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profile))) {
                _snwprintf(sshDir, MAX_PATH - 1, L"%ls\\.ssh", profile);
            }

            OPENFILENAMEW ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = PATH_LEN;
            ofn.lpstrFilter = L"SSH 私钥\0id_*;*.pem;*.key\0所有文件\0*.*\0";
            ofn.lpstrInitialDir = sshDir[0] ? sshDir : NULL;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                char keyPath[PATH_LEN];
                if (!WToU8Buffer(buffer, keyPath, PATH_LEN) || !ValidateSSHPrivateKey(keyPath)) {
                    wchar_t reason[512] = L"所选文件不是有效的 SSH 私钥";
                    U8ToWBuffer(GetLogicErrorMessage(), reason, 512);
                    ShowMessage(hwnd, reason, L"无法使用此文件", MB_OK);
                } else {
                    SetWindowTextW(hSSH, buffer);
                    OnSSHKeyChanged(buffer);
                }
            }
        }
        else if (id == ID_BTN_GENERATE) {
            wchar_t wEmail[EMAIL_LEN] = L"";
            wchar_t wHost[HOST_LEN] = L"github.com";
            char email[EMAIL_LEN] = "";
            char host[HOST_LEN] = "github.com";
            GetWindowTextW(hEmail, wEmail, EMAIL_LEN);
            GetWindowTextW(hHostControls[0], wHost, HOST_LEN);
            if (!WToU8Buffer(wEmail, email, EMAIL_LEN) || !WToU8Buffer(wHost, host, HOST_LEN)) {
                ShowMessage(hwnd, L"邮箱或 Host 太长", L"输入错误", MB_OK);
                break;
            }
            if (!email[0]) {
                ShowMessage(hwnd, L"请先填写邮箱，邮箱将作为公钥注释", L"需要邮箱", MB_OK);
                SetFocus(hEmail);
                break;
            }
            if (!host[0]) strcpy(host, "github.com");

            char outPath[PATH_LEN];
            if (ShowGenerateKeyDialog(hwnd, email, host, outPath)) {
                LoadSSHKeysToCombo();
                wchar_t widePath[PATH_LEN];
                if (U8ToWBuffer(outPath, widePath, PATH_LEN)) {
                    SetWindowTextW(hSSH, widePath);
                    OnSSHKeyChanged(widePath);
                }
            }
        }
        else if (id == ID_BTN_THEME) {
            if (g_configLoadFailed) {
                ShowMessage(hwnd, L"账户配置损坏时不能保存界面设置，请先修复或备份 accounts.json。", L"无法保存设置", MB_OK);
                break;
            }
            isDarkMode = !isDarkMode;
            config.dark_mode = isDarkMode;
            SaveConfig(&config);
            ApplyTheme(hwnd);
        }
        else if (id == ID_BTN_TASKBAR) {
            if (g_configLoadFailed) {
                ShowMessage(hwnd, L"账户配置损坏时不能保存任务栏设置，请先修复或备份 accounts.json。", L"无法保存设置", MB_OK);
                break;
            }
            config.show_identity_badge = !config.show_identity_badge;
            if (!SaveConfig(&config)) {
                config.show_identity_badge = !config.show_identity_badge;
                ShowMessage(hwnd, L"无法保存任务栏提醒设置", L"保存失败", MB_OK);
                break;
            }
            ApplyTaskbarReminderState(hwnd);
            if (config.show_identity_badge) {
                ShowMessage(hwnd,
                    L"任务栏图标已开启。\n悬停可查看完整账号，右键可切换白字显示。",
                    L"任务栏提醒已开启", MB_OK);
            }
        }
        else if (id == ID_BTN_CANCEL) {
            ClearForm();
        }
        else if (id == ID_BTN_SAVE) {
            if (g_configLoadFailed && ShowMessage(hwnd,
                    L"原账户配置无法解析。继续保存会用当前内容替换它，是否继续？",
                    L"确认覆盖损坏配置", MB_YESNO) != IDYES) {
                break;
            }
            wchar_t wName[NAME_LEN], wEmail[EMAIL_LEN], wSSH[PATH_LEN];
            GetWindowTextW(hName, wName, NAME_LEN);
            GetWindowTextW(hEmail, wEmail, EMAIL_LEN);
            GetWindowTextW(hSSH, wSSH, PATH_LEN);
            char nameBuf[NAME_LEN], emailBuf[EMAIL_LEN], sshBuf[PATH_LEN];
            if (!WToU8Buffer(wName, nameBuf, NAME_LEN) ||
                !WToU8Buffer(wEmail, emailBuf, EMAIL_LEN) ||
                !WToU8Buffer(wSSH, sshBuf, PATH_LEN)) {
                ShowMessage(hwnd, L"输入内容过长或包含无效字符", L"输入错误", MB_OK);
                break;
            }
            if (!nameBuf[0] || !emailBuf[0]) {
                ShowMessage(hwnd, L"用户名和邮箱不能为空", L"输入错误", MB_OK);
                break;
            }
            char* atSign = strchr(emailBuf, '@');
            if (!atSign || !strchr(atSign, '.')) {
                ShowMessage(hwnd, L"请输入有效邮箱，例如 test@example.com", L"输入错误", MB_OK);
                break;
            }
            if (sshBuf[0] && !ValidateSSHPrivateKey(sshBuf)) {
                wchar_t reason[512] = L"SSH 私钥无效";
                U8ToWBuffer(GetLogicErrorMessage(), reason, 512);
                ShowMessage(hwnd, reason, L"无法保存账户", MB_OK);
                break;
            }

            int editIndex = -1;
            for (int i = 0; i < config.account_count; i++) {
                if (currentEditID[0] && strcmp(config.accounts[i].id, currentEditID) == 0) {
                    editIndex = i;
                    break;
                }
            }
            if (currentEditID[0] && editIndex < 0) {
                ShowMessage(hwnd, L"正在编辑的账户已不存在，请重新选择", L"账户状态已变化", MB_OK);
                break;
            }
            for (int i = 0; i < config.account_count; i++) {
                if (i != editIndex && _stricmp(config.accounts[i].email, emailBuf) == 0) {
                    ShowMessage(hwnd, L"这个邮箱已存在于账户列表中", L"邮箱重复", MB_OK);
                    editIndex = -2;
                    break;
                }
            }
            if (editIndex == -2) break;
            if (!currentEditID[0] && config.account_count >= MAX_ACCOUNTS) {
                ShowMessage(hwnd, L"最多只能保存 50 个账户", L"账户已满", MB_OK);
                break;
            }

            Config updated = config;
            Account candidate;
            if (editIndex >= 0) candidate = updated.accounts[editIndex];
            else memset(&candidate, 0, sizeof(candidate));
            strncpy(candidate.name, nameBuf, sizeof(candidate.name) - 1);
            candidate.name[sizeof(candidate.name) - 1] = 0;
            strncpy(candidate.email, emailBuf, sizeof(candidate.email) - 1);
            candidate.email[sizeof(candidate.email) - 1] = 0;
            strncpy(candidate.ssh_key_path, sshBuf, sizeof(candidate.ssh_key_path) - 1);
            candidate.ssh_key_path[sizeof(candidate.ssh_key_path) - 1] = 0;
            if (!UpdateAccountHosts(&candidate)) break;

            if (editIndex >= 0) {
                if (!ApplyAccountSettings(candidate.name, candidate.email,
                        candidate.ssh_key_path,
                        (const char (*)[HOST_LEN])candidate.host_list,
                        candidate.host_count)) {
                    wchar_t reason[512] = L"无法应用账户改动，账户未更新";
                    const char* logicError = GetLogicErrorMessage();
                    if (logicError[0]) U8ToWBuffer(logicError, reason, 512);
                    ShowMessage(hwnd, reason, L"更新失败", MB_OK);
                    break;
                }
                updated.accounts[editIndex] = candidate;
                strncpy(updated.active_id, candidate.id, sizeof(updated.active_id) - 1);
                updated.active_id[sizeof(updated.active_id) - 1] = 0;
            } else {
                FILETIME now;
                GetSystemTimeAsFileTime(&now);
                unsigned long long generatedId = ((unsigned long long)now.dwHighDateTime << 32) | now.dwLowDateTime;
                snprintf(candidate.id, sizeof(candidate.id), "%llu", generatedId);
                updated.accounts[updated.account_count++] = candidate;
            }
            if (!SaveConfig(&updated)) {
                wchar_t reason[512] = L"无法写入账户配置";
                U8ToWBuffer(GetLogicErrorMessage(), reason, 512);
                ShowMessage(hwnd, reason, L"保存失败", MB_OK);
                break;
            }
            config = updated;
            g_configLoadFailed = FALSE;
            ClearForm();
            RefreshList();
            UpdateStatus();
            ShowMessage(hwnd, editIndex >= 0
                ? L"账户已更新并切换"
                : L"账户已添加", L"保存成功", MB_OK);
        }
        else if (id == ID_BTN_DELETE) {
            if (strlen(currentEditID) > 0) {
                if (ShowMessage(hwnd, L"确定要删除此账户吗？", L"确认删除", MB_YESNO) == IDYES) {
                    int found = -1;
                    for (int i = 0; i < config.account_count; i++) {
                        if (strcmp(config.accounts[i].id, currentEditID) == 0) {
                            found = i;
                            break;
                        }
                    }
                    if (found != -1) {
                        Config updated = config;
                        BOOL wasActive = strcmp(updated.accounts[found].id, updated.active_id) == 0 ||
                                         IsAccountCurrent(&updated.accounts[found]);
                        if (wasActive) updated.active_id[0] = 0;
                        for (int i = found; i < updated.account_count - 1; i++) {
                            updated.accounts[i] = updated.accounts[i + 1];
                        }
                        updated.account_count--;
                        if (!SaveConfig(&updated)) {
                            ShowMessage(hwnd, L"无法保存删除结果，账户未删除", L"删除失败", MB_OK);
                            break;
                        }
                        if (wasActive && !ClearAllManagedSSHConfig()) {
                            SaveConfig(&config);
                            wchar_t reason[512] = L"无法清理程序管理的 SSH 配置";
                            U8ToWBuffer(GetLogicErrorMessage(), reason, 512);
                            ShowMessage(hwnd, reason, L"删除失败", MB_OK);
                            break;
                        }
                        config = updated;
                        ClearForm();
                        RefreshList();
                        UpdateStatus();
                        ShowMessage(hwnd, wasActive
                            ? L"账户已删除；Git 全局用户名和邮箱保持不变"
                            : L"账户已删除", L"删除成功", MB_OK);
                    }
                }
            } else {
                ShowMessage(hwnd, L"请先从左侧选择要删除的账户", L"未选择账户", MB_OK);
            }
        }
        else if (id == ID_BTN_SWITCH) {
            int idx = SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                int accIdx = SendMessageW(hList, LB_GETITEMDATA, idx, 0);
                if (accIdx < 0 || accIdx >= config.account_count) break;
                Account* acc = &config.accounts[accIdx];
                if (ApplyAccountSettings(acc->name, acc->email, acc->ssh_key_path,
                        (const char (*)[HOST_LEN])acc->host_list, acc->host_count)) {
                    strncpy(config.active_id, acc->id, sizeof(config.active_id) - 1);
                    config.active_id[sizeof(config.active_id) - 1] = 0;
                    BOOL saved = SaveConfig(&config);
                    RefreshList();
                    UpdateStatus();
                    wchar_t wName[NAME_LEN];
                    U8ToWBuffer(acc->name, wName, NAME_LEN);
                    wchar_t msgBuf[512];
                    _snwprintf(msgBuf, 511, saved
                        ? L"已切换到 %ls"
                        : L"已切换到 %ls，但未能保存当前标记", wName);
                    msgBuf[511] = L'\0';
                    ShowMessage(hwnd, msgBuf, saved ? L"切换成功" : L"切换完成", MB_OK);
                } else {
                    wchar_t reason[512] = L"切换失败，Git 与 SSH 配置均未更改";
                    U8ToWBuffer(GetLogicErrorMessage(), reason, 512);
                    ShowMessage(hwnd, reason, L"切换失败", MB_OK);
                }
            } else {
                ShowMessage(hwnd, L"请先选择一个账户", L"提示", MB_OK);
            }
        }
        break;
    }
    case WM_DPICHANGED: {
        RECT* suggested = (RECT*)lParam;
        g_dpiScale = HIWORD(wParam) / 96.0f;
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        HFONT oldGlobal = hGlobalFont;
        HFONT oldHint = hHintFont;
        HFONT oldTitle = hTitleFont;
        HFONT oldSection = hSectionFont;
        hGlobalFont = CreateUiFont(10, FW_NORMAL);
        hHintFont = CreateUiFont(9, FW_NORMAL);
        hTitleFont = CreateUiFont(15, FW_SEMIBOLD);
        hSectionFont = CreateUiFont(11, FW_SEMIBOLD);
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGlobalFont);
        SendMessageW(hHeaderTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
        SendMessageW(hSectionAccounts, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
        SendMessageW(hSectionDetails, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
        SendMessageW(hSectionSSH, WM_SETFONT, (WPARAM)hSectionFont, TRUE);
        SendMessageW(hHeaderSubtitle, WM_SETFONT, (WPARAM)hHintFont, TRUE);
        SendMessageW(hLblHostHint, WM_SETFONT, (WPARAM)hHintFont, TRUE);
        SendMessageW(hList, LB_SETITEMHEIGHT, 0, DPI(UI_ACCOUNT_ITEM_HEIGHT));
        for (int i = 0; i < hHostControlCount; i++) {
            SetComboBoxClosedHeight(hHostControls[i * 2], DPI(UI_CONTROL_HEIGHT));
        }
        SetComboBoxClosedHeight(hSSH, DPI(UI_CONTROL_HEIGHT));
        DeleteObject(oldGlobal);
        DeleteObject(oldHint);
        DeleteObject(oldTitle);
        DeleteObject(oldSection);
        LayoutMainWindow(FALSE);
        SetComboBoxClosedHeight(hSSH, DPI(UI_CONTROL_HEIGHT));
        for (int i = 0; i < hHostControlCount; i++) {
            SetComboBoxClosedHeight(hHostControls[i * 2], DPI(UI_CONTROL_HEIGHT));
        }
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) LayoutMainWindow(FALSE);
        break;
    case WM_TRAY_IDENTITY: {
        UINT event = LOWORD(lParam);
        if (event == NIN_SELECT || event == NIN_KEYSELECT ||
            event == WM_LBUTTONUP) {
            RestoreMainWindow(hwnd);
        } else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
            int menuX = event == WM_CONTEXTMENU ? (short)LOWORD(wParam) : -1;
            int menuY = event == WM_CONTEXTMENU ? (short)HIWORD(wParam) : -1;
            int command = TrayIdentityShowContextMenu(
                &g_trayIdentity, config.show_taskbar_text, menuX, menuY);
            if (command == TRAY_MENU_OPEN) {
                RestoreMainWindow(hwnd);
            } else if (command == TRAY_MENU_TASKBAR_TEXT) {
                if (g_configLoadFailed) {
                    ShowMessage(hwnd,
                        L"账户配置损坏时不能保存任务栏设置，请先修复或备份 accounts.json。",
                        L"无法保存设置", MB_OK);
                    break;
                }
                config.show_taskbar_text = !config.show_taskbar_text;
                if (!SaveConfig(&config)) {
                    config.show_taskbar_text = !config.show_taskbar_text;
                    ShowMessage(hwnd, L"无法保存任务栏文字设置",
                                L"保存失败", MB_OK);
                    break;
                }
                ApplyTaskbarReminderState(hwnd);
            } else if (command == TRAY_MENU_EXIT) {
                g_exitRequested = TRUE;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        }
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        TaskbarIdentityReposition(&g_taskbarIdentity);
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* info = (MINMAXINFO*)lParam;
        info->ptMinTrackSize.x = DPI(680);
        info->ptMinTrackSize.y = DPI(520);
        return 0;
    }
    case WM_TIMER:
        if (wParam == ID_TIMER_IDENTITY) UpdateIdentitySurfaces();
        else if (wParam == ID_TIMER_TASKBAR) {
            TaskbarIdentityReposition(&g_taskbarIdentity);
        }
        break;
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, isDarkMode ? hBrushDark : hBrushLight);
        return 1;
    }
    case WM_CLOSE:
        if (config.show_identity_badge && !g_exitRequested) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_IDENTITY);
        KillTimer(hwnd, ID_TIMER_TASKBAR);
        TrayIdentityDestroy(&g_trayIdentity);
        TaskbarIdentityDestroy(&g_taskbarIdentity);
        DeleteObject(hBrushDark);
        DeleteObject(hBrushControlDark);
        DeleteObject(hBrushLight);
        DeleteObject(hBrushControlLight);
        DeleteObject(hGlobalFont);
        DeleteObject(hHintFont);
        DeleteObject(hTitleFont);
        DeleteObject(hSectionFont);
        if (g_hMutex) {
            ReleaseMutex(g_hMutex);
            CloseHandle(g_hMutex);
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    // 单实例检查：如果程序已在运行，激活现有窗口并退出
#ifdef GAM_TESTING
    g_hMutex = CreateMutexW(NULL, TRUE, L"GitAccountManagerC_UITest_SingleInstance");
#else
    g_hMutex = CreateMutexW(NULL, TRUE, L"GitAccountManagerC_SingleInstance");
#endif
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 程序已在运行，查找并激活现有窗口
        HWND hExistingWnd = FindWindowW(L"GitAccountManagerC", NULL);
        if (hExistingWnd) {
            // 如果窗口最小化，恢复它
            if (IsIconic(hExistingWnd)) {
                ShowWindow(hExistingWnd, SW_RESTORE);
            } else if (!IsWindowVisible(hExistingWnd)) {
                ShowWindow(hExistingWnd, SW_SHOW);
            }
            // 激活窗口
            SetForegroundWindow(hExistingWnd);
        }
        // 关闭互斥体句柄并退出
        if (g_hMutex) CloseHandle(g_hMutex);
        return 0;
    }
    
    // 初始化 DPI 缩放比例 (支持 2K/4K 高分屏)
    InitDPIScale();
    
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GitAccountManagerC";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101)); // 加载图标 (ID 101)

    RegisterClassW(&wc);

    // 窗口大小根据 DPI 缩放
    HWND hwnd = CreateWindowW(L"GitAccountManagerC", L"Git Account Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, DPI(720), DPI(550),
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}
