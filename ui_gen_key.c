#include "shared.h"
#include "ui_draw.h"
#include "logic.h"
#include <stdio.h>
#include <time.h>

// Dialog Control IDs
#define ID_GEN_LBL_NAME 1001
#define ID_GEN_EDIT_NAME 1002
#define ID_GEN_LBL_EMAIL 1003
#define ID_GEN_EDIT_EMAIL 1004
#define ID_GEN_LBL_TYPE 1005
#define ID_GEN_COMBO_TYPE 1006
#define ID_GEN_BTN_OK 1007
#define ID_GEN_BTN_CANCEL 1008

// Structure to pass data to dialog
typedef struct {
    char defaultEmail[EMAIL_LEN];
    char outName[64];
    char outEmail[EMAIL_LEN];
    char outType[16];
    BOOL success;
} GenKeyDialogData;

// Dialog Procedure
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

        // 1. Filename
        CreateWindowW(L"STATIC", L"密钥文件名:", WS_CHILD | WS_VISIBLE, x, y+3, lblW, h, hwnd, (HMENU)ID_GEN_LBL_NAME, NULL, NULL);
        
        // Generate default name
        char defaultName[64];
        snprintf(defaultName, sizeof(defaultName), "id_ed25519_%lld", (long long)time(NULL));
        
        HWND hName = CreateWindowW(L"EDIT", U8ToW(defaultName), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 
            inputX, y, inputW, h, hwnd, (HMENU)ID_GEN_EDIT_NAME, NULL, NULL);

        y += h + 15;

        // 2. Email
        CreateWindowW(L"STATIC", L"关联邮箱:", WS_CHILD | WS_VISIBLE, x, y+3, lblW, h, hwnd, (HMENU)ID_GEN_LBL_EMAIL, NULL, NULL);
        CreateWindowW(L"EDIT", U8ToW(pData->defaultEmail), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 
            inputX, y, inputW, h, hwnd, (HMENU)ID_GEN_EDIT_EMAIL, NULL, NULL);

        y += h + 15;

        // 3. Type
        CreateWindowW(L"STATIC", L"加密类型:", WS_CHILD | WS_VISIBLE, x, y+3, lblW, h, hwnd, (HMENU)ID_GEN_LBL_TYPE, NULL, NULL);
        HWND hCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST, 
            inputX, y, inputW, 100, hwnd, (HMENU)ID_GEN_COMBO_TYPE, NULL, NULL);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"ed25519");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"rsa");
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0); // Default to ed25519

        y += h + 30;

        // Buttons
        int btnW = 100;
        int btnGap = 20;
        int totalBtnW = btnW * 2 + btnGap;
        int btnStart = (x + w + 20 - totalBtnW) / 2; // Center buttons

        HWND hBtnCancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
            btnStart, y, btnW, 32, hwnd, (HMENU)ID_GEN_BTN_CANCEL, NULL, NULL);

        HWND hBtnOk = CreateWindowW(L"BUTTON", L"生成", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON, 
            btnStart + btnW + btnGap, y, btnW, 32, hwnd, (HMENU)ID_GEN_BTN_OK, NULL, NULL);

        // Apply theme and fonts
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGlobalFont);
        SetTitleBarTheme(hwnd, isDarkMode);

        // Subclass edits for rounded border painting? 
        // Or just let main loop handle painting via WM_PAINT if we expose hName/hEmail
        // For simplicity in this dialog, we can skip custom rounded border on inputs OR implement it simply.
        // Given "optimization" request, maybe keep it simple standard controls for now, 
        // OR reuse DrawRoundedBorder in WM_PAINT if we track handles.
        // Let's rely on standard look for dialogs to save code, or simple border.
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_GEN_BTN_CANCEL) {
            pData->success = FALSE;
            DestroyWindow(hwnd);
        } else if (id == ID_GEN_BTN_OK) {
            // Validate and Save
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
            SetBkColor(hdc, COLOR_DARK_BG); // Or control bg
            if (msg == WM_CTLCOLORSTATIC) SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)hBrushDark; // For dialog bg
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
        
        // Draw rounded borders for Inputs
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

             // Inflate to draw border OUTSIDE the edit control
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

// Public function to show the dialog
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

    // Center dialog
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
