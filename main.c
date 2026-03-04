#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <time.h>
#include <dwmapi.h> // 引入 DWM API
#include <shlobj.h> // 用于 SHGetFolderPath
#include "shared.h"
#include "logic.h"
#include "ui_draw.h"
#include "ui_gen_key.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
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

// 全局变量
HWND hList, hName, hEmail, hSSH, hHost, hLblHost, hStatus, hBtnSave, hBtnCancel, hLblDetails, hBtnGenerate;
Config config;
char currentEditID[ID_LEN] = "";
BOOL isDarkMode = FALSE;
BOOL g_bIgnoreEditChange = FALSE;
HBRUSH hBrushDark, hBrushLight, hBrushControlDark;
HFONT hGlobalFont = NULL; // 全局字体句柄
float g_dpiScale = 1.0f;  // DPI 缩放比例 (2K屏幕通常为 1.25 或 1.5)

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
        PSetWindowTheme pFunc = (PSetWindowTheme)GetProcAddress(hMod, "SetWindowTheme");
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

void UpdateHostVisibility() {
    // 始终显示，不再隐藏
}

void OnSSHKeyChanged(const wchar_t* wSSHPath) {
    if (wSSHPath && wcslen(wSSHPath) > 0) {
        char u8Path[PATH_LEN];
        strcpy(u8Path, WToU8(wSSHPath));
        char foundHost[256] = "";
        
        if (GetHostFromSSHConfig(u8Path, foundHost, sizeof(foundHost)) && strlen(foundHost) > 0) {
            SetWindowTextW(hHost, U8ToW(foundHost));
        } else {
            const wchar_t* filename = wcsrchr(wSSHPath, L'\\');
            if (filename) filename++;
            else filename = wSSHPath;
            
            const wchar_t* filenameUnix = wcsrchr(filename, L'/');
            if (filenameUnix) filename = filenameUnix + 1;
            
            const wchar_t* id_pos = wcsstr(filename, L"id_");
            if (id_pos) {
                const wchar_t* host_part = id_pos + 3;
                if (wcslen(host_part) > 0) {
                    SetWindowTextW(hHost, host_part);
                }
            }
        }
    } else {
        SetWindowTextW(hHost, L"");
    }
}

// 刷新列表显示
void RefreshList() {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < config.account_count; i++) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s <%s>", config.accounts[i].name, config.accounts[i].email);
        
        if (strcmp(config.accounts[i].id, config.active_id) == 0) {
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
    SendMessageW(hHost, CB_SETCURSEL, -1, 0);
    SetWindowTextW(hHost, L"");
    currentEditID[0] = 0;
    SetWindowTextW(hBtnSave, L"添加账户");
    ShowWindow(hBtnCancel, SW_HIDE);
    SendMessageW(hList, LB_SETCURSEL, -1, 0);
    UpdateHostVisibility();
}

// 更新状态栏
void UpdateStatus() {
    char name[NAME_LEN], email[EMAIL_LEN];
    GetGlobalConfig(name, email);
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "   当前全局身份: %s <%s>", name, email);
    SetWindowTextW(hStatus, U8ToW(buffer));
    InvalidateRect(hStatus, NULL, TRUE); // 强制重绘以修复文字重叠问题
}

// 设置 DWM 沉浸式暗黑模式标题栏
void SetTitleBarTheme(HWND hwnd, BOOL dark) {
    BOOL value = dark;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}

// --- 自定义消息框 (居中且支持暗黑模式) ---
#define ID_BTN_MSG_OK 201
#define ID_BTN_MSG_YES IDYES
#define ID_BTN_MSG_NO IDNO

