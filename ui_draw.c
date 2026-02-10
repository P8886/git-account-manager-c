#include "ui_draw.h"
#include <stdio.h>

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
        border = RGB(80, 80, 80); // 较暗的边框
        text = RGB(240, 240, 240);
    } else {
        bg = isPressed ? RGB(200, 200, 200) : RGB(245, 245, 245); // 较亮的背景
        border = RGB(180, 180, 180);
        text = RGB(0, 0, 0);
    }
    
    // 3. 绘制圆角按钮
    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    
    // 使用稍大的半径以获得“更圆”的外观（匹配 Go 版本）
    // Go 版本看起来像 4-6px 半径。
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    
    // 4. 绘制文本
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);
    
    // 显式选择按钮的字体
    HFONT hFont = (HFONT)SendMessage(pDIS->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    wchar_t buf[256];
    GetWindowTextW(pDIS->hwndItem, buf, 256);
    DrawTextW(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // 清理
    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

// 绘制圆角边框 (用于模拟圆角输入框)
void DrawRoundedBorder(HDC hdc, RECT* rc, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight) {
    // 颜色
    COLORREF bg, border;
    if (isDarkMode) {
        bg = RGB(40, 40, 40); // 比背景稍亮
        border = RGB(80, 80, 80);
    } else {
        bg = RGB(255, 255, 255);
        border = RGB(180, 180, 180);
    }

    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    // 绘制圆角矩形
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, 8, 8);

    // 清理
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}
