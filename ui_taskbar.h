#pragma once

#include <windows.h>

typedef struct {
    HWND window;
    HWND taskbar;
    HWND owner;
    HFONT font;
    UINT dpi;
    UINT callback_message;
    UINT taskbar_created_message;
    BOOL enabled;
    wchar_t text[320];
} TaskbarIdentity;

void TaskbarIdentityInitialize(TaskbarIdentity* identity, HWND owner,
                               UINT callbackMessage);
void TaskbarIdentitySetEnabled(TaskbarIdentity* identity, BOOL enabled);
void TaskbarIdentityUpdate(TaskbarIdentity* identity, const wchar_t* name);
void TaskbarIdentityReposition(TaskbarIdentity* identity);
void TaskbarIdentityRestoreAfterExplorerRestart(TaskbarIdentity* identity);
void TaskbarIdentityDestroy(TaskbarIdentity* identity);
