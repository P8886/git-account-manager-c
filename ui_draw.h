#pragma once
#include <windows.h>

typedef struct {
    COLORREF windowBackground;
    COLORREF surface;
    COLORREF surfaceHover;
    COLORREF surfacePressed;
    COLORREF border;
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF textOnAccent;
    COLORREF accent;
    COLORREF accentHover;
    COLORREF accentPressed;
    COLORREF accentSoft;
    COLORREF danger;
    COLORREF dangerHover;
    COLORREF dangerPressed;
    COLORREF success;
    COLORREF warning;
    COLORREF disabledBackground;
    COLORREF disabledText;
    COLORREF focus;
    COLORREF selection;
} UI_PALETTE;

typedef enum {
    UI_BUTTON_SECONDARY = 0,
    UI_BUTTON_PRIMARY,
    UI_BUTTON_DANGER,
    UI_BUTTON_ICON
} UI_BUTTON_VARIANT;

typedef enum {
    UI_IDENTITY_NEUTRAL = 0,
    UI_IDENTITY_ACTIVE,
    UI_IDENTITY_WARNING
} UI_IDENTITY_STATE;

// 返回进程生命周期内有效的只读配色表。
const UI_PALETTE* GetUiPalette(BOOL isDarkMode);

// 根据当前项目的控件 ID 返回默认按钮样式。
UI_BUTTON_VARIANT GetButtonVariantForControlId(int controlId);

// 显式指定按钮样式的绘制入口。
void DrawOwnerDrawButtonVariant(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode,
                                HBRUSH hBrushDark, HBRUSH hBrushLight,
                                UI_BUTTON_VARIANT variant);

// 兼容现有调用；内部根据控件 ID 选择样式。
void DrawOwnerDrawButton(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode, HBRUSH hBrushDark, HBRUSH hBrushLight);

// 绘制两行账户列表项。列表建议使用 LBS_OWNERDRAWFIXED，项目高度建议为 52 DPI 像素。
void DrawOwnerDrawAccountListItem(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode,
                                  LPCWSTR name, LPCWSTR email, BOOL isCurrent);

// 绘制两行身份卡；detail 为空时 title 垂直居中。
void DrawRoundedIdentityCard(HDC hdc, const RECT* rect, BOOL isDarkMode,
                             HFONT font, LPCWSTR title, LPCWSTR detail,
                             UI_IDENTITY_STATE state);

// 绘制 ComboBox 的选中区域及下拉列表项。
void DrawOwnerDrawComboItem(LPDRAWITEMSTRUCT pDIS, BOOL isDarkMode);
