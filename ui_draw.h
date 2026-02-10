#pragma once
#include <windows.h>

// 绘制自绘按钮
void DrawOwnerDrawButton(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight);

// 绘制圆角边框 (用于模拟圆角输入框)
void DrawRoundedBorder(HDC hdc, RECT* rc, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight);
