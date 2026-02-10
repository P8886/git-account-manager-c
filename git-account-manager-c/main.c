#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <time.h>
#include <dwmapi.h> // 引入 DWM API
#include "logic.h"

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

// 全局变量
HWND hList, hName, hEmail, hSSH, hStatus, hBtnSave, hBtnCancel, hLblDetails;
Config config;
char currentEditID[ID_LEN] = "";
BOOL isDarkMode = FALSE;
HBRUSH hBrushDark, hBrushLight, hBrushControlDark;

// 动态加载 SetWindowTheme
typedef HRESULT (WINAPI *PSetWindowTheme)(HWND, LPCWSTR, LPCWSTR);

void MySetWindowTheme(HWND hwnd, LPCWSTR appName, LPCWSTR idList) {
    HMODULE hUxTheme = LoadLibrary(L"uxtheme.dll");
    if (hUxTheme) {
        PSetWindowTheme pSetWindowTheme = (PSetWindowTheme)GetProcAddress(hUxTheme, "SetWindowTheme");
        if (pSetWindowTheme) {
            pSetWindowTheme(hwnd, appName, idList);
        }
        FreeLibrary(hUxTheme);
    }
}

// 前向声明
void DrawButton(LPDRAWITEMSTRUCT pDIS);
void SetTitleBarTheme(HWND hwnd, BOOL dark);

// --- UTF-8 与 WideChar 转换辅助函数 ---

wchar_t* U8ToW(const char* utf8) {
    static wchar_t buffer[1024]; // 减小 buffer 节省栈/BSS
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
    snprintf(buffer, sizeof(buffer), "当前全局身份: %s <%s>", name, email);
    SetWindowTextW(hStatus, U8ToW(buffer));
}

// --- 自定义消息框 (居中且支持暗黑模式) ---
#define ID_BTN_MSG_OK 201

