#pragma once
#include <windows.h>

// 显示生成密钥对话框
// owner: 父窗口
// defaultEmail: 默认填充的邮箱
// outPath: 生成成功后返回私钥路径
// 返回值: TRUE 成功, FALSE 失败/取消
BOOL ShowGenerateKeyDialog(HWND owner, const char* defaultEmail, char* outPath);
