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

// Git 操作
void GetGlobalConfig(char* name, char* email);
int SetGlobalConfig(const char* name, const char* email, const char* sshKeyPath);
int GetSSHKeys(char keys[][PATH_LEN], int maxKeys);

#endif
