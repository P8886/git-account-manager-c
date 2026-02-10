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
#define ID_EDIT_SSH 104
#define ID_BTN_BROWSE 105
#define ID_BTN_SAVE 106
#define ID_BTN_DELETE 107
#define ID_BTN_SWITCH 108
#define ID_STATUS 109
#define ID_BTN_CANCEL 110
#define ID_BTN_THEME 111

// 全局变量
HWND hList, hName, hEmail, hSSH, hStatus, hBtnSave, hBtnCancel;
Config config;
char currentEditID[ID_LEN] = "";
BOOL isDarkMode = FALSE;
HBRUSH hBrushDark, hBrushLight;

// --- UTF-8 与 WideChar 转换辅助函数 ---

// 将 UTF-8 字符串转换为 WideChar (wchar_t*)
// 注意：返回的是静态缓冲区，非线程安全，仅用于单线程 UI 调用
wchar_t* U8ToW(const char* utf8) {
    static wchar_t buffer[2048];
    if (!utf8) return L"";
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buffer, 2048);
    return buffer;
}

// 将 WideChar 转换为 UTF-8
// 注意：返回的是静态缓冲区
char* WToU8(const wchar_t* wstr) {
    static char buffer[2048];
    if (!wstr) return "";
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buffer, 2048, NULL, NULL);
    return buffer;
}

// 刷新列表显示
void RefreshList() {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < config.account_count; i++) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s <%s>", config.accounts[i].name, config.accounts[i].email);
        
        // 高亮当前账户
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
    // 强制重绘子控件
    EnumChildWindows(hwnd, (WNDENUMPROC)(void*)InvalidateRect, (LPARAM)TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 创建字体
        HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

        // 初始化画刷
        hBrushDark = CreateSolidBrush(RGB(30, 30, 30));
        hBrushLight = GetSysColorBrush(COLOR_WINDOW);

        // 左侧列表
        hList = CreateWindowW(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
            10, 10, 220, 340, hwnd, (HMENU)ID_LIST, NULL, NULL);
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 右侧面板
        int x = 240, y = 10;
        CreateWindowW(L"STATIC", L"用户名:", WS_CHILD | WS_VISIBLE, x, y, 60, 20, hwnd, NULL, NULL, NULL);
        hName = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 70, y, 180, 25, hwnd, (HMENU)ID_EDIT_NAME, NULL, NULL);
        SendMessageW(hName, WM_SETFONT, (WPARAM)hFont, TRUE);

        y += 40;
        CreateWindowW(L"STATIC", L"邮箱:", WS_CHILD | WS_VISIBLE, x, y, 60, 20, hwnd, NULL, NULL, NULL);
        hEmail = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 70, y, 180, 25, hwnd, (HMENU)ID_EDIT_EMAIL, NULL, NULL);
        SendMessageW(hEmail, WM_SETFONT, (WPARAM)hFont, TRUE);

        y += 40;
        CreateWindowW(L"STATIC", L"SSH Key:", WS_CHILD | WS_VISIBLE, x, y, 60, 20, hwnd, NULL, NULL, NULL);
        hSSH = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 70, y, 140, 25, hwnd, (HMENU)ID_EDIT_SSH, NULL, NULL);
        SendMessageW(hSSH, WM_SETFONT, (WPARAM)hFont, TRUE);
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE, x + 215, y, 35, 25, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

        y += 50;
        hBtnSave = CreateWindowW(L"BUTTON", L"添加账户", WS_CHILD | WS_VISIBLE, x, y, 80, 30, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);
        hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE, x + 90, y, 60, 30, hwnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);
        ShowWindow(hBtnCancel, SW_HIDE);
        CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE, x + 160, y, 60, 30, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);

        y += 40;
        CreateWindowW(L"BUTTON", L"切换到此账户", WS_CHILD | WS_VISIBLE, x, y, 220, 30, hwnd, (HMENU)ID_BTN_SWITCH, NULL, NULL);
        
        y += 40;
        CreateWindowW(L"BUTTON", L"切换夜间模式", WS_CHILD | WS_VISIBLE, x, y, 220, 30, hwnd, (HMENU)ID_BTN_THEME, NULL, NULL);

        // 状态栏
        hStatus = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_SUNKEN, 0, 360, 500, 25, hwnd, (HMENU)ID_STATUS, NULL, NULL);
        SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 设置子控件字体
        EnumChildWindows(hwnd, (WNDENUMPROC)(void*)SendMessageW, (LPARAM)WM_SETFONT);
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE); // Ensure list has font

        LoadConfig(&config);
        RefreshList();
        UpdateStatus();
        break;
    }
    // 处理颜色消息以实现夜间模式
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        if (isDarkMode) {
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(50, 50, 50));
            return (LRESULT)CreateSolidBrush(RGB(50, 50, 50)); // 需注意内存泄露，实际应用应缓存 Brush
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        if (isDarkMode) {
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(30, 30, 30));
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
            ofn.lpstrFilter = L"All Files\0*.*\0Key Files\0id_rsa*\0";
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
            // WToU8 使用静态 buffer，需要复制出来
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
            if (strlen(currentEditID) > 0) {
                 int found = -1;
                for (int i = 0; i < config.account_count; i++) {
                    if (strcmp(config.accounts[i].id, currentEditID) == 0) {
                        found = i;
                        break;
                    }
                }
                if (found != -1) {
                    if (SetGlobalConfig(config.accounts[found].name, config.accounts[found].email, config.accounts[found].ssh_key_path)) {
                        strcpy(config.active_id, currentEditID);
                        SaveConfig(&config);
                        RefreshList();
                        UpdateStatus();
                        wchar_t msg[512];
                        swprintf(msg, 512, L"已切换到 %S", config.accounts[found].name); // %S for char* in swprintf
                        MessageBoxW(hwnd, msg, L"成功", MB_OK);
                    } else {
                        MessageBoxW(hwnd, L"切换失败，请检查Git环境", L"错误", MB_OK | MB_ICONERROR);
                    }
                }
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

    HWND hwnd = CreateWindowW(L"GitAccountManagerC", L"Git Account Manager (C Version)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 430,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
