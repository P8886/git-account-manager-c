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
    char git_host[HOST_LEN];  // Git服务Host (github.com, gitlab.com等)
} Account;

// 配置结构体
typedef struct {
    Account accounts[MAX_ACCOUNTS];
    int account_count;
    char active_id[ID_LEN];
} Config;

// 配置操作
void GetConfigDir(char* buffer, int size);
void LoadConfig(Config* config);
void SaveConfig(Config* config);
void AutoImportGlobalIdentity(Config* config); // 首次使用时自动导入当前 Git 身份

// Git 操作
void GetGlobalConfig(char* name, char* email);
int SetGlobalConfig(const char* name, const char* email, const char* sshKeyPath);
int GetSSHKeys(char keys[][PATH_LEN], int maxKeys);

// 生成 SSH 密钥
// name: 密钥名称 (文件名)
// email: 关联邮箱
// type: 加密类型 (rsa, ed25519)
// outPath: 输出生成的私钥完整路径
// 返回值: 0 失败, 1 成功
int GenerateSSHKey(const char* name, const char* email, const char* type, char* outPath);

// 生成 SSH 密钥并自动添加到 SSH config
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

// 从 SSH config 中获取对应密钥的 Host
// keyPath: SSH 密钥的完整路径
// outHost: 输出的 Host 字符串
// maxLen: outHost 缓冲区大小
// 返回值: 0 失败, 1 成功
int GetHostFromSSHConfig(const char* keyPath, char* outHost, int maxLen);

#endif