LRESULT CALLBACK MsgBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(DPI(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        
        // 获取参数
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        LPCWSTR text = (LPCWSTR)pCreate->lpCreateParams;
        
        // 内容文本 (DPI 缩放)
        HWND hStatic = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER, 
            DPI(20), DPI(30), DPI(310), DPI(60), hwnd, NULL, NULL, NULL);
        SendMessageW(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);
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
    HFONT hFont = CreateFontW(DPI(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    
    if (type == MB_YESNO) {
        HWND hBtnYes = CreateWindowW(L"BUTTON", L"是(Y)", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW, 
            DPI(60), DPI(100), DPI(100), DPI(26), hMsgBox, (HMENU)IDYES, NULL, NULL);
        HWND hBtnNo = CreateWindowW(L"BUTTON", L"否(N)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 
            DPI(190), DPI(100), DPI(100), DPI(26), hMsgBox, (HMENU)IDNO, NULL, NULL);
        SendMessageW(hBtnYes, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnNo, WM_SETFONT, (WPARAM)hFont, TRUE);
    } else {
        HWND hBtn = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW, 
            DPI(125), DPI(100), DPI(100), DPI(26), hMsgBox, (HMENU)ID_BTN_MSG_OK, NULL, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
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

// 应用主题颜色
void ApplyTheme(HWND hwnd) {
    // 设置标题栏主题
    SetTitleBarTheme(hwnd, isDarkMode);

    // 更新夜间模式按钮图标
    HWND hBtnTheme = GetDlgItem(hwnd, ID_BTN_THEME);
    if (hBtnTheme) {
        SetWindowTextW(hBtnTheme, isDarkMode ? L"☀️" : L"🌙");
    }

    // ListBox 和 ComboBox 使用 DarkMode_Explorer 主题（获得暗色滚动条和下拉列表）
    LPCWSTR theme = isDarkMode ? L"DarkMode_Explorer" : NULL;
    MySetWindowTheme(hList, theme, NULL);
    MySetWindowTheme(hSSH, theme, NULL);
    MySetWindowTheme(hHost, theme, NULL);
    // 切换按钮样式 (OwnerDraw) - 始终启用 OwnerDraw 以保持圆角风格一致
    int btnIds[] = {ID_BTN_BROWSE, ID_BTN_SAVE, ID_BTN_DELETE, ID_BTN_SWITCH, ID_BTN_CANCEL, ID_BTN_THEME, ID_BTN_GENERATE};
    for (int i = 0; i < 7; i++) {
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
    EnumChildWindows(hwnd, (WNDENUMPROC)(void*)InvalidateRect, (LPARAM)NULL);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 根据 DPI 缩放创建字体 (基础 18px)
        hGlobalFont = CreateFontW(DPI(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        
        hBrushDark = CreateSolidBrush(RGB(32, 32, 32));
        hBrushControlDark = CreateSolidBrush(RGB(50, 50, 50));
        hBrushLight = GetSysColorBrush(COLOR_WINDOW);

        // 布局常量 (使用 DPI 缩放)
        int margin = DPI(25);
        int listWidth = DPI(200);
        int rightX = margin + listWidth + DPI(25);
        int rightWidth = DPI(340);
        
        // 统一高度：所有控件（输入框、按钮、下拉框）
        int ctrlH = DPI(26);
        int labelWidth = DPI(70);
        int inputX = rightX + labelWidth + DPI(10);
        int inputWidth = rightWidth - labelWidth - DPI(10);
        int rowGap = DPI(16); // 统一行间距
        int y = margin;

        // 左侧列表（高度稍后根据右侧内容确定）
        // 先占位，后面设置高度
        hList = CreateWindowW(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
            margin, y, listWidth, DPI(100), hwnd, (HMENU)ID_LIST, NULL, NULL);
        
        // Row 1: 用户名
        CreateWindowW(L"STATIC", L"用户名:", WS_CHILD | WS_VISIBLE, rightX, y + DPI(4), labelWidth, DPI(20), hwnd, NULL, NULL, NULL);
        hName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 
            inputX, y, inputWidth, ctrlH, hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);

        y += ctrlH + rowGap;

        // Row 2: 邮箱
        CreateWindowW(L"STATIC", L"邮箱:", WS_CHILD | WS_VISIBLE, rightX, y + DPI(4), labelWidth, DPI(20), hwnd, NULL, NULL, NULL);
        hEmail = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 
            inputX, y, inputWidth, ctrlH, hwnd, (HMENU)ID_EDIT_EMAIL, NULL, NULL);

        y += ctrlH + rowGap;

        // Row 3: SSH密钥 标签 + 生成按钮
        CreateWindowW(L"STATIC", L"SSH密钥:", WS_CHILD | WS_VISIBLE, rightX, y + DPI(4), labelWidth, DPI(20), hwnd, NULL, NULL, NULL);
        int genBtnW = DPI(70);
        hBtnGenerate = CreateWindowW(L"BUTTON", L"生成", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 
            rightX + rightWidth - genBtnW, y, genBtnW, ctrlH, hwnd, (HMENU)ID_BTN_GENERATE, NULL, NULL);

        y += ctrlH + DPI(8); // SSH 组内间距小一点

        // Row 4: SSH ComboBox + 浏览按钮
        int browseBtnW = DPI(40);
        int comboW = rightWidth - browseBtnW - DPI(5);
        hSSH = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP, 
            rightX, y, comboW, DPI(200), hwnd, (HMENU)ID_COMBO_SSH, NULL, NULL);
        SendMessage(hSSH, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)DPI(22));
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 
            rightX + comboW + DPI(5), y, browseBtnW, ctrlH, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

        y += ctrlH + rowGap + DPI(4);

        // Row 5: SSH Host
        hLblHost = CreateWindowW(L"STATIC", L"SSH Host:", WS_CHILD | WS_VISIBLE, rightX, y + DPI(4), labelWidth, DPI(20), hwnd, NULL, NULL, NULL);
        hHost = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP, 
            inputX, y, inputWidth, DPI(180), hwnd, (HMENU)ID_COMBO_HOST, NULL, NULL);
        SendMessage(hHost, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)DPI(22));
        
        // 添加常见的Git服务
        SendMessageW(hHost, CB_ADDSTRING, 0, (LPARAM)L"github.com");
        SendMessageW(hHost, CB_ADDSTRING, 0, (LPARAM)L"gitlab.com");
        SendMessageW(hHost, CB_ADDSTRING, 0, (LPARAM)L"gitee.com");
        SendMessageW(hHost, CB_ADDSTRING, 0, (LPARAM)L"bitbucket.org");

        y += ctrlH + rowGap;

        // Row 6: 添加/取消/删除 按钮组
        int btnWidth = DPI(100);
        hBtnSave = CreateWindowW(L"BUTTON", L"添加账户", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX, y, btnWidth, ctrlH, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);
        hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX + btnWidth + DPI(10), y, DPI(70), ctrlH, hwnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);
        ShowWindow(hBtnCancel, SW_HIDE);
        CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX + rightWidth - DPI(80), y, DPI(80), ctrlH, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);

        y += ctrlH + rowGap + DPI(4);

        // Row 7: 切换到选中账户
        CreateWindowW(L"BUTTON", L"切换到选中账户", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rightX, y, rightWidth, ctrlH, hwnd, (HMENU)ID_BTN_SWITCH, NULL, NULL);

        // 状态栏和列表高度：根据窗口客户区大小计算，状态栏贴底
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int clientH = rcClient.bottom;
        int statusY = clientH - margin - ctrlH;
        int statusWidth = rcClient.right - margin * 2 - ctrlH - DPI(5); // 留出主题按钮空间
        hStatus = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | WS_BORDER, 
            margin, statusY, statusWidth, ctrlH, hwnd, (HMENU)ID_STATUS, NULL, NULL);

        // 夜间模式切换按钮
        HWND hBtnTheme = CreateWindowW(L"BUTTON", L"🌙", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
            margin + statusWidth + DPI(5), statusY, ctrlH, ctrlH, hwnd, (HMENU)ID_BTN_THEME, NULL, NULL);

        // 列表高度：从顶部边距到状态栏上方
        int listHeight = statusY - margin - DPI(15);
        SetWindowPos(hList, NULL, 0, 0, listWidth, listHeight, SWP_NOMOVE | SWP_NOZORDER);

        // 设置全局字体
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGlobalFont);

        // 禁用 EDIT 和 STATIC 控件的视觉主题，让 WM_CTLCOLOREDIT 完全控制颜色
        // （参考 ui_gen_key.c 的做法：不依赖视觉主题，自己处理两种模式的颜色）
        MySetWindowTheme(hName, L"", L"");
        MySetWindowTheme(hEmail, L"", L"");
        MySetWindowTheme(hStatus, L"", L"");

        ApplyTheme(hwnd);

        LoadConfig(&config);
        AutoImportGlobalIdentity(&config); // 首次使用时自动导入当前 Git 全局身份
        if (config.account_count > 0 && config.active_id[0] != 0) {
            SaveConfig(&config); // 如果有导入则保存配置
        }
        RefreshList();
        UpdateStatus();
        LoadSSHKeysToCombo();
        UpdateHostVisibility();
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        int id = GetDlgCtrlID((HWND)lParam);
        
        if (isDarkMode) {
            SetTextColor(hdc, RGB(220, 220, 220));
            if (msg == WM_CTLCOLOREDIT) {
                SetBkColor(hdc, RGB(50, 50, 50));
                return (LRESULT)hBrushControlDark;
            }
            // STATIC 控件：全部使用不透明背景
            // 状态栏背景色=控件色，标签背景色=窗口背景色（与父窗口融合）
            SetBkMode(hdc, OPAQUE);
            if (id == ID_STATUS) {
                SetBkColor(hdc, RGB(50, 50, 50));
                return (LRESULT)hBrushControlDark;
            }
            SetBkColor(hdc, RGB(32, 32, 32));
            return (LRESULT)hBrushDark;
        }
        
        // 浅色模式：全部使用不透明背景（WS_CLIPCHILDREN 下不能依赖透明）
        SetBkMode(hdc, OPAQUE);
        if (msg == WM_CTLCOLOREDIT) {
            SetTextColor(hdc, RGB(0, 0, 0));
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)hBrushLight;
        }
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        return (LRESULT)hBrushLight;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        if (isDarkMode) {
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(50, 50, 50));
            return (LRESULT)hBrushControlDark;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawOwnerDrawButton(pDIS, isDarkMode, hBrushDark, hBrushLight);
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
                Account* acc = &config.accounts[accIdx];
                SetWindowTextW(hName, U8ToW(acc->name));
                SetWindowTextW(hEmail, U8ToW(acc->email));
                
                g_bIgnoreEditChange = TRUE;
                SetWindowTextW(hSSH, U8ToW(acc->ssh_key_path));
                g_bIgnoreEditChange = FALSE;
                
                char foundHost[256] = "";
                if (GetHostFromSSHConfig(acc->ssh_key_path, foundHost, sizeof(foundHost)) && strlen(foundHost) > 0) {
                    SetWindowTextW(hHost, U8ToW(foundHost));
                } else {
                    SetWindowTextW(hHost, U8ToW(acc->git_host));
                }

                strcpy(currentEditID, acc->id);
                SetWindowTextW(hBtnSave, L"更新账户");
                ShowWindow(hBtnCancel, SW_SHOW);
                UpdateHostVisibility();
            }
        }
        else if (id == ID_COMBO_HOST && HIWORD(wParam) == CBN_SELCHANGE) {
            // Git服务Host选择变化时，获取选中的项目内容
            int idx = SendMessageW(hHost, CB_GETCURSEL, 0, 0);
            if (idx != CB_ERR) {
                wchar_t buffer[HOST_LEN];
                SendMessageW(hHost, CB_GETLBTEXT, idx, (LPARAM)buffer);
                SetWindowTextW(hHost, buffer);
            }
        }
        else if (id == ID_COMBO_SSH && HIWORD(wParam) == CBN_SELCHANGE) {
            // ComboBox 选择变化时，获取选中的项目内容
            int idx = SendMessageW(hSSH, CB_GETCURSEL, 0, 0);
            if (idx != CB_ERR) {
                wchar_t buffer[PATH_LEN];
                SendMessageW(hSSH, CB_GETLBTEXT, idx, (LPARAM)buffer);
                SetWindowTextW(hSSH, buffer);
                OnSSHKeyChanged(buffer);
            }
        }
        else if (id == ID_COMBO_SSH && HIWORD(wParam) == CBN_EDITCHANGE) {
            if (g_bIgnoreEditChange) break;
            wchar_t buffer[PATH_LEN];
            GetWindowTextW(hSSH, buffer, PATH_LEN);
            OnSSHKeyChanged(buffer);
        }
        else if (id == ID_BTN_BROWSE) {
            wchar_t buffer[MAX_PATH] = L"";
            
            // Get User Profile/.ssh directory
            char userProfile[MAX_PATH];
            wchar_t wSSHDir[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) {
                char sshDir[MAX_PATH];
                snprintf(sshDir, MAX_PATH, "%s\\.ssh", userProfile);
                mbstowcs(wSSHDir, sshDir, MAX_PATH);
            }

            OPENFILENAMEW ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"All Files\0*.*\0Key Files\0id_*\0";
            ofn.lpstrInitialDir = wSSHDir;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(hSSH, buffer);
                OnSSHKeyChanged(buffer);
            }
        }
        else if (id == ID_BTN_GENERATE) {
             char email[EMAIL_LEN] = "";
             char host[HOST_LEN] = "github.com";
             // Check if account is selected in listbox
             int idx = SendMessageW(hList, LB_GETCURSEL, 0, 0);
             if (idx != LB_ERR) {
                 int accIdx = SendMessageW(hList, LB_GETITEMDATA, idx, 0);
                 if (accIdx >= 0 && accIdx < config.account_count) {
                     strcpy(email, config.accounts[accIdx].email);
                     strcpy(host, config.accounts[accIdx].git_host);
                 }
             } else {
                 wchar_t wHost[HOST_LEN];
                 GetWindowTextW(hHost, wHost, HOST_LEN);
                 if (wcslen(wHost) > 0) {
                     strcpy(host, WToU8(wHost));
                 }
             }
             
             char outPath[MAX_PATH];
             if (ShowGenerateKeyDialog(hwnd, email, host, outPath)) {
                  LoadSSHKeysToCombo(); // Refresh list
                  wchar_t* wOutPath = U8ToW(outPath);
                  SetWindowTextW(hSSH, wOutPath); // Auto select
                  OnSSHKeyChanged(wOutPath);
             }
        }
        else if (id == ID_BTN_THEME) {
            isDarkMode = !isDarkMode;
            ApplyTheme(hwnd);
        }
        else if (id == ID_BTN_CANCEL) {
            ClearForm();
        }
        else if (id == ID_BTN_SAVE) {
            wchar_t wName[NAME_LEN], wEmail[EMAIL_LEN], wSSH[PATH_LEN], wHost[HOST_LEN];
            GetWindowTextW(hName, wName, NAME_LEN);
            GetWindowTextW(hEmail, wEmail, EMAIL_LEN);
            GetWindowTextW(hSSH, wSSH, PATH_LEN);
            GetWindowTextW(hHost, wHost, HOST_LEN);

            char* name = WToU8(wName);
            char nameBuf[NAME_LEN]; strcpy(nameBuf, name);

            char* email = WToU8(wEmail);
            char emailBuf[EMAIL_LEN]; strcpy(emailBuf, email);
            
            char* ssh = WToU8(wSSH);
            char sshBuf[PATH_LEN]; strcpy(sshBuf, ssh);
            
            char* host = WToU8(wHost);
            char hostBuf[HOST_LEN]; strcpy(hostBuf, host);

            if (strlen(nameBuf) == 0 || strlen(emailBuf) == 0) {
                ShowMessage(hwnd, L"用户名和邮箱不能为空", L"错误", MB_OK);
                return 0;
            }
            
            // 简单的邮箱格式验证 (包含 @ 符号和 .)
            char* atSign = strchr(emailBuf, '@');
            if (!atSign || !strchr(atSign, '.')) {
                ShowMessage(hwnd, L"请输入有效的邮箱格式 (例如: test@example.com)", L"错误", MB_OK);
                return 0;
            }

            if (strlen(currentEditID) > 0) {
                // 更新
                for (int i = 0; i < config.account_count; i++) {
                    if (strcmp(config.accounts[i].id, currentEditID) == 0) {
                        strcpy(config.accounts[i].name, nameBuf);
                        strcpy(config.accounts[i].email, emailBuf);
                        strcpy(config.accounts[i].ssh_key_path, sshBuf);
                        strcpy(config.accounts[i].git_host, hostBuf);
                        break;
                    }
                }
                ShowMessage(hwnd, L"账户更新成功", L"成功", MB_OK);
            } else {
                // 新增
                if (config.account_count < MAX_ACCOUNTS) {
                    Account* acc = &config.accounts[config.account_count];
                    snprintf(acc->id, ID_LEN, "%lld", (long long)time(NULL));
                    strcpy(acc->name, nameBuf);
                    strcpy(acc->email, emailBuf);
                    strcpy(acc->ssh_key_path, sshBuf);
                    strcpy(acc->git_host, hostBuf);
                    config.account_count++;
                    ShowMessage(hwnd, L"账户添加成功", L"成功", MB_OK);
                }
            }
            
            // 如果指定了 SSH 密钥路径，自动添加到 SSH config
            if (strlen(sshBuf) > 0 && strlen(hostBuf) > 0) {
                AddExistingKeyToSSHConfig(sshBuf, emailBuf, hostBuf);
            }
            
            SaveConfig(&config);
            ClearForm();
            RefreshList();
        }
        else if (id == ID_BTN_DELETE) {
            if (strlen(currentEditID) > 0) {
                if (ShowMessage(hwnd, L"确定要删除此账户吗？", L"确认", MB_YESNO) == IDYES) {
                    int found = -1;
                    for (int i = 0; i < config.account_count; i++) {
                        if (strcmp(config.accounts[i].id, currentEditID) == 0) {
                            found = i;
                            break;
                        }
                    }
                    if (found != -1) {
                        // 移除并移位
                        for (int i = found; i < config.account_count - 1; i++) {
                            config.accounts[i] = config.accounts[i + 1];
                        }
                        config.account_count--;
                        SaveConfig(&config);
                        ClearForm();
                        RefreshList();
                    }
                }
            }
        }
        else if (id == ID_BTN_SWITCH) {
            int idx = SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                int accIdx = SendMessageW(hList, LB_GETITEMDATA, idx, 0);
                Account* acc = &config.accounts[accIdx];
                if (SetGlobalConfig(acc->name, acc->email, acc->ssh_key_path)) {
                    strcpy(config.active_id, acc->id);
                    SaveConfig(&config);
                    RefreshList();
                    UpdateStatus();
                    wchar_t wName[NAME_LEN];
                    wcscpy(wName, U8ToW(acc->name));
                    wchar_t msgBuf[512];
                    wcscpy(msgBuf, L"已切换到 ");
                    wcscat(msgBuf, wName);
                    ShowMessage(hwnd, msgBuf, L"成功", MB_OK);
                } else {
                    ShowMessage(hwnd, L"切换失败，请检查Git环境", L"错误", MB_OK);
                }
            } else {
                ShowMessage(hwnd, L"请先选择一个账户", L"提示", MB_OK);
            }
        }
        break;
    }
    case WM_ERASEBKGND: {
        if (isDarkMode) {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, hBrushDark);
            return 1;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_DESTROY:
        DeleteObject(hBrushDark);
        DeleteObject(hBrushControlDark);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 初始化 DPI 缩放比例 (支持 2K/4K 高分屏)
    InitDPIScale();
    
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GitAccountManagerC";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101)); // 加载图标 (ID 101)

    RegisterClassW(&wc);

    // 窗口大小根据 DPI 缩放 (基础 640x480)
    HWND hwnd = CreateWindowW(L"GitAccountManagerC", L"Git Account Manager",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, DPI(640), DPI(480),
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