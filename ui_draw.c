#include "ui_draw.h"
#include <stdio.h>

// 绘制自绘按钮
void DrawOwnerDrawButton(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    BOOL isPressed = pDIS->itemState & ODS_SELECTED;
    
    // 1. Fill background with parent window color to clean up corners
    HBRUSH hBrushParent = isDarkMode ? hBrushDark : hBrushLight;
    FillRect(hdc, &rc, hBrushParent);
    
    // 2. Define colors
    COLORREF bg, border, text;
    if (isDarkMode) {
        bg = isPressed ? RGB(60, 60, 60) : RGB(45, 45, 45);
        border = RGB(80, 80, 80); // Darker border
        text = RGB(240, 240, 240);
    } else {
        bg = isPressed ? RGB(200, 200, 200) : RGB(245, 245, 245); // Lighter background
        border = RGB(180, 180, 180);
        text = RGB(0, 0, 0);
    }
    
    // 3. Draw rounded button
    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    
    // Use slightly larger radius for "rounder" look (matches Go version)
    // Go version looks like 4-6px radius.
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    
    // 4. Draw Text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);
    
    // Explicitly select the button's font
    HFONT hFont = (HFONT)SendMessage(pDIS->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    wchar_t buf[256];
    GetWindowTextW(pDIS->hwndItem, buf, 256);
    DrawTextW(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // Cleanup
    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

// 绘制圆角边框 (用于模拟圆角输入框)
void DrawRoundedBorder(HDC hdc, RECT* rc, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight) {
    // Colors
    COLORREF bg, border;
    if (isDarkMode) {
        bg = RGB(40, 40, 40); // Slightly lighter than background
        border = RGB(80, 80, 80);
    } else {
        bg = RGB(255, 255, 255);
        border = RGB(180, 180, 180);
    }

    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    // Draw rounded rectangle
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, 8, 8);

    // Cleanup
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}
