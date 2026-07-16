#include "ui_taskbar.h"
#include <string.h>
#include <wchar.h>

#define TASKBAR_IDENTITY_CLASS L"GitManagerTaskbarIdentity"
typedef UINT (WINAPI *PGetDpiForWindow)(HWND);

static UINT GetWindowDpiCompat(HWND window) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    PGetDpiForWindow getDpi = NULL;
    FARPROC proc = user32 ? GetProcAddress(user32, "GetDpiForWindow") : NULL;
    if (proc) memcpy(&getDpi, &proc, sizeof(getDpi));
    if (getDpi) return getDpi(window);

    HDC dc = GetDC(window);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(window, dc);
    return dpi > 0 ? (UINT)dpi : 96;
}

static int Scale(UINT dpi, int value) {
    return MulDiv(value, (int)dpi, 96);
}

static void EnsureFont(TaskbarIdentity* identity, UINT dpi) {
    if (!identity || (identity->font && identity->dpi == dpi)) return;
    HFONT font = CreateFontW(-MulDiv(9, (int)dpi, 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    if (!font) return;
    if (identity->font) DeleteObject(identity->font);
    identity->font = font;
    identity->dpi = dpi;
}

static LRESULT CALLBACK TaskbarIdentityProc(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
    TaskbarIdentity* identity = (TaskbarIdentity*)GetWindowLongPtrW(
        window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONUP:
        if (identity && identity->owner && identity->callback_message) {
            PostMessageW(identity->owner, identity->callback_message,
                         0, WM_LBUTTONUP);
        }
        return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        if (identity && identity->owner && identity->callback_message) {
            POINT cursor;
            if (message == WM_CONTEXTMENU && (short)LOWORD(lParam) != -1) {
                cursor.x = (short)LOWORD(lParam);
                cursor.y = (short)HIWORD(lParam);
            } else {
                GetCursorPos(&cursor);
            }
            PostMessageW(identity->owner, identity->callback_message,
                         MAKEWPARAM(cursor.x, cursor.y), WM_CONTEXTMENU);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

static BOOL RegisterTaskbarIdentityClass(void) {
    WNDCLASSW existing;
    HINSTANCE instance = GetModuleHandleW(NULL);
    if (GetClassInfoW(instance, TASKBAR_IDENTITY_CLASS, &existing)) return TRUE;
    WNDCLASSW windowClass = {0};
    windowClass.lpfnWndProc = TaskbarIdentityProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = TASKBAR_IDENTITY_CLASS;
    windowClass.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32649));
    return RegisterClassW(&windowClass) != 0;
}

static BOOL CALLBACK FindTrayChild(HWND child, LPARAM data) {
    wchar_t className[64];
    if (GetClassNameW(child, className, 64) > 0 &&
        wcscmp(className, L"TrayNotifyWnd") == 0) {
        *(HWND*)data = child;
        return FALSE;
    }
    return TRUE;
}

static HWND FindTaskbarNotificationArea(HWND taskbar) {
    if (!taskbar) return NULL;
    HWND found = NULL;
    EnumChildWindows(taskbar, FindTrayChild, (LPARAM)&found);
    return found;
}

typedef struct {
    RECT taskbar_rect;
    int anchor_left;
    int tolerance;
} TaskbarAnchorSearch;

static BOOL IsTaskbarLayoutContainer(const wchar_t* className) {
    return wcscmp(className, L"ReBarWindow32") == 0 ||
           wcscmp(className, L"MSTaskSwWClass") == 0 ||
           wcscmp(className, L"MSTaskListWClass") == 0 ||
           wcscmp(className, L"TaskbandHWND") == 0 ||
           wcscmp(className, L"TrayNotifyWnd") == 0;
}

static BOOL CALLBACK FindAdjacentTaskbarPanel(HWND child, LPARAM data) {
    TaskbarAnchorSearch* search = (TaskbarAnchorSearch*)data;
    if (!IsWindowVisible(child)) return TRUE;
    RECT rect;
    wchar_t className[64] = L"";
    if (!GetWindowRect(child, &rect) ||
        GetClassNameW(child, className, 64) <= 0 ||
        IsTaskbarLayoutContainer(className)) {
        return TRUE;
    }
    int overlapTop = rect.top > search->taskbar_rect.top
        ? rect.top : search->taskbar_rect.top;
    int overlapBottom = rect.bottom < search->taskbar_rect.bottom
        ? rect.bottom : search->taskbar_rect.bottom;
    if (overlapBottom <= overlapTop || rect.right - rect.left < 16) return TRUE;
    if (rect.left < search->anchor_left &&
        rect.right >= search->anchor_left - search->tolerance) {
        search->anchor_left = rect.left;
    }
    return TRUE;
}

static int FindAvailableAnchorLeft(HWND taskbar, const RECT* taskbarRect,
                                   int trayLeft, UINT dpi) {
    TaskbarAnchorSearch search = {*taskbarRect, trayLeft, Scale(dpi, 3)};
    for (int i = 0; i < 4; i++) {
        int previous = search.anchor_left;
        EnumChildWindows(taskbar, FindAdjacentTaskbarPanel, (LPARAM)&search);
        if (search.anchor_left == previous) break;
    }
    return search.anchor_left;
}

static void MeasureIdentity(TaskbarIdentity* identity, UINT dpi,
                            int* width, int* height) {
    *width = Scale(dpi, 150);
    *height = Scale(dpi, 32);
    HDC dc = GetDC(identity->window);
    if (!dc) return;
    HFONT oldFont = identity->font
        ? (HFONT)SelectObject(dc, identity->font) : NULL;
    SIZE textSize = {0};
    if (GetTextExtentPoint32W(dc, identity->text,
                              (int)wcslen(identity->text), &textSize)) {
        *width = textSize.cx + Scale(dpi, 24);
        if (*width < Scale(dpi, 150)) *width = Scale(dpi, 150);
        if (*width > Scale(dpi, 280)) *width = Scale(dpi, 280);
        *height = textSize.cy + Scale(dpi, 10);
    }
    if (oldFont) SelectObject(dc, oldFont);
    ReleaseDC(identity->window, dc);
}

static BOOL IsFullscreenWindowCoveringTaskbar(HWND overlay, HWND taskbar,
                                               const RECT* taskbarRect) {
    HMONITOR monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {0};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;

    RECT visibleTaskbar;
    if (!IntersectRect(&visibleTaskbar, taskbarRect, &info.rcMonitor) ||
        visibleTaskbar.bottom - visibleTaskbar.top < 3 ||
        visibleTaskbar.right - visibleTaskbar.left < 3) {
        return TRUE;
    }

    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == overlay || foreground == taskbar) return FALSE;
    wchar_t className[64] = L"";
    GetClassNameW(foreground, className, 64);
    if (wcscmp(className, L"Progman") == 0 ||
        wcscmp(className, L"WorkerW") == 0 ||
        wcscmp(className, L"Shell_TrayWnd") == 0) {
        return FALSE;
    }
    RECT foregroundRect;
    if (!GetWindowRect(foreground, &foregroundRect)) return FALSE;
    return foregroundRect.left <= info.rcMonitor.left &&
           foregroundRect.top <= info.rcMonitor.top &&
           foregroundRect.right >= info.rcMonitor.right &&
           foregroundRect.bottom >= info.rcMonitor.bottom;
}

static BOOL RenderTaskbarText(TaskbarIdentity* identity, int x, int y,
                              int width, int height) {
    if (!identity || !identity->window || width <= 0 || height <= 0) return FALSE;
    HDC screen = GetDC(NULL);
    HDC memory = screen ? CreateCompatibleDC(screen) : NULL;
    BITMAPINFO bitmapInfo = {0};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = NULL;
    HBITMAP bitmap = memory ? CreateDIBSection(memory, &bitmapInfo,
        DIB_RGB_COLORS, (void**)&pixels, NULL, 0) : NULL;
    if (!screen || !memory || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (screen) ReleaseDC(NULL, screen);
        return FALSE;
    }

    ZeroMemory(pixels, (size_t)width * (size_t)height * sizeof(DWORD));
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    HFONT oldFont = identity->font
        ? (HFONT)SelectObject(memory, identity->font) : NULL;
    SetBkColor(memory, RGB(0, 0, 0));
    SetTextColor(memory, RGB(255, 255, 255));
    SetBkMode(memory, OPAQUE);
    RECT textRect = {Scale(identity->dpi, 10), 0,
                     width - Scale(identity->dpi, 10), height};
    int drawnHeight = DrawTextW(memory, identity->text, -1, &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
        DT_NOPREFIX);
    if (oldFont) SelectObject(memory, oldFont);
    SelectObject(memory, oldBitmap);
    GdiFlush();
    if (drawnHeight <= 0) {
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(NULL, screen);
        return FALSE;
    }

    size_t pixelCount = (size_t)width * (size_t)height;
    BYTE* bytes = (BYTE*)pixels;
    for (size_t i = 0; i < pixelCount; i++) {
        BYTE blue = bytes[i * 4];
        BYTE green = bytes[i * 4 + 1];
        BYTE red = bytes[i * 4 + 2];
        BYTE coverage = red > green ? red : green;
        if (blue > coverage) coverage = blue;
        bytes[i * 4] = coverage;
        bytes[i * 4 + 1] = coverage;
        bytes[i * 4 + 2] = coverage;
        bytes[i * 4 + 3] = coverage ? coverage : 1;
    }

    POINT destination = {x, y};
    POINT source = {0, 0};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    oldBitmap = SelectObject(memory, bitmap);
    BOOL updated = UpdateLayeredWindow(identity->window, screen,
        &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
    return updated;
}

void TaskbarIdentityReposition(TaskbarIdentity* identity) {
    if (!identity || !identity->window || !identity->enabled) return;
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    RECT taskbarRect;
    if (!taskbar || !IsWindowVisible(taskbar) ||
        !GetWindowRect(taskbar, &taskbarRect)) {
        ShowWindow(identity->window, SW_HIDE);
        return;
    }

    UINT dpi = GetWindowDpiCompat(taskbar);
    EnsureFont(identity, dpi);
    int taskbarWidth = taskbarRect.right - taskbarRect.left;
    int taskbarHeight = taskbarRect.bottom - taskbarRect.top;
    if (taskbarWidth < Scale(dpi, 24) || taskbarHeight < Scale(dpi, 24)) {
        ShowWindow(identity->window, SW_HIDE);
        return;
    }
    if (taskbarWidth < taskbarHeight ||
        IsFullscreenWindowCoveringTaskbar(identity->window, taskbar,
                                          &taskbarRect)) {
        ShowWindow(identity->window, SW_HIDE);
        return;
    }
    identity->taskbar = taskbar;

    int contentWidth;
    int contentHeight;
    MeasureIdentity(identity, dpi, &contentWidth, &contentHeight);
    (void)contentHeight;
    HWND notificationArea = FindTaskbarNotificationArea(taskbar);
    RECT notificationRect;
    if (!notificationArea ||
        !GetWindowRect(notificationArea, &notificationRect)) {
        ShowWindow(identity->window, SW_HIDE);
        return;
    }
    int width = contentWidth;
    int height = taskbarHeight;
    int anchorLeft = FindAvailableAnchorLeft(
        taskbar, &taskbarRect, notificationRect.left, dpi);
    int x = anchorLeft - width - Scale(dpi, 8);
    int minimumX = taskbarRect.left + Scale(dpi, 16);
    if (x < minimumX) x = minimumX;
    int y = taskbarRect.top;
    if (!RenderTaskbarText(identity, x, y, width, height)) {
        ShowWindow(identity->window, SW_HIDE);
        return;
    }
    SetWindowPos(identity->window, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                 SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
}

void TaskbarIdentityInitialize(TaskbarIdentity* identity, HWND owner,
                               UINT callbackMessage) {
    if (!identity) return;
    ZeroMemory(identity, sizeof(*identity));
    identity->owner = owner;
    identity->callback_message = callbackMessage;
    wcscpy(identity->text, L"当前全局 Git 账号：未配置");
    identity->taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    if (!RegisterTaskbarIdentityClass()) return;
    identity->taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
    identity->window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        TASKBAR_IDENTITY_CLASS, L"", WS_POPUP,
        0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), identity);
}

void TaskbarIdentitySetEnabled(TaskbarIdentity* identity, BOOL enabled) {
    if (!identity) return;
    identity->enabled = enabled;
    if (enabled) TaskbarIdentityReposition(identity);
    else if (identity->window) ShowWindow(identity->window, SW_HIDE);
}

void TaskbarIdentityUpdate(TaskbarIdentity* identity, const wchar_t* name) {
    if (!identity) return;
    wchar_t text[320];
    _snwprintf(text, 319, L"当前全局 Git 账号：%ls",
               name && name[0] ? name : L"未配置");
    text[319] = L'\0';
    if (wcscmp(identity->text, text) != 0) {
        wcscpy(identity->text, text);
        if (identity->window) SetWindowTextW(identity->window, text);
    }
    if (identity->enabled) TaskbarIdentityReposition(identity);
}

void TaskbarIdentityRestoreAfterExplorerRestart(TaskbarIdentity* identity) {
    if (!identity) return;
    identity->taskbar = NULL;
    if (identity->enabled) TaskbarIdentityReposition(identity);
}

void TaskbarIdentityDestroy(TaskbarIdentity* identity) {
    if (!identity) return;
    if (identity->window) DestroyWindow(identity->window);
    if (identity->font) DeleteObject(identity->font);
    ZeroMemory(identity, sizeof(*identity));
}
