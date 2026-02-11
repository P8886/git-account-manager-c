#pragma once
#include <windows.h>

// 绘制自绘按钮
void DrawOwnerDrawButton(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight);
