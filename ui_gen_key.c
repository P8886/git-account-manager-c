#include "shared.h"
#include "ui_draw.h"
#include "logic.h"
#include <stdio.h>
#include <time.h>

static BOOL WideToUtf8Buffer(const wchar_t* value, char* output, int capacity) {
    if (!value || !output || capacity <= 0) return FALSE;
    output[0] = 0;
    return WideCharToMultiByte(CP_UTF8, 0, value, -1, output, capacity,
                               NULL, NULL) > 0;
}

static BOOL Utf8ToWideBuffer(const char* value, wchar_t* output, int capacity) {
    if (!value || !output || capacity <= 0) return FALSE;
    output[0] = 0;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                               output, capacity) > 0;
}

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
    char host[HOST_LEN];
    char outName[64];
    char outEmail[EMAIL_LEN];
    char outType[16];
    BOOL success;
} GenKeyDialogData;

// 对话框过程
LRESULT CALLBACK GenKeyDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static GenKeyDialogData* pData = NULL;

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pData = (GenKeyDialogData*)pCreate->lpCreateParams;
        
        int x = DPI(24), y = DPI(24), w = DPI(392), h = DPI(32);
        int lblW = DPI(84);
        int inputX = x + lblW + DPI(12);
        int inputW = w - lblW - DPI(12);

        // 1. 密钥文件名
        CreateWindowW(L"STATIC", L"密钥文件名", WS_CHILD | WS_VISIBLE, x, y+DPI(6), lblW, h, hwnd, (HMENU)ID_GEN_LBL_NAME, NULL, NULL);
        
        // 生成默认名称
        char defaultName[64];
        snprintf(defaultName, sizeof(defaultName), "id_ed25519_%lld", (long long)time(NULL));
        
        CreateWindowW(L"EDIT", U8ToW(defaultName), WS_CHILD | WS_VISIBLE | WS_BORDER |
            ES_MULTILINE | ES_AUTOHSCROLL,
            inputX, y, inputW, h, hwnd, (HMENU)ID_GEN_EDIT_NAME, NULL, NULL);

        SendMessageW(GetDlgItem(hwnd, ID_GEN_EDIT_NAME), EM_SETLIMITTEXT, 63, 0);
        SendMessageW(GetDlgItem(hwnd, ID_GEN_EDIT_NAME), EM_SETMARGINS,
                     EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(DPI(8), DPI(8)));
        y += h + DPI(16);

        // 2. 关联邮箱
        CreateWindowW(L"STATIC", L"关联邮箱", WS_CHILD | WS_VISIBLE, x, y+DPI(6), lblW, h, hwnd, (HMENU)ID_GEN_LBL_EMAIL, NULL, NULL);
        CreateWindowW(L"EDIT", U8ToW(pData->defaultEmail), WS_CHILD | WS_VISIBLE | WS_BORDER |
            ES_MULTILINE | ES_AUTOHSCROLL,
            inputX, y, inputW, h, hwnd, (HMENU)ID_GEN_EDIT_EMAIL, NULL, NULL);

        SendMessageW(GetDlgItem(hwnd, ID_GEN_EDIT_EMAIL), EM_SETLIMITTEXT, EMAIL_LEN - 1, 0);
        SendMessageW(GetDlgItem(hwnd, ID_GEN_EDIT_EMAIL), EM_SETMARGINS,
                     EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(DPI(8), DPI(8)));
        y += h + DPI(16);

        // 3. 加密类型
        CreateWindowW(L"STATIC", L"加密类型", WS_CHILD | WS_VISIBLE, x, y+DPI(6), lblW, h, hwnd, (HMENU)ID_GEN_LBL_TYPE, NULL, NULL);
        HWND hCombo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
            inputX, y, inputW, DPI(100), hwnd, (HMENU)ID_GEN_COMBO_TYPE, NULL, NULL);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"ed25519");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"rsa");
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0); // 默认选中 ed25519
        MySetWindowTheme(GetDlgItem(hwnd, ID_GEN_EDIT_NAME), L"", L"");
        MySetWindowTheme(GetDlgItem(hwnd, ID_GEN_EDIT_EMAIL), L"", L"");
        MySetWindowTheme(hCombo, isDarkMode ? L"DarkMode_Explorer" : NULL, NULL);

        y += h + DPI(22);

        // 按钮
        int btnW = DPI(100);
        int btnGap = DPI(8);
        int totalBtnW = btnW * 2 + btnGap;
        int btnStart = x + w - totalBtnW;

        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 
            btnStart, y, btnW, h, hwnd, (HMENU)ID_GEN_BTN_CANCEL, NULL, NULL);

        CreateWindowW(L"BUTTON", L"生成", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON, 
            btnStart + btnW + btnGap, y, btnW, h, hwnd, (HMENU)ID_GEN_BTN_OK, NULL, NULL);

        // 应用主题和字体
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hGlobalFont);
        EnableVerticallyCenteredEdit(GetDlgItem(hwnd, ID_GEN_EDIT_NAME));
        EnableVerticallyCenteredEdit(GetDlgItem(hwnd, ID_GEN_EDIT_EMAIL));
        EnableModernComboBox(hCombo);
        SetComboBoxClosedHeight(hCombo, h);
        SetTitleBarTheme(hwnd, isDarkMode);
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

            if (!WideToUtf8Buffer(wName, pData->outName, sizeof(pData->outName)) ||
                !WideToUtf8Buffer(wEmail, pData->outEmail, sizeof(pData->outEmail)) ||
                !WideToUtf8Buffer(wType, pData->outType, sizeof(pData->outType))) {
                ShowMessage(hwnd, L"输入内容过长或包含无效字符", L"输入错误", MB_OK);
                return 0;
            }

            if (strlen(pData->outName) == 0) {
                ShowMessage(hwnd, L"请填写文件名", L"提示", MB_OK);
                return 0;
            }
            if (strlen(pData->outEmail) == 0) {
                ShowMessage(hwnd, L"请填写关联邮箱", L"提示", MB_OK);
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
        const UI_PALETTE* palette = GetUiPalette(isDarkMode);
        SetTextColor(hdc, palette->textPrimary);
        if (msg == WM_CTLCOLOREDIT) {
            SetBkColor(hdc, palette->surface);
            return (LRESULT)(isDarkMode ? hBrushControlDark : hBrushControlLight);
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)(isDarkMode ? hBrushDark : hBrushLight);
    }
    

    
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, isDarkMode ? hBrushDark : hBrushLight);
        return 1;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlType == ODT_BUTTON) {
            DrawOwnerDrawButton(pDIS, isDarkMode, hBrushDark, hBrushLight);
            return TRUE;
        }
        if (pDIS->CtlType == ODT_COMBOBOX) {
            DrawOwnerDrawComboItem(pDIS, isDarkMode);
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
BOOL ShowGenerateKeyDialog(HWND owner, const char* defaultEmail, const char* host, char* outPath) {
    GenKeyDialogData data = {0};
    strncpy(data.defaultEmail, defaultEmail ? defaultEmail : "", sizeof(data.defaultEmail) - 1);
    if (host && strlen(host) > 0) {
        strncpy(data.host, host, sizeof(data.host) - 1);
    } else {
        strcpy(data.host, "github.com");
    }
    data.success = FALSE;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = GenKeyDlgProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GitManagerGenKeyDlg";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);

    // 居中显示对话框 (DPI 缩放)
    RECT rcOwner;
    GetWindowRect(owner, &rcOwner);
    int w = DPI(440), h = DPI(260);
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
        if (GenerateSSHKeyAndUpdateConfig(data.outName, data.outEmail, data.outType, outPath, data.host)) {
            char pubPath[PATH_LEN + 5];
            snprintf(pubPath, sizeof(pubPath), "%s.pub", outPath);
            wchar_t widePubPath[PATH_LEN + 5];
            FILE* fp = NULL;
            if (Utf8ToWideBuffer(pubPath, widePubPath, PATH_LEN + 5)) {
                fp = _wfopen(widePubPath, L"rb");
            }
            if (fp) {
                char pubKey[4096] = {0};
                fread(pubKey, 1, sizeof(pubKey) - 1, fp);
                fclose(fp);
                // 去除尾部空白符
                int len = strlen(pubKey);
                while (len > 0 && (pubKey[len-1] == '\n' || pubKey[len-1] == '\r' || pubKey[len-1] == ' ')) {
                    pubKey[--len] = 0;
                }
                // 复制 UTF-8 公钥为 Unicode 文本
                if (len > 0 && OpenClipboard(owner)) {
                    BOOL copied = FALSE;
                    EmptyClipboard();
                    int wideLen = MultiByteToWideChar(CP_UTF8, 0, pubKey, -1, NULL, 0);
                    HGLOBAL hMem = wideLen > 0
                        ? GlobalAlloc(GMEM_MOVEABLE, (size_t)wideLen * sizeof(wchar_t)) : NULL;
                    wchar_t* pMem = hMem ? (wchar_t*)GlobalLock(hMem) : NULL;
                    if (pMem) {
                        MultiByteToWideChar(CP_UTF8, 0, pubKey, -1, pMem, wideLen);
                        GlobalUnlock(hMem);
                        if (SetClipboardData(CF_UNICODETEXT, hMem)) copied = TRUE;
                        else GlobalFree(hMem);
                    } else if (hMem) {
                        GlobalFree(hMem);
                    }
                    CloseClipboard();
                    ShowMessage(owner, copied
                        ? L"密钥已生成，公钥已复制到剪贴板。\n保存账户并点击切换后，SSH 配置才会生效。"
                        : L"密钥已生成，但公钥未能复制到剪贴板。\n保存账户并点击切换后生效。",
                        L"生成成功", MB_OK);
                } else {
                    ShowMessage(owner, L"密钥已生成。保存账户并点击切换后生效。", L"生成成功", MB_OK);
                }
            } else {
                ShowMessage(owner, L"密钥已生成，但未能读取公钥文件。", L"生成成功", MB_OK);
            }
            return TRUE;
        } else {
            wchar_t reason[512] = L"密钥生成失败";
            Utf8ToWideBuffer(GetLogicErrorMessage(), reason, 512);
            ShowMessage(owner, reason, L"生成失败", MB_OK);
            return FALSE;
        }
    }
    return FALSE;
}
