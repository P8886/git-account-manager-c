#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
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
#define ID_HOST_COMBO_PREFIX 200  // 动态创建的host下拉框ID前缀
#define ID_HOST_DELETE_PREFIX 300 // 动态创建的host删除按钮ID前缀
#define ID_BTN_ADD_HOST 400       // 新增host按钮

// 全局变量
HWND hMainWnd;  // 主窗口句柄
HWND hList, hName, hEmail, hSSH, hHost, hLblHost, hStatus, hBtnSave, hBtnCancel, hBtnDelete, hBtnSwitch, hLblDetails, hBtnGenerate;
HWND hHostControls[20];  // 存储host控件的句柄数组（下拉框和删除按钮配对，最多10对 = 20个句柄）
int hHostControlCount = 1;  // 当前host控件的数量，默认为1（初始控件）
HWND hBtnAddHost;  // 新增host按钮
Config config;
char currentEditID[ID_LEN] = "";
BOOL isDarkMode = FALSE;
BOOL g_bIgnoreEditChange = FALSE;
HBRUSH hBrushDark, hBrushLight, hBrushControlDark;
HFONT hGlobalFont = NULL; // 全局字体句柄
float g_dpiScale = 1.0f;  // DPI 缩放比例 (2K屏幕通常为 1.25 或 1.5)
HANDLE g_hMutex = NULL;   // 单实例互斥体句柄

// 函数声明
void RepositionLowerControls();
void AddHostControl(const wchar_t* initialHost);
void RemoveHostControl(int index);
void ClearHostControls();
void UpdateAccountHosts(Account* acc);
void PopulateHostControls(Account* acc);

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

