#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <time.h>
#include "logic.h"

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

// 全局变量
HWND hList, hName, hEmail, hSSH, hStatus, hBtnSave, hBtnCancel;
Config config;
char currentEditID[ID_LEN] = "";
BOOL isDarkMode = FALSE;
HBRUSH hBrushDark, hBrushLight, hBrushControlDark;

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

// 应用主题颜色
void ApplyTheme(HWND hwnd) {
    InvalidateRect(hwnd, NULL, TRUE);
    EnumChildWindows(hwnd, (WNDENUMPROC)(void*)InvalidateRect, (LPARAM)TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        HFONT hBoldFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

        hBrushDark = CreateSolidBrush(RGB(30, 30, 30));
        hBrushControlDark = CreateSolidBrush(RGB(50, 50, 50));
        hBrushLight = GetSysColorBrush(COLOR_WINDOW);

        // 布局常量
        int margin = 15;
        int listWidth = 200;
        int rightX = margin + listWidth + margin;
        int rightWidth = 300;
        int rowHeight = 30;
        int labelWidth = 60;
        int inputX = rightX + labelWidth + 10;
        int inputWidth = rightWidth - labelWidth - 10;
        int y = margin;

        // 左侧列表
        hList = CreateWindowW(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            margin, y, listWidth, 340, hwnd, (HMENU)ID_LIST, NULL, NULL);
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 右侧 GroupBox (使用 Button 类实现)
        CreateWindowW(L"BUTTON", L"账户详情", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            rightX, y - 5, rightWidth, 240, hwnd, (HMENU)ID_GROUP_DETAILS, NULL, NULL);
        
        y += 25; // GroupBox 内部起始 Y

        // 用户名
        CreateWindowW(L"STATIC", L"用户名:", WS_CHILD | WS_VISIBLE, rightX + 10, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        hName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
            inputX, y, inputWidth - 10, 23, hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);
        SendMessageW(hName, WM_SETFONT, (WPARAM)hFont, TRUE);

        y += rowHeight + 5;
        // 邮箱
        CreateWindowW(L"STATIC", L"邮箱:", WS_CHILD | WS_VISIBLE, rightX + 10, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        hEmail = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
            inputX, y, inputWidth - 10, 23, hwnd, (HMENU)ID_EDIT_EMAIL, NULL, NULL);
        SendMessageW(hEmail, WM_SETFONT, (WPARAM)hFont, TRUE);

        y += rowHeight + 5;
        // SSH Key
        CreateWindowW(L"STATIC", L"SSH Key:", WS_CHILD | WS_VISIBLE, rightX + 10, y + 3, labelWidth, 20, hwnd, NULL, NULL, NULL);
        // 使用 ComboBox 替代 Edit
        hSSH = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL, 
            inputX, y, inputWidth - 45, 200, hwnd, (HMENU)ID_COMBO_SSH, NULL, NULL);
        SendMessageW(hSSH, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, inputX + inputWidth - 40, y, 30, 23, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

        y += rowHeight + 15;
        // 按钮组
        int btnWidth = 80;
        hBtnSave = CreateWindowW(L"BUTTON", L"添加账户", WS_CHILD | WS_VISIBLE, rightX + 10, y, btnWidth, 28, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);
        SendMessageW(hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, rightX + 10 + btnWidth + 10, y, 60, 28, hwnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);
        SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
        ShowWindow(hBtnCancel, SW_HIDE);
        
        CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE, rightX + rightWidth - 60 - 10, y, 60, 28, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);

        y += 60; // 跳出 GroupBox
        
        // 全局操作区
        CreateWindowW(L"BUTTON", L"切换到选中账户", WS_CHILD | WS_VISIBLE, rightX, y, rightWidth, 32, hwnd, (HMENU)ID_BTN_SWITCH, NULL, NULL);
        y += 40;
        CreateWindowW(L"BUTTON", L"切换夜间模式", WS_CHILD | WS_VISIBLE, rightX, y, rightWidth, 32, hwnd, (HMENU)ID_BTN_THEME, NULL, NULL);

        // 状态栏
        hStatus = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_SUNKEN, 
            0, 365, 560, 25, hwnd, (HMENU)ID_STATUS, NULL, NULL);
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
        if (isDarkMode) {
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (LRESULT)hBrushDark;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CTLCOLORBTN: {
        // 按钮颜色通常由系统绘制，Win32 标准按钮较难定制颜色，除非自绘。
        // 这里仅处理背景，实际效果有限。
        if (isDarkMode) {
            return (LRESULT)hBrushDark;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
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
                MessageBoxW(hwnd, L"用户名和邮箱不能为空", L"错误", MB_OK | MB_ICONERROR);
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
                MessageBoxW(hwnd, L"账户更新成功", L"成功", MB_OK);
            } else {
                // 新增
                if (config.account_count < MAX_ACCOUNTS) {
                    Account* acc = &config.accounts[config.account_count];
                    snprintf(acc->id, ID_LEN, "%lld", (long long)time(NULL));
                    strcpy(acc->name, nameBuf);
                    strcpy(acc->email, emailBuf);
                    strcpy(acc->ssh_key_path, sshBuf);
                    config.account_count++;
                    MessageBoxW(hwnd, L"账户添加成功", L"成功", MB_OK);
                }
            }
            SaveConfig(&config);
            ClearForm();
            RefreshList();
        }
        else if (id == ID_BTN_DELETE) {
            if (strlen(currentEditID) > 0) {
                if (MessageBoxW(hwnd, L"确定要删除此账户吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
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
                    swprintf(msg, 512, L"已切换到 %S", acc->name);
                    MessageBoxW(hwnd, msg, L"成功", MB_OK);
                } else {
                    MessageBoxW(hwnd, L"切换失败，请检查Git环境", L"错误", MB_OK | MB_ICONERROR);
                }
            } else {
                MessageBoxW(hwnd, L"请先选择一个账户", L"提示", MB_OK | MB_ICONINFORMATION);
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

    // 调整窗口大小
    HWND hwnd = CreateWindowW(L"GitAccountManagerC", L"Git Account Manager (C Version)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 430,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
