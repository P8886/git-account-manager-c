#ifndef LOGIC_H
#define LOGIC_H

#include <windows.h>

#define MAX_ACCOUNTS 50
#define ID_LEN 64
#define NAME_LEN 256
#define EMAIL_LEN 256
#define PATH_LEN 1024

// 账户结构体
typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    char email[EMAIL_LEN];
    char ssh_key_path[PATH_LEN];
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

#endif
