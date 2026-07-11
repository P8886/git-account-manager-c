#include "ui_draw.h"
#include <wchar.h>

// 控件 ID 定义在 main.c/ui_gen_key.c 中。绘制层只保留稳定的视觉映射，
// 新控件可直接调用 DrawOwnerDrawButtonVariant 显式指定样式。
#define UI_ID_BTN_BROWSE 105
#define UI_ID_BTN_SAVE 106
#define UI_ID_BTN_DELETE 107
#define UI_ID_BTN_SWITCH 108
#define UI_ID_BTN_THEME 111
#define UI_ID_BTN_MESSAGE_OK 201
#define UI_ID_HOST_DELETE_FIRST 300
#define UI_ID_HOST_DELETE_LAST 309
#define UI_ID_BTN_ADD_HOST 400
#define UI_ID_GEN_BTN_OK 1007

static const UI_PALETTE UI_LIGHT_PALETTE = {
    RGB(243, 243, 243), // windowBackground
    RGB(255, 255, 255), // surface
    RGB(249, 249, 249), // surfaceHover
    RGB(245, 245, 245), // surfacePressed
    RGB(224, 224, 224), // border
    RGB(31, 31, 31),    // textPrimary
    RGB(96, 96, 96),    // textSecondary
    RGB(255, 255, 255), // textOnAccent
    RGB(15, 108, 189),  // accent
    RGB(17, 94, 163),   // accentHover
    RGB(12, 74, 110),   // accentPressed
    RGB(229, 241, 251), // accentSoft
    RGB(196, 43, 28),   // danger
    RGB(168, 36, 24),   // dangerHover
    RGB(133, 30, 20),   // dangerPressed
    RGB(16, 124, 65),   // success
    RGB(157, 93, 0),    // warning
    RGB(235, 235, 235), // disabledBackground
    RGB(153, 153, 153), // disabledText
    RGB(0, 95, 184),    // focus
    RGB(232, 240, 247)  // selection
};

static const UI_PALETTE UI_DARK_PALETTE = {
    RGB(32, 32, 32),    // windowBackground
    RGB(43, 43, 43),    // surface
    RGB(50, 50, 50),    // surfaceHover
    RGB(56, 56, 56),    // surfacePressed
    RGB(62, 62, 62),    // border
    RGB(245, 245, 245), // textPrimary
    RGB(190, 190, 190), // textSecondary
    RGB(255, 255, 255), // textOnAccent
    RGB(96, 160, 211),  // accent
    RGB(117, 177, 225), // accentHover
    RGB(68, 131, 184),  // accentPressed
    RGB(38, 52, 64),    // accentSoft
    RGB(255, 153, 164), // danger
    RGB(255, 176, 185), // dangerHover
    RGB(222, 120, 132), // dangerPressed
    RGB(108, 203, 128), // success
    RGB(247, 194, 84),  // warning
    RGB(45, 45, 45),    // disabledBackground
    RGB(120, 120, 120), // disabledText
    RGB(96, 160, 211),  // focus
    RGB(47, 55, 62)     // selection
};

const UI_PALETTE* GetUiPalette(BOOL isDarkMode) {
    return isDarkMode ? &UI_DARK_PALETTE : &UI_LIGHT_PALETTE;
}

static int ScaleForDc(HDC hdc, int value) {
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    if (dpi <= 0) dpi = 96;
    return MulDiv(value, dpi, 96);
}

static LPCWSTR SafeText(LPCWSTR text) {
    return text ? text : L"";
}

static void DrawRoundedBox(HDC hdc, const RECT* rect, COLORREF fill,
                           COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom,
              radius, radius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void DrawRoundedOutline(HDC hdc, const RECT* rect, COLORREF color,
                               int radius) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom,
              radius, radius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void FillStatusDot(HDC hdc, int centerX, int centerY, int radius,
                          COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));

    Ellipse(hdc, centerX - radius, centerY - radius,
            centerX + radius + 1, centerY + radius + 1);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
}

static COLORREF GetIdentityStateColor(const UI_PALETTE* palette,
                                      UI_IDENTITY_STATE state) {
    if (state == UI_IDENTITY_ACTIVE) return palette->success;
    if (state == UI_IDENTITY_WARNING) return palette->warning;
    return palette->textSecondary;
}