LRESULT CALLBACK MsgBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        
        // 获取传入的文本
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        LPCWSTR text = (LPCWSTR)pCreate->lpCreateParams;
        
        // 内容文本
        HWND hStatic = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER, 
            20, 30, 260, 60, hwnd, NULL, NULL, NULL);
        SendMessageW(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // OK 按钮
        HWND hBtn = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
            100, 100, 100, 32, hwnd, (HMENU)ID_BTN_MSG_OK, NULL, NULL);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_BTN_MSG_OK) {
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
            DrawButton(pDIS);
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
        DestroyWindow(hwnd);
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowMessage(HWND owner, LPCWSTR text, LPCWSTR title) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = MsgBoxProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GitManagerMsgBox";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    int width = 300;
    int height = 180;
    
    // 计算居中位置
    RECT rcOwner;
    GetWindowRect(owner, &rcOwner);
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - width) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - height) / 2;

    HWND hMsgBox = CreateWindowW(L"GitManagerMsgBox", title, WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
        x, y, width, height, owner, NULL, GetModuleHandle(NULL), (LPVOID)text);

    // 设置主题
    SetTitleBarTheme(hMsgBox, isDarkMode);
    
    // 模态循环
    EnableWindow(owner, FALSE);
    
    MSG msg;
    while (IsWindow(hMsgBox) && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

// 设置 DWM 沉浸式暗黑模式标题栏
void SetTitleBarTheme(HWND hwnd, BOOL dark) {
    BOOL value = dark;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}

// 自绘按钮函数
void DrawButton(LPDRAWITEMSTRUCT pDIS) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    BOOL isPressed = pDIS->itemState & ODS_SELECTED;
    
    // 1. Fill background with parent window color to clean up corners
    HBRUSH hBrushParent = isDarkMode ? hBrushDark : hBrushLight;
    FillRect(hdc, &rc, hBrushParent);
    
    // 2. Define colors
    COLORREF bg, border, text;
    if (isDarkMode) {
        bg = isPressed ? RGB(60, 60, 60) : RGB(45, 45, 45);
        border = RGB(100, 100, 100);
        text = RGB(240, 240, 240);
    } else {
        bg = isPressed ? RGB(200, 200, 200) : RGB(240, 240, 240); // Light mode button
        border = RGB(160, 160, 160);
        text = RGB(0, 0, 0);
    }
    
    // 3. Draw rounded button
    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    
    // Use slightly larger radius for "rounder" look
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
    
    // 4. Draw Text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);
    
    wchar_t buf[256];
    GetWindowTextW(pDIS->hwndItem, buf, 256);
    DrawTextW(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // Cleanup
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
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

    // 设置列表框和下拉框主题 (Explorer 风格在夜间模式下表现较好)
    LPCWSTR theme = isDarkMode ? L"DarkMode_Explorer" : NULL;
    MySetWindowTheme(hList, theme, NULL);
    MySetWindowTheme(hSSH, theme, NULL);
    
    // 切换按钮样式 (OwnerDraw)
    int btnIds[] = {ID_BTN_BROWSE, ID_BTN_SAVE, ID_BTN_DELETE, ID_BTN_SWITCH, ID_BTN_CANCEL, ID_BTN_THEME};
    for (int i = 0; i < 6; i++) {
        HWND hBtn = GetDlgItem(hwnd, btnIds[i]);
        if (hBtn) {
            // 所有按钮在暗黑模式下都使用 OwnerDraw，主题按钮在所有模式下都使用 OwnerDraw (因为它在创建时指定了)
            // 这里我们只更新那些需要动态切换样式的按钮
            LONG_PTR style = GetWindowLongPtr(hBtn, GWL_STYLE);
            if (isDarkMode || btnIds[i] == ID_BTN_THEME) {
                 style |= BS_OWNERDRAW;
            } else {
                 style &= ~BS_OWNERDRAW;
            }
            SetWindowLongPtr(hBtn, GWL_STYLE, style);
            SetWindowPos(hBtn, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            InvalidateRect(hBtn, NULL, TRUE);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        
        hBrushDark = CreateSolidBrush(RGB(32, 32, 32));
        hBrushControlDark = CreateSolidBrush(RGB(50, 50, 50));
        hBrushLight = GetSysColorBrush(COLOR_WINDOW);

        // 布局常量
        int margin = 15;
        int listWidth = 200;
        int rightX = margin + listWidth + margin;
        int rightWidth = 320; // 增加右侧宽度
        int rowHeight = 35;   // 增加行高
        int labelWidth = 70;  // 增加标签宽度
        int inputX = rightX + labelWidth + 10;
        int inputWidth = rightWidth - labelWidth - 10;
        int y = margin;

        // 左侧列表
        hList = CreateWindowW(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            margin, y, listWidth, 360, hwnd, (HMENU)ID_LIST, NULL, NULL);
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 右侧 GroupBox (使用 Button 类实现)
        CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            rightX, y - 5, rightWidth, 260, hwnd, (HMENU)ID_GROUP_DETAILS, NULL, NULL);

        // 独立的 GroupBox 标题 Label (用于解决夜间模式文字颜色问题)
        hLblDetails = CreateWindowW(L"STATIC", L" 账户详情 ", WS_CHILD | WS_VISIBLE,
            rightX + 10, y - 5, 80, 20, hwnd, (HMENU)ID_LBL_DETAILS, NULL, NULL);
        SendMessageW(hLblDetails, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        y += 30; // GroupBox 内部起始 Y

        // 用户名
        CreateWindowW(L"STATIC", L"用户名:", WS_CHILD | WS_VISIBLE, rightX + 15, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        hName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
            inputX, y, inputWidth - 20, 26, hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);
        SendMessageW(hName, WM_SETFONT, (WPARAM)hFont, TRUE);

        y += rowHeight + 5;
        // 邮箱
        CreateWindowW(L"STATIC", L"邮箱:", WS_CHILD | WS_VISIBLE, rightX + 15, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        hEmail = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
            inputX, y, inputWidth - 20, 26, hwnd, (HMENU)ID_EDIT_EMAIL, NULL, NULL);
        SendMessageW(hEmail, WM_SETFONT, (WPARAM)hFont, TRUE);

        y += rowHeight + 5;
        // SSH Key
        CreateWindowW(L"STATIC", L"SSH Key:", WS_CHILD | WS_VISIBLE, rightX + 15, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        // 使用 ComboBox 替代 Edit
        hSSH = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL, 
            inputX, y, inputWidth - 55, 200, hwnd, (HMENU)ID_COMBO_SSH, NULL, NULL);
        SendMessageW(hSSH, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, inputX + inputWidth - 50, y, 30, 26, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

        y += rowHeight + 20;
        // 按钮组
        int btnWidth = 90;
        hBtnSave = CreateWindowW(L"BUTTON", L"添加账户", WS_CHILD | WS_VISIBLE, rightX + 15, y, btnWidth, 32, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);
        SendMessageW(hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, rightX + 15 + btnWidth + 10, y, 70, 32, hwnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);
        SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
        ShowWindow(hBtnCancel, SW_HIDE);
        
        CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE, rightX + rightWidth - 70 - 20, y, 70, 32, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);

        y += 70; // 跳出 GroupBox
        
        // 全局操作区
        CreateWindowW(L"BUTTON", L"切换到选中账户", WS_CHILD | WS_VISIBLE, rightX, y, rightWidth, 38, hwnd, (HMENU)ID_BTN_SWITCH, NULL, NULL);
        
        // 顶部夜间模式切换按钮 (小图标)
        HWND hBtnTheme = CreateWindowW(L"BUTTON", L"🌙", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
            540, 10, 30, 30, hwnd, (HMENU)ID_BTN_THEME, NULL, NULL);
        SendMessageW(hBtnTheme, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 状态栏
        hStatus = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_SUNKEN, 
            0, 395, 590, 25, hwnd, (HMENU)ID_STATUS, NULL, NULL);
        SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 设置全局字体
        EnumChildWindows(hwnd, (WNDENUMPROC)(void*)SendMessageW, (LPARAM)WM_SETFONT);
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE); // ListBox 需要显式设置

        LoadConfig(&config);
        RefreshList();
        UpdateStatus();
        LoadSSHKeysToCombo();
        break;
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
        HWND hCtrl = (HWND)lParam;
        int id = GetDlgCtrlID(hCtrl);

        if (id == ID_LBL_DETAILS) {
            // GroupBox 标题特殊处理：必须使用不透明背景以遮挡 GroupBox 线条
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
            DrawButton(pDIS);
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
            OPENFILENAMEW ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = buffer;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"All Files\0*.*\0Key Files\0id_*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(hSSH, buffer);
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
                ShowMessage(hwnd, L"用户名和邮箱不能为空", L"错误");
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
                ShowMessage(hwnd, L"账户更新成功", L"成功");
            } else {
                // 新增
                if (config.account_count < MAX_ACCOUNTS) {
                    Account* acc = &config.accounts[config.account_count];
                    snprintf(acc->id, ID_LEN, "%lld", (long long)time(NULL));
                    strcpy(acc->name, nameBuf);
                    strcpy(acc->email, emailBuf);
                    strcpy(acc->ssh_key_path, sshBuf);
                    config.account_count++;
                    ShowMessage(hwnd, L"账户添加成功", L"成功");
                }
            }
            SaveConfig(&config);
            ClearForm();
            RefreshList();
        }
        else if (id == ID_BTN_DELETE) {
            if (strlen(currentEditID) > 0) {
                // 使用 ShowMessage 的确认框逻辑比较复杂，因为 ShowMessage 是模态且只用于显示
                // 这里暂时保持 MessageBoxW 用于确认，或者我们可以扩展 ShowMessage 支持返回值
                // 为了简单，我们使用 MessageBoxW，但注意它可能不是居中的。
                // 如果用户非常介意，我们可以实现 ConfirmDialog。
                // 鉴于用户只抱怨了成功提示，我们暂时保留 MessageBoxW 用于删除确认，
                // 或者我们可以快速实现一个 ConfirmDialog。
                // 考虑到时间，我们先用 MessageBoxW，如果不满意再改。
                if (MessageBoxW(hwnd, L"确定要删除此账户吗？", L"确认", MB_YESNO | MB_ICONWARNING) == IDYES) {
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
                    ShowMessage(hwnd, msg, L"成功");
                } else {
                    ShowMessage(hwnd, L"切换失败，请检查Git环境", L"错误");
                }
            } else {
                ShowMessage(hwnd, L"请先选择一个账户", L"提示");
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
        CW_USEDEFAULT, CW_USEDEFAULT, 590, 460,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
