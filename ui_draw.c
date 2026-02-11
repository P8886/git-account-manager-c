#include "ui_draw.h"
#include <stdio.h>
#include <wchar.h>

// 绘制自绘按钮
void DrawOwnerDrawButton(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight) {
    HDC hdc = pDIS->hDC;
    RECT rc = pDIS->rcItem;
    BOOL isPressed = pDIS->itemState & ODS_SELECTED;
    
    // 1. 用父窗口颜色填充背景以清除角落
    HBRUSH hBrushParent = isDarkMode ? hBrushDark : hBrushLight;
    FillRect(hdc, &rc, hBrushParent);
    
    // 2. 定义颜色
    COLORREF bg, border, text;
    if (isDarkMode) {
        bg = isPressed ? RGB(60, 60, 60) : RGB(45, 45, 45);
        border = RGB(80, 80, 80);
        text = RGB(240, 240, 240);
    } else {
        bg = isPressed ? RGB(200, 200, 200) : RGB(245, 245, 245);
        border = RGB(180, 180, 180);
        text = RGB(0, 0, 0);
    }
    
    // 3. 绘制圆角按钮
    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    
    // 4. 绘制文本
    SetBkMode(hdc, TRANSPARENT);
    
    wchar_t buf[256];
    GetWindowTextW(pDIS->hwndItem, buf, 256);

    // 检查是否为太阳图标 
    if (wcscmp(buf, L"☀️") == 0) {
        SetTextColor(hdc, RGB(255, 215, 0));
    } else {
        SetTextColor(hdc, text);
    }
    
    // 显式选择按钮的字体
    HFONT hFont = (HFONT)SendMessage(pDIS->hwndItem, WM_GETFONT, 0, 0);
    
    // 如果是太阳/月亮图标，使用更大的字体
    BOOL isIcon = (wcscmp(buf, L"☀️") == 0 || wcscmp(buf, L"🌙") == 0);
    HFONT hIconFont = NULL;
    HGDIOBJ oldFont = NULL;

    if (isIcon) {
        hIconFont = CreateFontW(24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Emoji");
        oldFont = SelectObject(hdc, hIconFont);
    } else {
        oldFont = SelectObject(hdc, hFont);
    }

    DrawTextW(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // 清理
    SelectObject(hdc, oldFont);
    if (hIconFont) {
        DeleteObject(hIconFont);
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}