UI_BUTTON_VARIANT GetButtonVariantForControlId(int controlId) {
    if (controlId == UI_ID_BTN_SWITCH ||
        controlId == UI_ID_BTN_MESSAGE_OK ||
        controlId == IDYES ||
        controlId == UI_ID_GEN_BTN_OK) {
        return UI_BUTTON_PRIMARY;
    }

    if (controlId == UI_ID_BTN_DELETE ||
        (controlId >= UI_ID_HOST_DELETE_FIRST &&
         controlId <= UI_ID_HOST_DELETE_LAST)) {
        return UI_BUTTON_DANGER;
    }

    if (controlId == UI_ID_BTN_BROWSE ||
        controlId == UI_ID_BTN_ADD_HOST) {
        return UI_BUTTON_ICON;
    }

    return UI_BUTTON_SECONDARY;
}

void DrawOwnerDrawButtonVariant(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode,
                                HBRUSH hBrushDark, HBRUSH hBrushLight,
                                UI_BUTTON_VARIANT variant) {
    if (!pDIS || !pDIS->hDC) return;

    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    const UI_PALETTE* palette = GetUiPalette(isDarkMode);
    BOOL isPressed = (pDIS->itemState & ODS_SELECTED) != 0;
    BOOL isDisabled = (pDIS->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    BOOL isFocused = (pDIS->itemState & ODS_FOCUS) != 0;
    BOOL isHot = FALSE;
#ifdef ODS_HOTLIGHT
    isHot = (pDIS->itemState & ODS_HOTLIGHT) != 0;
#endif
    COLORREF background = palette->surface;
    COLORREF border = palette->border;
    COLORREF text = palette->textPrimary;
    int radius = ScaleForDc(hdc, 4);
    int savedDc = SaveDC(hdc);

    HBRUSH parentBrush = isDarkMode ? hBrushDark : hBrushLight;
    if (parentBrush) {
        FillRect(hdc, &rc, parentBrush);
    } else {
        HBRUSH fallbackBrush = CreateSolidBrush(palette->windowBackground);
        FillRect(hdc, &rc, fallbackBrush);
        DeleteObject(fallbackBrush);
    }

    if (variant == UI_BUTTON_PRIMARY) {
        background = isPressed ? palette->accentPressed :
                     isHot ? palette->accentHover : palette->accent;
        border = background;
        text = palette->textOnAccent;
    } else if (variant == UI_BUTTON_DANGER) {
        background = isPressed ? palette->surfacePressed :
                     isHot ? palette->surfaceHover : palette->surface;
        border = isPressed ? palette->dangerPressed :
                 isHot ? palette->dangerHover : palette->danger;
        text = border;
    } else {
        background = isPressed ? palette->surfacePressed :
                     isHot ? palette->surfaceHover : palette->surface;
        if (variant == UI_BUTTON_ICON) {
            text = palette->accent;
        }
    }

    if (isDisabled) {
        background = palette->disabledBackground;
        border = palette->border;
        text = palette->disabledText;
    } else if ((pDIS->itemState & ODS_DEFAULT) != 0 &&
               variant == UI_BUTTON_SECONDARY) {
        border = palette->accent;
    }

    DrawRoundedBox(hdc, &rc, background, border, radius);

    if (isFocused && !isDisabled) {
        RECT focusRect = rc;
        int inset = ScaleForDc(hdc, 3);
        InflateRect(&focusRect, -inset, -inset);
        DrawRoundedOutline(hdc, &focusRect, palette->focus,
                           radius > inset ? radius - inset : 1);
    }

    wchar_t textBuffer[256];
    textBuffer[0] = L'\0';
    if (pDIS->hwndItem) {
        GetWindowTextW(pDIS->hwndItem, textBuffer,
                       (int)(sizeof(textBuffer) / sizeof(textBuffer[0])));
    }

    if (!isDisabled && wcscmp(textBuffer, L"☀️") == 0) {
        text = palette->warning;
    }

    HFONT font = pDIS->hwndItem
        ? (HFONT)SendMessageW(pDIS->hwndItem, WM_GETFONT, 0, 0)
        : NULL;
    if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);

    RECT textRect = rc;
    int horizontalPadding = ScaleForDc(hdc, variant == UI_BUTTON_ICON ? 4 : 10);
    InflateRect(&textRect, -horizontalPadding, 0);
    if (isPressed && !isDisabled) {
        OffsetRect(&textRect, ScaleForDc(hdc, 1), ScaleForDc(hdc, 1));
    }
    DrawTextW(hdc, textBuffer, -1, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (savedDc) RestoreDC(hdc, savedDc);
}

void DrawOwnerDrawButton(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode,
                         HBRUSH hBrushDark, HBRUSH hBrushLight) {
    int controlId = pDIS && pDIS->hwndItem ? GetDlgCtrlID(pDIS->hwndItem) : 0;
    DrawOwnerDrawButtonVariant(pDIS, isDarkMode, hBrushDark, hBrushLight,
                               GetButtonVariantForControlId(controlId));
}

void DrawOwnerDrawAccountListItem(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode,
                                  LPCWSTR name, LPCWSTR email,
                                  BOOL isCurrent) {
    if (!pDIS || !pDIS->hDC) return;

    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    const UI_PALETTE* palette = GetUiPalette(isDarkMode);
    BOOL isSelected = (pDIS->itemState & ODS_SELECTED) != 0;
    BOOL isDisabled = (pDIS->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    int savedDc = SaveDC(hdc);
    int padding = ScaleForDc(hdc, 12);
    int gap = ScaleForDc(hdc, 2);
    COLORREF itemBackground = isSelected ? palette->selection : palette->surface;
    HBRUSH surfaceBrush = CreateSolidBrush(palette->surface);

    FillRect(hdc, &rc, surfaceBrush);
    DeleteObject(surfaceBrush);

    RECT itemRect = rc;
    InflateRect(&itemRect, -ScaleForDc(hdc, 4), -ScaleForDc(hdc, 2));
    if (isSelected) {
        DrawRoundedBox(hdc, &itemRect, itemBackground, itemBackground,
                       ScaleForDc(hdc, 4));
    }

    HFONT font = pDIS->hwndItem
        ? (HFONT)SendMessageW(pDIS->hwndItem, WM_GETFONT, 0, 0)
        : NULL;
    if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    TEXTMETRICW metrics;
    GetTextMetricsW(hdc, &metrics);
    int lineHeight = metrics.tmHeight;
    int contentHeight = lineHeight * 2 + gap;
    int contentTop = itemRect.top + ((itemRect.bottom - itemRect.top) - contentHeight) / 2;
    if (contentTop < itemRect.top + ScaleForDc(hdc, 3)) {
        contentTop = itemRect.top + ScaleForDc(hdc, 3);
    }

    RECT nameRect = itemRect;
    nameRect.left += padding;
    nameRect.top = contentTop;
    nameRect.bottom = contentTop + lineHeight;
    nameRect.right -= padding;

    SIZE badgeTextSize = {0};
    GetTextExtentPoint32W(hdc, L"当前", 2, &badgeTextSize);
    int badgeWidth = badgeTextSize.cx + ScaleForDc(hdc, 12);
    int badgeHeight = lineHeight + ScaleForDc(hdc, 4);
    if (isCurrent && (rc.right - rc.left) >= ScaleForDc(hdc, 140)) {
        RECT badgeRect;
        badgeRect.right = itemRect.right - padding;
        badgeRect.left = badgeRect.right - badgeWidth;
        badgeRect.top = contentTop - ScaleForDc(hdc, 1);
        badgeRect.bottom = badgeRect.top + badgeHeight;
        DrawRoundedBox(hdc, &badgeRect, palette->accentSoft,
                       palette->accentSoft, badgeHeight);
        SetTextColor(hdc, palette->accent);
        DrawTextW(hdc, L"当前", -1, &badgeRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        nameRect.right = badgeRect.left - ScaleForDc(hdc, 6);
    }

    SetTextColor(hdc, isDisabled ? palette->disabledText : palette->textPrimary);
    DrawTextW(hdc, SafeText(name), -1, &nameRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
              DT_NOPREFIX);

    RECT emailRect = nameRect;
    emailRect.top = nameRect.bottom + gap;
    emailRect.bottom = emailRect.top + lineHeight;
    SetTextColor(hdc, isDisabled ? palette->disabledText : palette->textSecondary);
    DrawTextW(hdc, SafeText(email), -1, &emailRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
              DT_NOPREFIX);

    if ((pDIS->itemState & ODS_FOCUS) != 0) {
        DrawRoundedOutline(hdc, &itemRect, palette->focus,
                           ScaleForDc(hdc, 4));
    }

    if (savedDc) RestoreDC(hdc, savedDc);
}

void DrawRoundedIdentityCard(HDC hdc, const RECT* rect, BOOL isDarkMode,
                             HFONT font, LPCWSTR title, LPCWSTR detail,
                             UI_IDENTITY_STATE state) {
    if (!hdc || !rect) return;

    const UI_PALETTE* palette = GetUiPalette(isDarkMode);
    int savedDc = SaveDC(hdc);
    int radius = ScaleForDc(hdc, 8);
    int padding = ScaleForDc(hdc, 20);
    int dotRadius = ScaleForDc(hdc, 4);
    int dotCenterX = rect->left + padding + dotRadius;

    DrawRoundedBox(hdc, rect, palette->surface, palette->border, radius);

    if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    TEXTMETRICW metrics;
    GetTextMetricsW(hdc, &metrics);
    int lineHeight = metrics.tmHeight;
    BOOL hasDetail = detail && detail[0] != L'\0';
    int gap = hasDetail ? ScaleForDc(hdc, 2) : 0;
    int contentHeight = hasDetail ? lineHeight * 2 + gap : lineHeight;
    int contentTop = rect->top + ((rect->bottom - rect->top) - contentHeight) / 2;
    if (contentTop < rect->top + ScaleForDc(hdc, 2)) {
        contentTop = rect->top + ScaleForDc(hdc, 2);
    }
    int dotCenterY = hasDetail ? contentTop + lineHeight / 2
                               : rect->top + (rect->bottom - rect->top) / 2;

    FillStatusDot(hdc, dotCenterX, dotCenterY, dotRadius,
                  GetIdentityStateColor(palette, state));

    RECT textRect = *rect;
    textRect.left = dotCenterX + dotRadius + ScaleForDc(hdc, 8);
    textRect.right -= padding;
    textRect.top = contentTop;
    textRect.bottom = textRect.top + lineHeight;
    SetTextColor(hdc, palette->textPrimary);
    DrawTextW(hdc, SafeText(title), -1, &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
              DT_NOPREFIX);

    if (hasDetail) {
        textRect.top = textRect.bottom + gap;
        textRect.bottom = textRect.top + lineHeight;
        SetTextColor(hdc, palette->textSecondary);
        DrawTextW(hdc, detail, -1, &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    }

    if (savedDc) RestoreDC(hdc, savedDc);
}

void DrawOwnerDrawComboItem(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode) {
    if (!pDIS || !pDIS->hDC || !pDIS->hwndItem) return;

    const UI_PALETTE* palette = GetUiPalette(isDarkMode);
    BOOL isEditField = (pDIS->itemState & ODS_COMBOBOXEDIT) != 0;
    BOOL isSelected = (pDIS->itemState & ODS_SELECTED) != 0;
    COLORREF background = (!isEditField && isSelected)
        ? palette->selection : palette->surface;
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(pDIS->hDC, &pDIS->rcItem, brush);
    DeleteObject(brush);

    wchar_t text[1024] = L"";
    if (pDIS->itemID != (UINT)-1) {
        SendMessageW(pDIS->hwndItem, CB_GETLBTEXT, pDIS->itemID,
                     (LPARAM)text);
    } else {
        GetWindowTextW(pDIS->hwndItem, text,
                       (int)(sizeof(text) / sizeof(text[0])));
    }

    HFONT font = (HFONT)SendMessageW(pDIS->hwndItem, WM_GETFONT, 0, 0);
    if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int savedDc = SaveDC(pDIS->hDC);
    SelectObject(pDIS->hDC, font);
    SetBkMode(pDIS->hDC, TRANSPARENT);
    SetTextColor(pDIS->hDC, palette->textPrimary);
    RECT textRect = pDIS->rcItem;
    InflateRect(&textRect, -ScaleForDc(pDIS->hDC, 8), 0);
    DrawTextW(pDIS->hDC, text, -1, &textRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
              DT_NOPREFIX);
    if ((pDIS->itemState & ODS_FOCUS) != 0 && !isEditField) {
        RECT focusRect = pDIS->rcItem;
        InflateRect(&focusRect, -ScaleForDc(pDIS->hDC, 2),
                    -ScaleForDc(pDIS->hDC, 1));
        DrawRoundedOutline(pDIS->hDC, &focusRect, palette->focus,
                           ScaleForDc(pDIS->hDC, 3));
    }
    if (savedDc) RestoreDC(pDIS->hDC, savedDc);
}
