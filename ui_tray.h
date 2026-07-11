#pragma once

#include <windows.h>

#define WM_TRAY_IDENTITY (WM_APP + 27)

enum {
    TRAY_MENU_OPEN = 5101,
    TRAY_MENU_TASKBAR_TEXT,
    TRAY_MENU_EXIT
};

typedef struct {
    HWND owner;
    HICON icon;
    UINT taskbar_created_message;
    BOOL enabled;
    BOOL added;
    wchar_t tooltip[128];
    wchar_t current_identity[640];
} TrayIdentity;

void TrayIdentityInitialize(TrayIdentity* tray, HWND owner);
void TrayIdentitySetEnabled(TrayIdentity* tray, BOOL enabled);
void TrayIdentityUpdate(TrayIdentity* tray, const wchar_t* name,
                        const wchar_t* email);
int TrayIdentityShowContextMenu(TrayIdentity* tray, BOOL showTaskbarText,
                                int anchorX, int anchorY);
void TrayIdentityRestoreAfterExplorerRestart(TrayIdentity* tray);
void TrayIdentityDestroy(TrayIdentity* tray);
