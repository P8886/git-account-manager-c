#include "shared.h"
#include "ui_draw.h"
#include "logic.h"
#include <stdio.h>
#include <time.h>

// 对话框控件 ID
#define ID_GEN_LBL_NAME 1001
#define ID_GEN_EDIT_NAME 1002
#define ID_GEN_LBL_EMAIL 1003
#define ID_GEN_EDIT_EMAIL 1004
#define ID_GEN_LBL_TYPE 1005
#define ID_GEN_COMBO_TYPE 1006
#define ID_GEN_BTN_OK 1007
#define ID_GEN_BTN_CANCEL 1008

// 传递给对话框的数据结构
typedef struct {
    char defaultEmail[EMAIL_LEN];
    char outName[64];
    char outEmail[EMAIL_LEN];
    char outType[16];
    BOOL success;
} GenKeyDialogData;

// 对话框过程
LRESULT CALLBACK GenKeyDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static GenKeyDialogData* pData = NULL;
    static HBRUSH hBrushGenControlDark = NULL;

    switch (msg) {
    case WM_CREATE: {
        if (!hBrushGenControlDark) hBrushGenControlDark = CreateSolidBrush(RGB(40, 40, 40));

        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pData = (GenKeyDialogData*)pCreate->lpCreateParams;
        
        int x = 20, y = 20, w = 340, h = 25;
        int lblW = 80;
        int inputX = x + lblW + 10;
        int inputW = w - lblW - 10;

        // 1. 密钥文件名
        CreateWindowW(L"STATIC", L"密钥文件名:", WS_CHILD | WS_VISIBLE, x, y+3, lblW, h, hwnd, (HMENU)ID_GEN_LBL_NAME, NULL, NULL);
        
        // 生成默认名称
        char defaultName[64];
        snprintf(defaultName, sizeof(defaultName), "id_ed25519_%lld", (long long)time(NULL));
        
        HWND hName = CreateWindowW(L"EDIT", U8ToW(defaultName), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 
            inputX, y, inputW, h, hwnd, (HMENU)ID_GEN_EDIT_NAME, NULL, NULL);

        y += h + 15;

        // 2. 关联邮箱
        CreateWindowW(L"STATIC", L"关联邮箱:", WS_CHILD | WS_VISIBLE, x, y+3, lblW, h, hwnd, (HMENU)ID_GEN_LBL_EMAIL, NULL, NULL);
        CreateWindowW(L"EDIT", U8ToW(pData->defaultEmail), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 
            inputX, y, inputW, h, hwnd, (HMENU)ID_GEN_EDIT_EMAIL, NULL, NULL);

        y += h + 15;

        // 3. 加密类型
        CreateWindowW(L"STATIC", L"加密类型:", WS_CHILD | WS_VISIBLE, x, y+3, lblW, h, hwnd, (HMENU)ID_GEN_LBL_TYPE, NULL, NULL);
        HWND hCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST, 
            inputX, y, inputW, 100, hwnd, (HMENU)ID_GEN_COMBO_TYPE, NULL, NULL);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"ed25519");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"rsa");
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0); // 默认选中 ed25519

        y += h + 30;

        // 按钮
        int btnW = 100;
        int btnGap = 20;
        int totalBtnW = btnW * 2 + btnGap;
        int btnStart = (x + w + 20 - totalBtnW) / 2; // 居中按钮

        HWND hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
            btnStart, y, btnW, 32, hwnd, (HMENU)ID_GEN_BTN_CANCEL, NULL, NULL);

        HWND hBtnOk = CreateWindowW(L"BUTTON", L"生成", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON, 
            btnStart + btnW + btnGap, y, btnW, 32, hwnd, (HMENU)ID_GEN_BTN_OK, NULL, NULL);

        // 应用主题和字体
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGlobalFont);
        SetTitleBarTheme(hwnd, isDarkMode);

        // 此次未子类化 Edit 控件进行圆角绘制，依靠 WM_PAINT 绘制边框
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_GEN_BTN_CANCEL) {
            pData->success = FALSE;
            DestroyWindow(hwnd);
        } else if (id == ID_GEN_BTN_OK) {
            // 验证并保存
            HWND hName = GetDlgItem(hwnd, ID_GEN_EDIT_NAME);
            HWND hEmail = GetDlgItem(hwnd, ID_GEN_EDIT_EMAIL);
            HWND hCombo = GetDlgItem(hwnd, ID_GEN_COMBO_TYPE);

            wchar_t wName[64], wEmail[EMAIL_LEN], wType[16];
            GetWindowTextW(hName, wName, 64);
            GetWindowTextW(hEmail, wEmail, EMAIL_LEN);
            GetWindowTextW(hCombo, wType, 16);

            strcpy(pData->outName, WToU8(wName));
            strcpy(pData->outEmail, WToU8(wEmail));
            strcpy(pData->outType, WToU8(wType));

            if (strlen(pData->outName) == 0) {
                ShowMessage(hwnd, L"请填写文件名", L"提示", MB_OK);
                return 0;
            }

            pData->success = TRUE;
            DestroyWindow(hwnd);
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        if (isDarkMode) {
            SetTextColor(hdc, COLOR_DARK_TEXT);
            if (msg == WM_CTLCOLOREDIT) {
                SetBkColor(hdc, RGB(40, 40, 40));
                return (LRESULT)hBrushGenControlDark;
            }
            SetBkColor(hdc, COLOR_DARK_BG); // 或控件背景
            if (msg == WM_CTLCOLORSTATIC) SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)hBrushDark; // 对话框背景
        }
        if (msg == WM_CTLCOLORSTATIC) {
             SetBkMode(hdc, TRANSPARENT);
             return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        // 为输入框绘制圆角边框
        HWND hName = GetDlgItem(hwnd, ID_GEN_EDIT_NAME);
        HWND hEmail = GetDlgItem(hwnd, ID_GEN_EDIT_EMAIL);
        
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

             // 向外扩展以在 Edit 控件外部绘制边框
             InflateRect(&rcName, 3, 3);
             InflateRect(&rcEmail, 3, 3);
             
             DrawRoundedBorder(hdc, &rcName, isDarkMode, hBrushGenControlDark, GetSysColorBrush(COLOR_WINDOW));
             DrawRoundedBorder(hdc, &rcEmail, isDarkMode, hBrushGenControlDark, GetSysColorBrush(COLOR_WINDOW));
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    
    case WM_ERASEBKGND: {
        if (isDarkMode) {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, hBrushDark);
            return 1;
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawOwnerDrawButton(pDIS, isDarkMode, hBrushDark, hBrushLight);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        pData->success = FALSE;
        DestroyWindow(hwnd);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 显示生成密钥对话框的公共函数
BOOL ShowGenerateKeyDialog(HWND owner, const char* defaultEmail, char* outPath) {
    GenKeyDialogData data = {0};
    strcpy(data.defaultEmail, defaultEmail);
    data.success = FALSE;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = GenKeyDlgProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GitManagerGenKeyDlg";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    // 居中显示对话框
    RECT rcOwner;
    GetWindowRect(owner, &rcOwner);
    int w = 400, h = 220;
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;

    HWND hDlg = CreateWindowW(L"GitManagerGenKeyDlg", L"生成 SSH 密钥", 
        WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_BORDER,
        x, y, w, h, owner, NULL, GetModuleHandle(NULL), &data);

    EnableWindow(owner, FALSE);

    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            data.success = FALSE;
            DestroyWindow(hDlg);
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);

    if (data.success) {
        if (GenerateSSHKey(data.outName, data.outEmail, data.outType, outPath)) {
            return TRUE;
        } else {
            ShowMessage(owner, L"密钥生成失败或已存在", L"错误", MB_OK);
            return FALSE;
        }
    }
    return FALSE;
}