#ifndef LOGIC_H
#define LOGIC_H

#include <windows.h>

#define MAX_ACCOUNTS 50
#define ID_LEN 64
#define NAME_LEN 256
#define EMAIL_LEN 256
#define PATH_LEN 1024
#define HOST_LEN 256

// 账户结构体
typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    char email[EMAIL_LEN];
    char ssh_key_path[PATH_LEN];
    char host_list[10][HOST_LEN];  // Git服务Host列表 (github.com, gitlab.com等)
    int host_count;                // Host数量
} Account;

// 配置结构体
typedef struct {
    Account accounts[MAX_ACCOUNTS];
    int account_count;
    char active_id[ID_LEN];
    BOOL show_identity_badge;
    BOOL show_taskbar_text;
    BOOL dark_mode;
} Config;

// 配置操作
void GetConfigDir(char* buffer, int size);
void LoadConfig(Config* config);
int SaveConfig(const Config* config);
void AutoImportGlobalIdentity(Config* config); // 首次使用时自动导入当前 Git 身份

// Git 操作
void GetGlobalConfig(char* name, char* email);
// sshKeyPath 仅为旧调用兼容参数；SSH 身份由受管 SSH config 区域负责。
int SetGlobalConfig(const char* name, const char* email, const char* sshKeyPath);
int GetSSHKeys(char keys[][PATH_LEN], int maxKeys);

// 输入验证
int ValidateSSHPrivateKey(const char* keyPath);
int ValidateSSHHost(const char* host);
int ValidateSSHHostList(const char hosts[][HOST_LEN], int hostCount);

// 最近一次逻辑层错误，返回 UTF-8 文本
const char* GetLogicErrorMessage(void);

// 生成 SSH 密钥
// name: 密钥名称 (文件名)
// email: 关联邮箱
// type: 加密类型 (rsa, ed25519)
// outPath: 输出生成的私钥完整路径
// 返回值: 0 失败, 1 成功
int GenerateSSHKey(const char* name, const char* email, const char* type, char* outPath);

// 旧版兼容入口：现在只生成密钥，不会提前修改 Git 或 SSH 配置。
// name: 密钥名称 (文件名)
// email: 关联邮箱
// type: 加密类型 (rsa, ed25519)
// outPath: 输出生成的私钥完整路径
// host: Git服务Host (例如 github.com, gitlab.com)
// 返回值: 0 失败, 1 成功
int GenerateSSHKeyAndUpdateConfig(const char* name, const char* email, const char* type, char* outPath, const char* host);

// 为现有的 SSH 密钥创建或更新 SSH config
// keyPath: SSH 密钥的完整路径 (如 C:\Users\Admin\.ssh\id_rsa)
// email: 关联邮箱 (用于注释)
// 返回值: 0 失败, 1 成功
int AddExistingKeyToSSHConfig(const char* keyPath, const char* email, const char* host);

// 旧版兼容入口：清理本程序管理的 SSH 配置区域。
// keyPath: SSH 密钥的完整路径
// email: 关联邮箱 (新版本不再使用，保留参数兼容性)
// keepHosts: 需要保留的hosts数组 (新版本不再使用)
// keepHostCount: 需要保留的host数量 (新版本不再使用)
// 返回值: 0 失败, 1 成功
int CleanupSSHConfigForKey(const char* keyPath, const char* email, const char* const* keepHosts, int keepHostCount);

// 清空 SSH config 中由本程序管理的区域，保留所有用户自定义内容。
// 返回值: 0 失败, 1 成功
int ClearAllManagedSSHConfig(void);

// 切换账号：替换本程序管理的区域并写入新账号 SSH 配置。
// keyPath: SSH 密钥的完整路径
// hosts: Git服务Host数组
// hostCount: Host数量
// 返回值: 0 失败, 1 成功
int SwitchAccountSSHConfig(const char* keyPath, const char hosts[][HOST_LEN], int hostCount);

// 为现有的 SSH 密钥创建或更新 SSH config，支持多个hosts
// keyPath: SSH 密钥的完整路径 (如 C:\Users\Admin\.ssh\id_rsa)
// email: 关联邮箱 (新版本不再使用，保留参数兼容性)
// hosts: Git服务Host数组
// hostCount: Host数量
// 返回值: 0 失败, 1 成功
int AddMultipleHostsToSSHConfig(const char* keyPath, const char* email, const char hosts[][HOST_LEN], int hostCount);

// 从 SSH config 中获取对应密钥的 Host
// keyPath: SSH 密钥的完整路径
// outHost: 输出的 Host 字符串
// maxLen: outHost 缓冲区大小
// 返回值: 0 失败, 1 成功
int GetHostFromSSHConfig(const char* keyPath, char* outHost, int maxLen);

// 原子应用账户身份与 SSH 配置；失败时回滚已写文件
int ApplyAccountSettings(const char* name, const char* email,
    const char* sshKeyPath, const char hosts[][HOST_LEN], int hostCount);

#endif
