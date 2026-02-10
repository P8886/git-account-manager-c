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

// 全局变量
HWND hList, hName, hEmail, hSSH, hStatus, hBtnSave, hBtnCancel, hLblDetails, hBtnGenerate;
Config config;
char currentEditID[ID_LEN] = "";
BOOL isDarkMode = FALSE;
HBRUSH hBrushDark, hBrushLight, hBrushControlDark;
HFONT hGlobalFont = NULL; // 全局字体句柄

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
    currentEditID[0] = 0;
    SetWindowTextW(hBtnSave, L"添加账户");
    ShowWindow(hBtnCancel, SW_HIDE);
    SendMessageW(hList, LB_SETCURSEL, -1, 0);
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
        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        
        // 获取参数
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        LPCWSTR text = (LPCWSTR)pCreate->lpCreateParams;
        
        // 内容文本
        HWND hStatic = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER, 
            20, 30, 310, 60, hwnd, NULL, NULL, NULL);
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

    int width = 350;
    int height = 180;
    
    // 计算居中位置
    RECT rcOwner;
    GetWindowRect(owner, &rcOwner);
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - width) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - height) / 2;

    HWND hMsgBox = CreateWindowW(L"GitManagerMsgBox", title, WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_BORDER,
        x, y, width, height, owner, NULL, GetModuleHandle(NULL), (LPVOID)text);

    int result = 0;
    SetProp(hMsgBox, L"ResultPtr", &result);

    // 创建按钮
    HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    
    if (type == MB_YESNO) {
        HWND hBtnYes = CreateWindowW(L"BUTTON", L"是(Y)", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW, 
            60, 100, 100, 32, hMsgBox, (HMENU)IDYES, NULL, NULL);
        HWND hBtnNo = CreateWindowW(L"BUTTON", L"否(N)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW, 
            190, 100, 100, 32, hMsgBox, (HMENU)IDNO, NULL, NULL);
        SendMessageW(hBtnYes, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hBtnNo, WM_SETFONT, (WPARAM)hFont, TRUE);
    } else {
        HWND hBtn = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | BS_OWNERDRAW, 
            125, 100, 100, 32, hMsgBox, (HMENU)ID_BTN_MSG_OK, NULL, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    // 设置主题
    SetTitleBarTheme(hMsgBox, isDarkMode);
    
    // 模态循环
    EnableWindow(owner, FALSE);
    
    MSG msg;
    while (IsWindow(hMsgBox) && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            result = IDCANCEL;
            DestroyWindow(hMsgBox);
            break;
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
    InvalidateRect(hwnd, NULL, TRUE);
    EnumChildWindows(hwnd, (WNDENUMPROC)(void*)InvalidateRect, (LPARAM)TRUE);
    
    // 设置标题栏主题
    SetTitleBarTheme(hwnd, isDarkMode);

    // 更新夜间模式按钮图标
    HWND hBtnTheme = GetDlgItem(hwnd, ID_BTN_THEME);
    if (hBtnTheme) {
        SetWindowTextW(hBtnTheme, isDarkMode ? L"☀" : L"🌙");
    }

    // 动态加载 SetWindowTheme
    // 为了减小体积，这里不使用静态链接 uxtheme.lib，而是保持动态加载，或者如果想更小，可以考虑静态链接并剥离未使用的符号
    // 但动态加载更安全。这里我们保持现状，优化 flags 已经在 build.bat 中处理。
    LPCWSTR theme = isDarkMode ? L"DarkMode_Explorer" : NULL;
    MySetWindowTheme(hList, theme, NULL);
    MySetWindowTheme(hSSH, theme, NULL);
    
    // 切换按钮样式 (OwnerDraw) - 始终启用 OwnerDraw 以保持圆角风格一致
    int btnIds[] = {ID_BTN_BROWSE, ID_BTN_SAVE, ID_BTN_DELETE, ID_BTN_SWITCH, ID_BTN_CANCEL, ID_BTN_THEME, ID_BTN_GENERATE};
    for (int i = 0; i < 7; i++) {
        HWND hBtn = GetDlgItem(hwnd, btnIds[i]);
        if (hBtn) {
            LONG_PTR style = GetWindowLongPtr(hBtn, GWL_STYLE);
            style |= BS_OWNERDRAW; // 无论明暗模式，都使用 OwnerDraw
            SetWindowLongPtr(hBtn, GWL_STYLE, style);
            SetWindowPos(hBtn, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            InvalidateRect(hBtn, NULL, TRUE);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 使用 18px 字体
        hGlobalFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        
        hBrushDark = CreateSolidBrush(RGB(32, 32, 32));
        hBrushControlDark = CreateSolidBrush(RGB(50, 50, 50));
        hBrushLight = GetSysColorBrush(COLOR_WINDOW);

        // 布局常量
        int margin = 35;
        int listWidth = 200;
        int rightX = margin + listWidth + margin;
        int rightWidth = 320; 
        int rowHeight = 35;   
        int labelWidth = 70;  
        int inputX = rightX + labelWidth + 10;
        int inputWidth = rightWidth - labelWidth - 10;
        int y = margin;

        // 左侧列表
        hList = CreateWindowW(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            margin, y, listWidth, 360, hwnd, (HMENU)ID_LIST, NULL, NULL);
        
        // 顶部：用户名
        CreateWindowW(L"STATIC", L"用户名:", WS_CHILD | WS_VISIBLE, rightX, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        hName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 
            inputX, y, inputWidth, 26, hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);

        y += rowHeight + 15;
        // 邮箱
        CreateWindowW(L"STATIC", L"邮箱:", WS_CHILD | WS_VISIBLE, rightX, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        hEmail = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 
            inputX, y, inputWidth, 26, hwnd, (HMENU)ID_EDIT_EMAIL, NULL, NULL);

        y += rowHeight + 15;
        // SSH Key
        CreateWindowW(L"STATIC", L"SSH密钥:", WS_CHILD | WS_VISIBLE, rightX, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        
        // 生成按钮 (放在 Label 同一行右侧)
        int genBtnW = 70;
        hBtnGenerate = CreateWindowW(L"BUTTON", L"生成", WS_CHILD | WS_VISIBLE, 
            rightX + rightWidth - genBtnW, y, genBtnW, 28, hwnd, (HMENU)ID_BTN_GENERATE, NULL, NULL);

        y += 35; // 下移一行

        // ComboBox 和 浏览按钮 (占据整行宽度)
        int browseBtnW = 40;
        int comboW = rightWidth - browseBtnW - 5;

        hSSH = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL, 
            rightX, y, comboW, 200, hwnd, (HMENU)ID_COMBO_SSH, NULL, NULL);

        // 浏览按钮
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, 
            rightX + comboW + 5, y, browseBtnW, 28, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

        y += rowHeight + 10;
        // 按钮组 (保存/取消/删除)
        int btnWidth = 100;
        hBtnSave = CreateWindowW(L"BUTTON", L"添加账户", WS_CHILD | WS_VISIBLE, rightX, y, btnWidth, 32, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);
        
        hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, rightX + btnWidth + 10, y, 70, 32, hwnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);
        ShowWindow(hBtnCancel, SW_HIDE);
        
        CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE, rightX + rightWidth - 80, y, 80, 32, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);

        y += 60; // 间距
        
        // 全局操作区
        CreateWindowW(L"BUTTON", L"切换到选中账户", WS_CHILD | WS_VISIBLE, rightX, y, rightWidth, 40, hwnd, (HMENU)ID_BTN_SWITCH, NULL, NULL);
        
        // 状态栏
        // 使用 WS_BORDER 替代 SS_SUNKEN 以在明暗模式下保持一致的边框样式
        // 底部 Y 坐标 = margin + mainHeight + 15 = 20 + 360 + 15 = 395
        int statusY = margin + 360 + 15;
        int statusWidth = 535; // 590 - 20(left) - 35(right btn area)
        hStatus = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | WS_BORDER, 
            margin, statusY, statusWidth, 25, hwnd, (HMENU)ID_STATUS, NULL, NULL);

        // 底部夜间模式切换按钮 (小图标)
        // 位置: margin + statusWidth + 5 = 20 + 535 + 5 = 560
        HWND hBtnTheme = CreateWindowW(L"BUTTON", L"🌙", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
            margin + statusWidth + 5, statusY - 3, 30, 30, hwnd, (HMENU)ID_BTN_THEME, NULL, NULL);

        // 设置全局字体 - 使用正确的回调函数
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGlobalFont);

        ApplyTheme(hwnd); // 初始化主题样式 (包括按钮圆角)

        LoadConfig(&config);
        RefreshList();
        UpdateStatus();
        LoadSSHKeysToCombo();
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        // Draw rounded borders for Inputs
        if (hName && hEmail) {
             RECT rcName, rcEmail;
             GetWindowRect(hName, &rcName);
             GetWindowRect(hEmail, &rcEmail);
             
             POINT ptNameTL = {rcName.left, rcName.top};
             POINT ptNameBR = {rcName.right, rcName.bottom};
             ScreenToClient(hwnd, &ptNameTL);
             ScreenToClient(hwnd, &ptNameBR);
             SetRect(&rcName, ptNameTL.x, ptNameTL.y, ptNameBR.x, ptNameBR.y);

             POINT ptEmailTL = {rcEmail.left, rcEmail.top};
             POINT ptEmailBR = {rcEmail.right, rcEmail.bottom};
             ScreenToClient(hwnd, &ptEmailTL);
             ScreenToClient(hwnd, &ptEmailBR);
             SetRect(&rcEmail, ptEmailTL.x, ptEmailTL.y, ptEmailBR.x, ptEmailBR.y);

             // Inflate to draw border OUTSIDE the edit control
             InflateRect(&rcName, 3, 3);
             InflateRect(&rcEmail, 3, 3);
             
             DrawRoundedBorder(hdc, &rcName, isDarkMode, hBrushControlDark, GetSysColorBrush(COLOR_WINDOW));
             DrawRoundedBorder(hdc, &rcEmail, isDarkMode, hBrushControlDark, GetSysColorBrush(COLOR_WINDOW));
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        if (isDarkMode) {
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(50, 50, 50));
            return (LRESULT)hBrushControlDark;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        int id = GetDlgCtrlID((HWND)lParam);
        
        // 状态栏不使用透明背景，防止文字重叠
        if (id == ID_STATUS) {
            SetBkMode(hdc, OPAQUE);
            if (isDarkMode) {
                SetTextColor(hdc, RGB(220, 220, 220));
                SetBkColor(hdc, RGB(32, 32, 32));
                return (LRESULT)hBrushDark;
            } else {
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
                return (LRESULT)hBrushLight;
            }
        }

        SetBkMode(hdc, TRANSPARENT); // 其他 Label 使用透明背景
        if (isDarkMode) {
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(32, 32, 32));
            return (LRESULT)hBrushDark;
        }
        return (LRESULT)GetStockObject(NULL_BRUSH); // 浅色模式下也使用透明背景
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
                SetWindowTextW(hSSH, U8ToW(acc->ssh_key_path));
                strcpy(currentEditID, acc->id);
                SetWindowTextW(hBtnSave, L"更新账户");
                ShowWindow(hBtnCancel, SW_SHOW);
            }
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
            }
        }
        else if (id == ID_BTN_GENERATE) {
             char email[EMAIL_LEN] = "";
             // Check if account is selected in listbox
             int idx = SendMessageW(hList, LB_GETCURSEL, 0, 0);
             if (idx != LB_ERR) {
                 int accIdx = SendMessageW(hList, LB_GETITEMDATA, idx, 0);
                 if (accIdx >= 0 && accIdx < config.account_count) {
                     strcpy(email, config.accounts[accIdx].email);
                 }
             }
             
             char outPath[MAX_PATH];
             if (ShowGenerateKeyDialog(hwnd, email, outPath)) {
                 ShowMessage(hwnd, L"密钥生成成功！", L"成功", MB_OK);
                 LoadSSHKeysToCombo(); // Refresh list
                 SetWindowTextW(hSSH, U8ToW(outPath)); // Auto select
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
            wchar_t wName[NAME_LEN], wEmail[EMAIL_LEN], wSSH[PATH_LEN];
            GetWindowTextW(hName, wName, NAME_LEN);
            GetWindowTextW(hEmail, wEmail, EMAIL_LEN);
            GetWindowTextW(hSSH, wSSH, PATH_LEN);

            char* name = WToU8(wName);
            char nameBuf[NAME_LEN]; strcpy(nameBuf, name);

            char* email = WToU8(wEmail);
            char emailBuf[EMAIL_LEN]; strcpy(emailBuf, email);
            
            char* ssh = WToU8(wSSH);
            char sshBuf[PATH_LEN]; strcpy(sshBuf, ssh);

            if (strlen(nameBuf) == 0 || strlen(emailBuf) == 0) {
                ShowMessage(hwnd, L"用户名和邮箱不能为空", L"错误", MB_OK);
                return 0;
            }

            if (strlen(currentEditID) > 0) {
                // 更新
                for (int i = 0; i < config.account_count; i++) {
                    if (strcmp(config.accounts[i].id, currentEditID) == 0) {
                        strcpy(config.accounts[i].name, nameBuf);
                        strcpy(config.accounts[i].email, emailBuf);
                        strcpy(config.accounts[i].ssh_key_path, sshBuf);
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
                    config.account_count++;
                    ShowMessage(hwnd, L"账户添加成功", L"成功", MB_OK);
                }
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
                    wchar_t msg[512];
                    swprintf(msg, 512, L"已切换到 %s", U8ToW(acc->name));
                    ShowMessage(hwnd, msg, L"成功", MB_OK);
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
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GitAccountManagerC";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    // 调整窗口大小 (增加宽度和高度以适应更宽松的布局)
    HWND hwnd = CreateWindowW(L"GitAccountManagerC", L"Git Account Manager (C Version)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}