// 重新定位下方的按钮控件以适应新增的host控件
void RepositionLowerControls() {
    int rightX = DPI(25) + DPI(200) + DPI(25);  // 左侧列表宽度 + 间距
    int rightWidth = DPI(340);
    int ctrlH = DPI(26);
    int rowGap = DPI(16);  // 统一行间距
    int labelWidth = DPI(70);
    int inputX = rightX + labelWidth + DPI(10);
    int inputWidth = rightWidth - labelWidth - DPI(10);
    
    // 计算host控件区域的总高度
    // 初始位置：用户名、邮箱、SSH密钥、SSH Hosts标签（4行），然后是host控件
    int yBase = DPI(25); // 初始边距
    int initialCtrlsHeight = 4 * (ctrlH + rowGap); // 用户名、邮箱、SSH密钥、SSH Hosts标签（4行）
    int hostCtrlsHeight = (ctrlH + rowGap) * hHostControlCount; // 所有host控件的高度
    int totalCtrlsHeight = initialCtrlsHeight + hostCtrlsHeight;
    int buttonsY = yBase + totalCtrlsHeight;  // 按钮组的Y位置

    // 重新定位按钮组（直接使用已存储的全局句柄）
    HWND hStatus = GetDlgItem(hMainWnd, ID_STATUS);
    HWND hBtnTheme = GetDlgItem(hMainWnd, ID_BTN_THEME);
    HWND hListCtrl = GetDlgItem(hMainWnd, ID_LIST);
    
    int btnWidth = DPI(100);
    // 使用SWP_NOCOPYBITS避免旧的绘制内容残留
    if (hBtnSave) SetWindowPos(hBtnSave, NULL, rightX, buttonsY, btnWidth, ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    if (hBtnCancel) SetWindowPos(hBtnCancel, NULL, rightX + btnWidth + DPI(10), buttonsY, DPI(70), ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    if (hBtnDelete) SetWindowPos(hBtnDelete, NULL, rightX + rightWidth - DPI(80), buttonsY, DPI(80), ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);

    // 重新定位"切换到选中账户"按钮
    int switchBtnY = buttonsY + ctrlH + rowGap + DPI(4);
    if (hBtnSwitch) SetWindowPos(hBtnSwitch, NULL, rightX, switchBtnY, rightWidth, ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);

    // 重新定位状态栏
    RECT rcClient;
    GetClientRect(hMainWnd, &rcClient);
    int statusY = rcClient.bottom - DPI(26) - DPI(25); // 状态栏高度 + 底部边距
    int statusWidth = rcClient.right - DPI(25) * 2 - ctrlH - DPI(5);
    if (hStatus) SetWindowPos(hStatus, NULL, DPI(25), statusY, statusWidth, DPI(26), SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    if (hBtnTheme) SetWindowPos(hBtnTheme, NULL, DPI(25) + statusWidth + DPI(5), statusY, ctrlH, ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);

    // 重新定位列表高度以适应新的布局
    int listHeight = statusY - DPI(25) - DPI(15); // 从顶部边距到状态栏上方
    if (hListCtrl) SetWindowPos(hListCtrl, NULL, DPI(25), DPI(25), DPI(200), listHeight, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    
    // 重新定位第一个host控件和新增按钮
    if (hHostControlCount >= 1 && hHostControls[0] != NULL) {
        int firstComboY = yBase + initialCtrlsHeight;
        // 第一个下拉框宽度与其他下拉框一致
        SetWindowPos(hHostControls[0], NULL, inputX, firstComboY, inputWidth - DPI(35), ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
        // 新增按钮在第一个下拉框右侧
        if (hBtnAddHost) SetWindowPos(hBtnAddHost, NULL, inputX + inputWidth - DPI(30), firstComboY, DPI(26), ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    }
    
    // 重新定位动态host控件（从第二个控件开始，因为第一个已经在上面定位了）
    for (int i = 1; i < hHostControlCount; i++) {
        int comboY = yBase + initialCtrlsHeight + (ctrlH + rowGap) * i;
        if (hHostControls[i * 2] != NULL) {  // 下拉框
            SetWindowPos(hHostControls[i * 2], NULL, inputX, comboY, inputWidth - DPI(35), ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
        }
        if (hHostControls[i * 2 + 1] != NULL) {  // 删除按钮
            SetWindowPos(hHostControls[i * 2 + 1], NULL, inputX + inputWidth - DPI(30), comboY, DPI(26), ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
        }
    }

    // 强制完整重绘窗口，避免残留渲染问题
    SendMessage(hMainWnd, WM_SETREDRAW, FALSE, 0);  // 暂停重绘
    SendMessage(hMainWnd, WM_SETREDRAW, TRUE, 0);   // 恢复重绘
    RedrawWindow(hMainWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    UpdateWindow(hMainWnd);  // 强制立即重绘
    
    // 动态调整窗口高度
    // 计算所需的最小高度：按钮Y位置 + 两行按钮高度 + 状态栏高度 + 底部边距
    int requiredHeight = switchBtnY + ctrlH + DPI(8) + DPI(26) + DPI(25) + DPI(15);
    // 基础高度（无额外host时）
    int baseHeight = DPI(480);
    // 每增加一个host，高度增加一行
    int extraHosts = (hHostControlCount > 1) ? (hHostControlCount - 1) : 0;
    int newHeight = baseHeight + extraHosts * (ctrlH + rowGap);
    
    // 确保窗口高度足够显示所有内容
    if (requiredHeight > newHeight) {
        newHeight = requiredHeight;
    }
    
    // 获取当前窗口位置和大小
    RECT rcWnd;
    GetWindowRect(hMainWnd, &rcWnd);
    int currentHeight = rcWnd.bottom - rcWnd.top;
    
    // 如果高度变化超过一行，调整窗口大小
    if (abs(newHeight - currentHeight) > DPI(20)) {
        SetWindowPos(hMainWnd, NULL, 0, 0, rcWnd.right - rcWnd.left, newHeight, 
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOCOPYBITS);
        // 重新定位状态栏
        GetClientRect(hMainWnd, &rcClient);
        statusY = rcClient.bottom - DPI(26) - DPI(25);
        statusWidth = rcClient.right - DPI(25) * 2 - ctrlH - DPI(5);
        if (hStatus) SetWindowPos(hStatus, NULL, DPI(25), statusY, statusWidth, DPI(26), SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
        if (hBtnTheme) SetWindowPos(hBtnTheme, NULL, DPI(25) + statusWidth + DPI(5), statusY, ctrlH, ctrlH, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
        // 重新定位列表
        listHeight = statusY - DPI(25) - DPI(15);
        if (hListCtrl) SetWindowPos(hListCtrl, NULL, DPI(25), DPI(25), DPI(200), listHeight, SWP_NOZORDER | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
    }
}

// 添加一个host控件（下拉框+删除按钮）
void AddHostControl(const wchar_t* initialHost) {
    if (hHostControlCount >= 10) return;  // 限制最大数量，包括初始的1个
    
    // 使用全局变量来获取正确的坐标
    int rightX = DPI(25) + DPI(200) + DPI(25);  // 左侧列表宽度 + 间距
    int labelWidth = DPI(70);
    int inputX = rightX + labelWidth + DPI(10);
    int inputWidth = DPI(340) - labelWidth - DPI(10);
    int ctrlH = DPI(26);
    int rowGap = DPI(16);  // 行间距

    // 计算Host控件的Y位置 - 在SSH密钥和现有Host控件之后
    // 从顶部开始计算：用户名、邮箱、SSH密钥（3行），然后是SSH Hosts行
    int yBase = DPI(25); // 初始边距
    int initialCtrlsHeight = 3 * (ctrlH + rowGap); // 到SSH Hosts行之前的高度
    // 新控件的Y位置 = 初始高度 + 已有host控件数量 * 行高
    int hostY = yBase + initialCtrlsHeight + (ctrlH + rowGap) * hHostControlCount;

    // 创建下拉框 - 使用全局主窗口句柄，恢复为标准样式
    HWND hCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP, 
        inputX, hostY, inputWidth - DPI(35), ctrlH, hMainWnd, (HMENU)(INT_PTR)(ID_HOST_COMBO_PREFIX + hHostControlCount), NULL, NULL);
    SendMessage(hCombo, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)DPI(22));
    // 添加常见的Git服务
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"github.com");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"gitlab.com");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"gitee.com");
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"bitbucket.org");
    if (initialHost && wcslen(initialHost) > 0) {
        SetWindowTextW(hCombo, initialHost);
    }

    // 创建删除按钮 - 使用当前控件索引作为ID
    HWND hDeleteBtn = CreateWindowW(L"BUTTON", L"–", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 
        inputX + inputWidth - DPI(30), hostY, DPI(26), ctrlH, hMainWnd, (HMENU)(INT_PTR)(ID_HOST_DELETE_PREFIX + hHostControlCount), NULL, NULL);

    // 存储控件句柄 - 从第2个控件开始（索引2和3为第2个控件的下拉框和删除按钮）
    hHostControls[hHostControlCount * 2] = hCombo;
    hHostControls[hHostControlCount * 2 + 1] = hDeleteBtn;
    hHostControlCount++;

    // 重新定位下方的控件
    RepositionLowerControls();
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
void UpdateAccountHosts(Account* acc) {
    acc->host_count = 0;
    for (int i = 0; i < hHostControlCount && i < 10; i++) {
        if (hHostControls[i * 2] != NULL) {  // 下拉框存在
            wchar_t wHost[HOST_LEN];
            GetWindowTextW(hHostControls[i * 2], wHost, HOST_LEN);
            if (wcslen(wHost) > 0) {
                strcpy(acc->host_list[acc->host_count], WToU8(wHost));
                acc->host_count++;
            }
        }
    }
}

// 用账户中的hosts填充控件
void PopulateHostControls(Account* acc) {
    ClearHostControls();  // 清空现有控件，保留第一个初始控件

    // 设置第一个控件的内容
    if (acc->host_count > 0 && hHostControls[0] != NULL) {
        SetWindowTextW(hHostControls[0], U8ToW(acc->host_list[0]));
    }

    // 添加其余的hosts（从第二个开始）
    for (int i = 1; i < acc->host_count && i < 10; i++) {
        AddHostControl(U8ToW(acc->host_list[i])); // 这将创建带删除按钮的控件
    }
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
    ClearHostControls();  // 清空hosts控件
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
    // 设置全局主窗口句柄
    if (msg == WM_CREATE) {
        hMainWnd = hwnd;
    }
    
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

        y += ctrlH + rowGap;

        // Row 4: SSH ComboBox + 浏览按钮
        int browseBtnW = DPI(40);
        int comboW = rightWidth - browseBtnW - DPI(5);
        hSSH = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP, 
            rightX, y, comboW, DPI(200), hwnd, (HMENU)ID_COMBO_SSH, NULL, NULL);
        SendMessage(hSSH, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)DPI(22));
        CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 
            rightX + comboW + DPI(5), y, browseBtnW, ctrlH, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

        y += ctrlH + rowGap;

        // Row 5: SSH Hosts 标签 + 第一个host控件 + 新增按钮
        CreateWindowW(L"STATIC", L"SSH Hosts:", WS_CHILD | WS_VISIBLE, rightX, y + DPI(4), labelWidth, DPI(20), hwnd, NULL, NULL, NULL);
        
        // 创建第一个host控件（下拉框）
        HWND hCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP, 
            inputX, y, inputWidth - DPI(35), ctrlH, hwnd, (HMENU)(INT_PTR)(ID_HOST_COMBO_PREFIX + 0), NULL, NULL);
        SendMessage(hCombo, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)DPI(22));
        // 添加常见的Git服务
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"github.com");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"gitlab.com");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"gitee.com");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"bitbucket.org");
        // 存储第一个控件句柄
        hHostControls[0] = hCombo;
        hHostControls[1] = NULL; // 第一个控件没有删除按钮，但预留位置
        hHostControlCount = 1; // 初始有一个控件，但不显示删除按钮

        // "新增"按钮 (放在第一个下拉框的右侧)
        hBtnAddHost = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 
            inputX + inputWidth - DPI(30), y, DPI(26), ctrlH, hwnd, (HMENU)ID_BTN_ADD_HOST, NULL, NULL);

        y += ctrlH + rowGap;

        // Row 6+: 动态添加的Host控件从这里开始排布

        // Row: 添加/取消/删除 按钮组 - 通过RepositionLowerControls动态调整位置
        int btnWidth = DPI(100);
        hBtnSave = CreateWindowW(L"BUTTON", L"添加账户", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 0, 0, btnWidth, ctrlH, hwnd, (HMENU)ID_BTN_SAVE, NULL, NULL);
        hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 0, 0, DPI(70), ctrlH, hwnd, (HMENU)ID_BTN_CANCEL, NULL, NULL);
        ShowWindow(hBtnCancel, SW_HIDE);
        hBtnDelete = CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 0, 0, DPI(80), ctrlH, hwnd, (HMENU)ID_BTN_DELETE, NULL, NULL);

        // Row: 切换到选中账户 - 通过RepositionLowerControls动态调整位置
        hBtnSwitch = CreateWindowW(L"BUTTON", L"切换到选中账户", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 0, 0, rightWidth, ctrlH, hwnd, (HMENU)ID_BTN_SWITCH, NULL, NULL);

                                                        

                                                        // 初始定位所有控件

                                                        RepositionLowerControls();        // 状态栏和列表高度：根据窗口客户区大小计算，状态栏贴底
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int clientH = rcClient.bottom;
        int statusY = clientH - margin - ctrlH;
        int statusWidth = rcClient.right - margin * 2 - ctrlH - DPI(5); // 留出主题按钮空间
        hStatus = CreateWindowW(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | WS_BORDER, 
            margin, statusY, statusWidth, ctrlH, hwnd, (HMENU)ID_STATUS, NULL, NULL);

        // 夜间模式切换按钮
        CreateWindowW(L"BUTTON", L"🌙", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
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
                    // 这里我们暂时不设置hHost，因为现在使用动态控件来处理hosts
                } else {
                    // 兼容旧数据
                }

                strcpy(currentEditID, acc->id);
                SetWindowTextW(hBtnSave, L"更新账户");
                ShowWindow(hBtnCancel, SW_SHOW);
                PopulateHostControls(acc);  // 填充hosts控件
            }
        }
        else if (id >= ID_HOST_DELETE_PREFIX && id < ID_HOST_DELETE_PREFIX + 10) {
            // 修复：需要找到正确的控件索引，而不是直接使用ID减去前缀
            int index = -1;
            for (int i = 1; i < hHostControlCount; i++) {  // 从1开始，因为0是初始控件
                if (hHostControls[i * 2 + 1] != NULL && GetDlgCtrlID(hHostControls[i * 2 + 1]) == id) {
                    index = i;
                    break;
                }
            }
            if (index != -1) {
                // 先获取当前账户信息和SSH路径，以便更新SSH配置
                char currentSSHPath[PATH_LEN] = "";
                if (strlen(currentEditID) > 0) {
                    // 获取当前SSH路径
                    wchar_t wSSH[PATH_LEN];
                    GetWindowTextW(hSSH, wSSH, PATH_LEN);
                    strcpy(currentSSHPath, WToU8(wSSH));
                }
                
                // 执行删除控件操作
                RemoveHostControl(index);
                
                // 如果当前正在编辑账户，需要更新SSH配置
                if (strlen(currentEditID) > 0 && strlen(currentSSHPath) > 0) {
                    // 更新账户的hosts列表
                    Account* acc = NULL;
                    for (int i = 0; i < config.account_count; i++) {
                        if (strcmp(config.accounts[i].id, currentEditID) == 0) {
                            acc = &config.accounts[i];
                            break;
                        }
                    }
                    
                    if (acc != NULL) {
                        // 更新hosts列表
                        UpdateAccountHosts(acc);
                        
                        // 创建一个新hosts列表
                        const char* newHosts[10];
                        for (int j = 0; j < acc->host_count; j++) {
                            newHosts[j] = acc->host_list[j];
                        }
                        
                        // 清理与该密钥相关的旧配置，但保留其他仍在使用中的host配置
                        CleanupSSHConfigForKey(currentSSHPath, acc->email, newHosts, acc->host_count);
                        
                        // 重新添加当前控件中存在的hosts配置
                        if (acc->host_count > 0) {
                            AddMultipleHostsToSSHConfig(currentSSHPath, acc->email, acc->host_list, acc->host_count);
                        }
                    }
                }
            }
        }
        else if (id == ID_BTN_ADD_HOST) {
            AddHostControl(L"");  // 添加一个空的host控件
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
                     // 使用第一个host或默认值
                     if (config.accounts[accIdx].host_count > 0) {
                         strcpy(host, config.accounts[accIdx].host_list[0]);
                     } else {
                         strcpy(host, "github.com"); // 默认值
                     }
                 }
             } else {
                 // 如果没有选中账户，尝试从第一个host控件获取值
                 if (hHostControlCount > 0 && hHostControls[0] != NULL) {
                     wchar_t wHost[HOST_LEN];
                     GetWindowTextW(hHostControls[0], wHost, HOST_LEN);
                     if (wcslen(wHost) > 0) {
                         strcpy(host, WToU8(wHost));
                     }
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
            
            // 简单的邮箱格式验证 (包含 @ 符号和 .)
            char* atSign = strchr(emailBuf, '@');
            if (!atSign || !strchr(atSign, '.')) {
                ShowMessage(hwnd, L"请输入有效的邮箱格式 (例如: test@example.com)", L"错误", MB_OK);
                return 0;
            }

            if (strlen(currentEditID) > 0) {
                // 更新现有账户
                for (int i = 0; i < config.account_count; i++) {
                    if (strcmp(config.accounts[i].id, currentEditID) == 0) {
                        // 保存原始数据用于比较
                        char originalKeyPath[PATH_LEN];
                        strcpy(originalKeyPath, config.accounts[i].ssh_key_path);
                        int originalHostCount = config.accounts[i].host_count;
                        char originalHosts[10][HOST_LEN];
                        for (int j = 0; j < originalHostCount; j++) {
                            strcpy(originalHosts[j], config.accounts[i].host_list[j]);
                        }
                        
                        // 更新账户信息
                        strcpy(config.accounts[i].name, nameBuf);
                        strcpy(config.accounts[i].email, emailBuf);
                        strcpy(config.accounts[i].ssh_key_path, sshBuf);
                        
                        // 更新hosts列表
                        UpdateAccountHosts(&config.accounts[i]);
                        
                        // 创建一个新账户hosts的列表
                        const char* newHosts[10];
                        for (int j = 0; j < config.accounts[i].host_count; j++) {
                            newHosts[j] = config.accounts[i].host_list[j];
                        }
                        
                        // 如果SSH密钥路径或hosts发生变化，需要更新SSH配置
                        if (strcmp(originalKeyPath, config.accounts[i].ssh_key_path) != 0 || 
                            originalHostCount != config.accounts[i].host_count ||
                            memcmp(originalHosts, config.accounts[i].host_list, sizeof(originalHosts)) != 0) {
                            
                            // 清理旧的SSH配置（只清理与旧密钥相关的、但不在新hosts列表中的配置）
                            if (strlen(originalKeyPath) > 0) {
                                CleanupSSHConfigForKey(originalKeyPath, config.accounts[i].email, newHosts, config.accounts[i].host_count);
                            }
                            
                            // 添加新的配置
                            if (strlen(config.accounts[i].ssh_key_path) > 0 && config.accounts[i].host_count > 0) {
                                AddMultipleHostsToSSHConfig(config.accounts[i].ssh_key_path, config.accounts[i].email, config.accounts[i].host_list, config.accounts[i].host_count);
                            }
                        }
                        
                        break;
                    }
                }
                
                ShowMessage(hwnd, L"账户更新成功", L"成功", MB_OK);
            } else {
                // 新增账户
                if (config.account_count < MAX_ACCOUNTS) {
                    Account* acc = &config.accounts[config.account_count];
                    snprintf(acc->id, ID_LEN, "%lld", (long long)time(NULL));
                    strcpy(acc->name, nameBuf);
                    strcpy(acc->email, emailBuf);
                    strcpy(acc->ssh_key_path, sshBuf);
                    
                    // 更新hosts列表
                    UpdateAccountHosts(acc);
                    
                    // 为新账户添加SSH配置
                    if (strlen(sshBuf) > 0 && acc->host_count > 0) {
                        AddMultipleHostsToSSHConfig(sshBuf, emailBuf, acc->host_list, acc->host_count);
                    }
                    
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
    // 单实例检查：如果程序已在运行，激活现有窗口并退出
    g_hMutex = CreateMutexW(NULL, TRUE, L"GitAccountManagerC_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 程序已在运行，查找并激活现有窗口
        HWND hExistingWnd = FindWindowW(L"GitAccountManagerC", NULL);
        if (hExistingWnd) {
            // 如果窗口最小化，恢复它
            if (IsIconic(hExistingWnd)) {
                ShowWindow(hExistingWnd, SW_RESTORE);
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