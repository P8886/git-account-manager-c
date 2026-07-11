#include "ui_tray.h"
#include "resource.h"
#include <shellapi.h>
#include <wchar.h>

#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4
#endif

static HICON LoadApplicationIcon(void) {
    return (HICON)LoadImageW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(APP_ICON_ID), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
}

static void FillNotifyData(const TrayIdentity* tray, NOTIFYICONDATAW* data) {
    ZeroMemory(data, sizeof(*data));
    data->cbSize = sizeof(*data);
    data->hWnd = tray->owner;
    data->uID = 1;
    data->uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
#ifdef NIF_SHOWTIP
    data->uFlags |= NIF_SHOWTIP;
#endif
    data->uCallbackMessage = WM_TRAY_IDENTITY;
    data->hIcon = tray->icon;
    wcsncpy(data->szTip, tray->tooltip,
            sizeof(data->szTip) / sizeof(data->szTip[0]) - 1);
}

static void AddTrayIcon(TrayIdentity* tray) {
    if (!tray || !tray->enabled || tray->added ||
        !tray->owner || !tray->icon) return;
    NOTIFYICONDATAW data;
    FillNotifyData(tray, &data);
    if (Shell_NotifyIconW(NIM_ADD, &data)) {
        tray->added = TRUE;
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

static void RemoveTrayIcon(TrayIdentity* tray) {
    if (!tray || !tray->added) return;
    NOTIFYICONDATAW data;
    FillNotifyData(tray, &data);
    Shell_NotifyIconW(NIM_DELETE, &data);
    tray->added = FALSE;
}

void TrayIdentityInitialize(TrayIdentity* tray, HWND owner) {
    if (!tray) return;
    ZeroMemory(tray, sizeof(*tray));
    tray->owner = owner;
    tray->taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    wcscpy(tray->tooltip, L"当前全局 Git 账号：未配置");
    wcscpy(tray->current_identity, L"当前：未配置");
    tray->icon = LoadApplicationIcon();
}

void TrayIdentitySetEnabled(TrayIdentity* tray, BOOL enabled) {
    if (!tray) return;
    tray->enabled = enabled;
    if (enabled) AddTrayIcon(tray);
    else RemoveTrayIcon(tray);
}

void TrayIdentityUpdate(TrayIdentity* tray, const wchar_t* name,
                        const wchar_t* email) {
    if (!tray) return;
    wchar_t tooltip[128];
    wchar_t currentIdentity[640];
    if (name && name[0] && email && email[0]) {
        _snwprintf(tooltip, 127, L"当前全局 Git 账号：%ls <%ls>", name, email);
        _snwprintf(currentIdentity, 639, L"当前：%ls <%ls>", name, email);
    } else if (name && name[0]) {
        _snwprintf(tooltip, 127, L"当前全局 Git 账号：%ls", name);
        _snwprintf(currentIdentity, 639, L"当前：%ls", name);
    } else {
        wcscpy(tooltip, L"当前全局 Git 账号：未配置");
        wcscpy(currentIdentity, L"当前：未配置");
    }
    tooltip[127] = L'\0';
    currentIdentity[639] = L'\0';
    if (wcscmp(tray->tooltip, tooltip) == 0) {
        AddTrayIcon(tray);
        return;
    }
    wcscpy(tray->tooltip, tooltip);
    wcscpy(tray->current_identity, currentIdentity);
    if (tray->added) {
        NOTIFYICONDATAW data;
        FillNotifyData(tray, &data);
        if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
            tray->added = FALSE;
            AddTrayIcon(tray);
        }
    } else {
        AddTrayIcon(tray);
    }
}

int TrayIdentityShowContextMenu(TrayIdentity* tray, BOOL showTaskbarText,
                                int anchorX, int anchorY) {
    if (!tray || !tray->owner) return 0;
    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;
    wchar_t menuIdentity[1280];
    int writeIndex = 0;
    for (int i = 0; tray->current_identity[i] && writeIndex < 1278; i++) {
        wchar_t value = tray->current_identity[i];
        if (value == L'\r' || value == L'\n' || value == L'\t') value = L' ';
        menuIdentity[writeIndex++] = value;
        if (value == L'&' && writeIndex < 1279) menuIdentity[writeIndex++] = L'&';
    }
    menuIdentity[writeIndex] = L'\0';
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, menuIdentity);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, TRAY_MENU_OPEN, L"打开 Git 账户管理");
    AppendMenuW(menu, MF_STRING | (showTaskbarText ? MF_CHECKED : 0),
                TRAY_MENU_TASKBAR_TEXT, L"在任务栏显示账号文字");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, TRAY_MENU_EXIT, L"退出");

    POINT cursor = {anchorX, anchorY};
    if (anchorX < 0 || anchorY < 0) GetCursorPos(&cursor);
    SetForegroundWindow(tray->owner);
    int command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        cursor.x, cursor.y, 0, tray->owner, NULL);
    DestroyMenu(menu);
    PostMessageW(tray->owner, WM_NULL, 0, 0);
    return command;
}

void TrayIdentityRestoreAfterExplorerRestart(TrayIdentity* tray) {
    if (!tray) return;
    tray->added = FALSE;
    AddTrayIcon(tray);
}

void TrayIdentityDestroy(TrayIdentity* tray) {
    if (!tray) return;
    RemoveTrayIcon(tray);
    if (tray->icon) DestroyIcon(tray->icon);
    ZeroMemory(tray, sizeof(*tray));
}
