#define _CRT_SECURE_NO_WARNINGS
#include "logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>
#include <time.h>
// --- JSON 解析辅助函数 ---

typedef struct {
    const char* json;
    size_t pos;
} JsonParser;

void skip_whitespace(JsonParser* p) {
    while (p->json[p->pos] && (p->json[p->pos] == ' ' || p->json[p->pos] == '\t' || p->json[p->pos] == '\n' || p->json[p->pos] == '\r')) {
        p->pos++;
    }
}

char peek(JsonParser* p) {
    skip_whitespace(p);
    return p->json[p->pos];
}

char advance(JsonParser* p) {
    char c = p->json[p->pos];
    if (c) p->pos++;
    return c;
}

int match(JsonParser* p, char c) {
    if (peek(p) == c) {
        advance(p);
        return 1;
    }
    return 0;
}

void parse_string(JsonParser* p, char* buffer, int size) {
    if (match(p, '"')) {
        int i = 0;
        while (p->json[p->pos] && p->json[p->pos] != '"' && i < size - 1) {
            if (p->json[p->pos] == '\\') {
                p->pos++; // 跳过转义符
                char esc = p->json[p->pos];
                if (esc == 'n') buffer[i++] = '\n';
                else if (esc == 't') buffer[i++] = '\t';
                else buffer[i++] = esc; // 原样保留
            } else {
                buffer[i++] = p->json[p->pos];
            }
            p->pos++;
        }
        buffer[i] = 0;
        match(p, '"');
    } else {
        buffer[0] = 0;
    }
}

// --- 实现 ---

void GetConfigDir(char* buffer, int size) {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        snprintf(buffer, size, "%s\\git-account-manager-c", path);
        CreateDirectoryA(buffer, NULL);
    }
}

void LoadConfig(Config* config) {
    config->account_count = 0;
    config->active_id[0] = 0;

    char dir[MAX_PATH];
    GetConfigDir(dir, MAX_PATH);
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\accounts.json", dir);

    FILE* f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = (char*)malloc(length + 1);
    fread(data, 1, length, f);
    data[length] = 0;
    fclose(f);

    JsonParser p = { data, 0 };
    if (match(&p, '{')) {
        while (peek(&p) != '}') {
            char key[256];
            parse_string(&p, key, sizeof(key));
            match(&p, ':');

            if (strcmp(key, "accounts") == 0) {
                if (match(&p, '[')) {
                    while (peek(&p) != ']') {
                        if (peek(&p) == '{') {
                            if (config->account_count < MAX_ACCOUNTS) {
                                Account* acc = &config->accounts[config->account_count];
                                match(&p, '{');
                                while (peek(&p) != '}') {
                                    char accKey[256];
                                    parse_string(&p, accKey, sizeof(accKey));
                                    match(&p, ':');
                                    
                                    // 初始化账户
                                    acc->host_count = 0;
                                    
                                    char val[1024];
                                    if (strcmp(accKey, "host_list") == 0 && peek(&p) == '[') {
                                        // 解析host_list数组
                                        match(&p, '[');
                                        int hostIndex = 0;
                                        while (peek(&p) != ']' && hostIndex < 10) {
                                            if (peek(&p) == '"') {
                                                parse_string(&p, acc->host_list[hostIndex], sizeof(acc->host_list[0]));
                                                hostIndex++;
                                                acc->host_count = hostIndex;
                                            } else {
                                                advance(&p); // 跳过无效字符
                                            }
                                            if (peek(&p) == ',') match(&p, ',');
                                        }
                                        match(&p, ']');
                                        
                                        // 如果host_list为空，尝试从旧的git_host字段读取
                                        if (acc->host_count == 0) {
                                            match(&p, '{'); // 跳转到下一个字段
                                            while (peek(&p) != '}' && peek(&p) != ',') advance(&p);
                                            if (peek(&p) == '}') break;
                                        }
                                    } else {
                                        parse_string(&p, val, sizeof(val));
                                        
                                        if (strcmp(accKey, "id") == 0) strcpy(acc->id, val);
                                        else if (strcmp(accKey, "name") == 0) strcpy(acc->name, val);
                                        else if (strcmp(accKey, "email") == 0) strcpy(acc->email, val);
                                        else if (strcmp(accKey, "ssh_key_path") == 0) strcpy(acc->ssh_key_path, val);
                                        else if (strcmp(accKey, "git_host") == 0) {
                                            // 兼容旧格式：将git_host作为第一个host
                                            strcpy(acc->host_list[0], val);
                                            acc->host_count = 1;
                                        }
                                    }
                                    
                                    if (peek(&p) == ',') match(&p, ',');
                                }
                                match(&p, '}');
                                config->account_count++;
                            } else {
                                // 如果满了则跳过
                                match(&p, '{');
                                while (peek(&p) != '}') advance(&p);
                                match(&p, '}');
                            }
                        }
                        if (peek(&p) == ',') match(&p, ',');
                    }
                    match(&p, ']');
                }
            } else if (strcmp(key, "active_id") == 0) {
                parse_string(&p, config->active_id, sizeof(config->active_id));
            } else {
                // 跳过未知值
                if (peek(&p) == '"') {
                     char temp[1024];
                     parse_string(&p, temp, sizeof(temp));
                } else {
                     while(peek(&p) && peek(&p) != ',' && peek(&p) != '}') advance(&p);
                }
            }

            if (peek(&p) == ',') match(&p, ',');
        }
    }

    free(data);
}

// 前向声明 (用于自动导入功能)
void GetGlobalConfig(char* name, char* email);

// 自动导入当前 Git 全局身份到配置 (首次使用时)
void AutoImportGlobalIdentity(Config* config) {
    if (config->account_count > 0) return; // 已有账户则不导入
    
    char name[NAME_LEN] = "";
    char email[EMAIL_LEN] = "";
    GetGlobalConfig(name, email);
    
    // 如果有有效的全局配置，自动添加到账户列表
    if (strlen(name) > 0 && strlen(email) > 0) {
        Account* acc = &config->accounts[0];
        snprintf(acc->id, ID_LEN, "%lld", (long long)time(NULL));
        strcpy(acc->name, name);
        strcpy(acc->email, email);
        acc->ssh_key_path[0] = 0; // SSH Key 默认为空
        strcpy(acc->host_list[0], "github.com"); // 默认为 github.com
        acc->host_count = 1; // 设置host数量为1
        config->account_count = 1;
        strcpy(config->active_id, acc->id); // 设为当前激活账户
    }
}

void SaveConfig(Config* config) {
    char dir[MAX_PATH];
    GetConfigDir(dir, MAX_PATH);
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\accounts.json", dir);

    FILE* f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "{\n  \"accounts\": [\n");
    for (int i = 0; i < config->account_count; i++) {
        Account* acc = &config->accounts[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": \"%s\",\n", acc->id);
        fprintf(f, "      \"name\": \"%s\",\n", acc->name);
        fprintf(f, "      \"email\": \"%s\",\n", acc->email);
        
        // JSON 转义反斜杠
        char sshEscaped[2048];
        int k = 0;
        for (int j = 0; acc->ssh_key_path[j]; j++) {
            if (acc->ssh_key_path[j] == '\\') {
                sshEscaped[k++] = '\\';
                sshEscaped[k++] = '\\';
            } else {
                sshEscaped[k++] = acc->ssh_key_path[j];
            }
        }
        sshEscaped[k] = 0;
        
        fprintf(f, "      \"ssh_key_path\": \"%s\",\n", sshEscaped);
        fprintf(f, "      \"host_list\": [");
        for (int j = 0; j < acc->host_count; j++) {
            fprintf(f, "\"%s\"", acc->host_list[j]);
            if (j < acc->host_count - 1) fprintf(f, ", ");
        }
        fprintf(f, "]\n");
        fprintf(f, "    }%s\n", (i < config->account_count - 1) ? "," : "");
    }
    fprintf(f, "  ],\n");
    fprintf(f, "  \"active_id\": \"%s\"\n", config->active_id);
    fprintf(f, "}\n");

    fclose(f);
}

// --- Git 操作 ---

int RunCmd(const char* cmd, char* output, int outSize) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return 0;

    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcess 需要可变字符串
    char cmdMutable[2048];
    strcpy(cmdMutable, cmd);

    // 加上 cmd /c 并设置 UTF-8 代码页以正确处理中文
    char fullCmd[2048];
    snprintf(fullCmd, sizeof(fullCmd), "cmd /c chcp 65001 >nul & %s", cmd);

    if (!CreateProcess(NULL, fullCmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return 0;
    }

    CloseHandle(hWrite); // 关闭写端，以便读取结束时返回

    if (output && outSize > 0) {
        DWORD read;
        ZeroMemory(output, outSize);
        ReadFile(hRead, output, outSize - 1, &read, NULL);
        // 去除尾部空白符
        int len = strlen(output);
        while (len > 0 && (output[len-1] == '\n' || output[len-1] == '\r' || output[len-1] == ' ')) {
            output[--len] = 0;
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    return exitCode == 0;
}

// 获取 .gitconfig 文件路径
static int GetGitConfigPath(char* buffer, int size) {
    char userProfile[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) return 0;
    snprintf(buffer, size, "%s\\.gitconfig", userProfile);
    return 1;
}

// 直接读取 .gitconfig 文件获取全局配置 (UTF-8 编码，避免命令行乱码)
void GetGlobalConfig(char* name, char* email) {
    name[0] = 0;
    email[0] = 0;
    
    char gitconfigPath[MAX_PATH];
    if (!GetGitConfigPath(gitconfigPath, MAX_PATH)) return;
    
    FILE* f = fopen(gitconfigPath, "rb");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = (char*)malloc(length + 1);
    fread(data, 1, length, f);
    data[length] = 0;
    fclose(f);
    
    // 简单的 INI 解析
    int inUserSection = 0;
    char* line = data;
    while (line && *line) {
        while (*line == ' ' || *line == '\t') line++;
        
        char* lineEnd = strchr(line, '\n');
        if (lineEnd) *lineEnd = 0;
        
        int lineLen = strlen(line);
        if (lineLen > 0 && line[lineLen-1] == '\r') line[lineLen-1] = 0;
        
        if (line[0] == '[') {
            inUserSection = (strncmp(line, "[user]", 6) == 0);
        } else if (inUserSection && line[0] != '#' && line[0] != ';') {
            char* eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                char* key = line;
                char* val = eq + 1;
                
                int keyLen = strlen(key);
                while (keyLen > 0 && (key[keyLen-1] == ' ' || key[keyLen-1] == '\t')) key[--keyLen] = 0;
                while (*val == ' ' || *val == '\t') val++;
                
                if (strcmp(key, "name") == 0 && name[0] == 0) {
                    strncpy(name, val, NAME_LEN - 1);
                    name[NAME_LEN - 1] = 0;
                } else if (strcmp(key, "email") == 0 && email[0] == 0) {
                    strncpy(email, val, EMAIL_LEN - 1);
                    email[EMAIL_LEN - 1] = 0;
                }
            }
        }
        line = lineEnd ? lineEnd + 1 : NULL;
    }
    free(data);
}

// 直接修改 .gitconfig 文件设置全局配置 (UTF-8 编码，避免命令行乱码)
int SetGlobalConfig(const char* name, const char* email, const char* sshKeyPath) {
    char gitconfigPath[MAX_PATH];
    if (!GetGitConfigPath(gitconfigPath, MAX_PATH)) return 0;
    
    // 读取现有配置
    char* data = NULL;
    long length = 0;
    FILE* f = fopen(gitconfigPath, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = (char*)malloc(length + 1);
        fread(data, 1, length, f);
        data[length] = 0;
        fclose(f);
    }
    
    // 准备新配置内容
    char newConfig[8192] = "";
    int hasUserSection = 0;
    int hasCoreSection = 0;
    int wroteUserName = 0;
    int wroteUserEmail = 0;
    int wroteSshCommand = 0;
    
    if (data) {
        char* line = data;
        char currentSection[64] = "";
        
        while (line && *line) {
            char* lineEnd = strchr(line, '\n');
            char lineBuffer[1024];
            int lineLen = lineEnd ? (lineEnd - line) : strlen(line);
            if (lineLen >= sizeof(lineBuffer)) lineLen = sizeof(lineBuffer) - 1;
            strncpy(lineBuffer, line, lineLen);
            lineBuffer[lineLen] = 0;
            
            // 去除行尾 \r
            int bufLen = strlen(lineBuffer);
            if (bufLen > 0 && lineBuffer[bufLen-1] == '\r') lineBuffer[bufLen-1] = 0;
            
            // 检查节头
            char* trimmed = lineBuffer;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
            
            if (trimmed[0] == '[') {
                // 如果离开 [user] 节但还没写入 name/email，现在写入
                if (strcmp(currentSection, "user") == 0) {
                    if (!wroteUserName) {
                        strcat(newConfig, "\tname = ");
                        strcat(newConfig, name);
                        strcat(newConfig, "\n");
                        wroteUserName = 1;
                    }
                    if (!wroteUserEmail) {
                        strcat(newConfig, "\temail = ");
                        strcat(newConfig, email);
                        strcat(newConfig, "\n");
                        wroteUserEmail = 1;
                    }
                }
                // 如果离开 [core] 节但还没写入 sshCommand，现在写入
                if (strcmp(currentSection, "core") == 0 && strlen(sshKeyPath) > 0 && !wroteSshCommand) {
                    char sshPath[1024];
                    int j = 0;
                    for (int i = 0; sshKeyPath[i]; i++) {
                        if (sshKeyPath[i] == '\\') sshPath[j++] = '/';
                        else sshPath[j++] = sshKeyPath[i];
                    }
                    sshPath[j] = 0;
                    strcat(newConfig, "\tsshCommand = ssh -i \"");
                    strcat(newConfig, sshPath);
                    strcat(newConfig, "\" -o IdentitiesOnly=yes\n");
                    wroteSshCommand = 1;
                }
                
                // 更新当前节
                if (strncmp(trimmed, "[user]", 6) == 0) {
                    strcpy(currentSection, "user");
                    hasUserSection = 1;
                } else if (strncmp(trimmed, "[core]", 6) == 0) {
                    strcpy(currentSection, "core");
                    hasCoreSection = 1;
                } else {
                    currentSection[0] = 0;
                }
                strcat(newConfig, lineBuffer);
                strcat(newConfig, "\n");
            }
            // 处理 [user] 节的配置项
            else if (strcmp(currentSection, "user") == 0) {
                char* eq = strchr(trimmed, '=');
                if (eq) {
                    char key[64];
                    int keyLen = eq - trimmed;
                    strncpy(key, trimmed, keyLen);
                    key[keyLen] = 0;
                    // 去除 key 尾部空白
                    while (keyLen > 0 && (key[keyLen-1] == ' ' || key[keyLen-1] == '\t')) key[--keyLen] = 0;
                    
                    if (strcmp(key, "name") == 0) {
                        strcat(newConfig, "\tname = ");
                        strcat(newConfig, name);
                        strcat(newConfig, "\n");
                        wroteUserName = 1;
                    } else if (strcmp(key, "email") == 0) {
                        strcat(newConfig, "\temail = ");
                        strcat(newConfig, email);
                        strcat(newConfig, "\n");
                        wroteUserEmail = 1;
                    } else {
                        strcat(newConfig, lineBuffer);
                        strcat(newConfig, "\n");
                    }
                } else {
                    strcat(newConfig, lineBuffer);
                    strcat(newConfig, "\n");
                }
            }
            // 处理 [core] 节的配置项
            else if (strcmp(currentSection, "core") == 0) {
                char* eq = strchr(trimmed, '=');
                if (eq) {
                    char key[64];
                    int keyLen = eq - trimmed;
                    strncpy(key, trimmed, keyLen);
                    key[keyLen] = 0;
                    while (keyLen > 0 && (key[keyLen-1] == ' ' || key[keyLen-1] == '\t')) key[--keyLen] = 0;
                    
                    if (strcmp(key, "sshCommand") == 0) {
                        if (strlen(sshKeyPath) > 0) {
                            char sshPath[1024];
                            int j = 0;
                            for (int i = 0; sshKeyPath[i]; i++) {
                                if (sshKeyPath[i] == '\\') sshPath[j++] = '/';
                                else sshPath[j++] = sshKeyPath[i];
                            }
                            sshPath[j] = 0;
                            strcat(newConfig, "\tsshCommand = ssh -i \"");
                            strcat(newConfig, sshPath);
                            strcat(newConfig, "\" -o IdentitiesOnly=yes\n");
                        }
                        // 如果 sshKeyPath 为空，不写入（相当于删除）
                        wroteSshCommand = 1;
                    } else {
                        strcat(newConfig, lineBuffer);
                        strcat(newConfig, "\n");
                    }
                } else {
                    strcat(newConfig, lineBuffer);
                    strcat(newConfig, "\n");
                }
            }
            // 其他节，原样保留
            else {
                strcat(newConfig, lineBuffer);
                strcat(newConfig, "\n");
            }
            
            line = lineEnd ? lineEnd + 1 : NULL;
        }
        
        // 文件结束时，检查是否还在某个节中需要写入
        if (strcmp(currentSection, "user") == 0) {
            if (!wroteUserName) {
                strcat(newConfig, "\tname = ");
                strcat(newConfig, name);
                strcat(newConfig, "\n");
                wroteUserName = 1;
            }
            if (!wroteUserEmail) {
                strcat(newConfig, "\temail = ");
                strcat(newConfig, email);
                strcat(newConfig, "\n");
                wroteUserEmail = 1;
            }
        }
        if (strcmp(currentSection, "core") == 0 && strlen(sshKeyPath) > 0 && !wroteSshCommand) {
            char sshPath[1024];
            int j = 0;
            for (int i = 0; sshKeyPath[i]; i++) {
                if (sshKeyPath[i] == '\\') sshPath[j++] = '/';
                else sshPath[j++] = sshKeyPath[i];
            }
            sshPath[j] = 0;
            strcat(newConfig, "\tsshCommand = ssh -i \"");
            strcat(newConfig, sshPath);
            strcat(newConfig, "\" -o IdentitiesOnly=yes\n");
            wroteSshCommand = 1;
        }
        
        free(data);
    }
    
    // 如果没有 [user] 节，添加
    if (!hasUserSection) {
        strcat(newConfig, "[user]\n");
        strcat(newConfig, "\tname = ");
        strcat(newConfig, name);
        strcat(newConfig, "\n");
        strcat(newConfig, "\temail = ");
        strcat(newConfig, email);
        strcat(newConfig, "\n");
        wroteUserName = 1;
        wroteUserEmail = 1;
    }
    
    // 如果需要 sshCommand 但没有 [core] 节，添加
    if (strlen(sshKeyPath) > 0 && !wroteSshCommand) {
        if (!hasCoreSection) {
            strcat(newConfig, "[core]\n");
        }
        char sshPath[1024];
        int j = 0;
        for (int i = 0; sshKeyPath[i]; i++) {
            if (sshKeyPath[i] == '\\') sshPath[j++] = '/';
            else sshPath[j++] = sshKeyPath[i];
        }
        sshPath[j] = 0;
        strcat(newConfig, "\tsshCommand = ssh -i \"");
        strcat(newConfig, sshPath);
        strcat(newConfig, "\" -o IdentitiesOnly=yes\n");
    }
    
    // 写入文件 (UTF-8)
    f = fopen(gitconfigPath, "wb");
    if (!f) return 0;
    fwrite(newConfig, 1, strlen(newConfig), f);
    fclose(f);
    
    return 1;
}

int GetSSHKeys(char keys[][PATH_LEN], int maxKeys) {
    char path[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path))) return 0;
    
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\.ssh\\id_*", path);
    
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    
    int count = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // 排除 .pub 文件
            if (strstr(fd.cFileName, ".pub")) continue;
            
            snprintf(keys[count], PATH_LEN, "%s\\.ssh\\%s", path, fd.cFileName);
            count++;
        }
    } while (FindNextFileA(hFind, &fd) && count < maxKeys);
    
    FindClose(hFind);
    return count;
}

int GenerateSSHKey(const char* name, const char* email, const char* type, char* outPath) {
    char userProfile[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) return 0;
    
    char sshDir[MAX_PATH];
    snprintf(sshDir, MAX_PATH, "%s\\.ssh", userProfile);
    CreateDirectoryA(sshDir, NULL); // 确保目录存在
    
    char keyPath[MAX_PATH];
    snprintf(keyPath, MAX_PATH, "%s\\%s", sshDir, name);
    
    // 检查是否存在
    if (GetFileAttributesA(keyPath) != INVALID_FILE_ATTRIBUTES) {
        return 0; // Already exists
    }
    
    // ssh-keygen -t <type> -C <email> -f <path> -N ""
    char cmd[2048];
    // 注意：ssh-keygen 可能需要路径周围有引号
    snprintf(cmd, sizeof(cmd), "ssh-keygen -t %s -C \"%s\" -f \"%s\" -N \"\"", type, email, keyPath);
    
    if (RunCmd(cmd, NULL, 0)) {
        if (outPath) strcpy(outPath, keyPath);
        return 1;
    }
    return 0;
}

// 从 SSH config 中获取对应密钥的 Host
int GetHostFromSSHConfig(const char* keyPath, char* outHost, int maxLen) {
    if (!keyPath || !strlen(keyPath)) return 0;
    
    char keyName[MAX_PATH];
    const char* lastSlash = strrchr(keyPath, '\\');
    strncpy(keyName, lastSlash ? lastSlash + 1 : keyPath, MAX_PATH - 1);
    
    char userProfile[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) return 0;
    
    char sshConfigPath[MAX_PATH];
    snprintf(sshConfigPath, MAX_PATH, "%s\\.ssh\\config", userProfile);
    
    FILE* f = fopen(sshConfigPath, "rb");
    if (!f) return 0;
    
    char line[512];
    char currentHost[256] = "";
    int found = 0;
    
    char searchPattern[512];
    snprintf(searchPattern, sizeof(searchPattern), "IdentityFile ~/.ssh/%s", keyName);
    
    while (fgets(line, sizeof(line), f)) {
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        
        if (strncmp(trimmed, "Host ", 5) == 0) {
            char* p = trimmed + 5;
            while (*p == ' ' || *p == '\t') p++;
            char* end = p;
            while (*end && *end != '\r' && *end != '\n' && *end != ' ' && *end != '\t') end++;
            int len = end - p;
            if (len < sizeof(currentHost)) {
                strncpy(currentHost, p, len);
                currentHost[len] = '\0';
            }
        }
        
        if (strstr(trimmed, searchPattern)) { // Found the IdentityFile line
            if (strlen(currentHost) > 0) {
                strncpy(outHost, currentHost, maxLen - 1);
                outHost[maxLen - 1] = '\0';
                found = 1;
            }
            break;
        }
    }
    
    fclose(f);
    return found;
}

// 通用的更新 SSH config 的逻辑：读入内存行数组，为每个host创建独立的配置块
static int ModifySSHConfigLines(const char* keyName, const char* email, const char* host) {
    char userProfile[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) return 0;
    
    char sshConfigPath[MAX_PATH];
    snprintf(sshConfigPath, MAX_PATH, "%s\\.ssh\\config", userProfile);
    
    // 读取现有配置
    int maxLines = 1000;
    char (*lines)[512] = malloc(maxLines * 512); // 分配大缓冲区
    int lineCount = 0;
    FILE* f = fopen(sshConfigPath, "rb");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f) && lineCount < maxLines) {
            strcpy(lines[lineCount++], line);
        }
        fclose(f);
    }
    
    // 检查是否已经存在相同的host配置（更全面的检查）
    int hostExists = 0;
    int existingHostStart = -1; // 记录现有Host块的起始位置
    int existingHostEnd = -1;   // 记录现有Host块的结束位置
    
    for (int i = 0; i < lineCount; i++) {
        char* trimmed = lines[i];
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        
        if (strncmp(trimmed, "Host ", 5) == 0) {
            // 检查host名称
            char* hostStart = trimmed + 5;
            while (*hostStart == ' ' || *hostStart == '\t') hostStart++;
            
            // 提取host名称
            char currentHost[256];
            int k = 0;
            while (hostStart[k] && hostStart[k] != ' ' && hostStart[k] != '\t' && 
                   hostStart[k] != '\n' && hostStart[k] != '\r') {
                currentHost[k] = hostStart[k];
                k++;
            }
            currentHost[k] = '\0';
            
            // 检查host是否匹配
            if (strcmp(currentHost, host) == 0) {
                existingHostStart = i; // 记录Host块的起始位置
                
                // 在这个Host块中查找IdentityFile
                existingHostEnd = i; // 初始化结束位置为起始位置
                for (int j = i + 1; j < lineCount && j < i + 20; j++) {  // 扩大搜索范围
                    char* nextLine = lines[j];
                    while (*nextLine == ' ' || *nextLine == '\t') nextLine++;
                    
                    if (strncmp(nextLine, "Host ", 5) == 0) {
                        // 到达下一个Host块，跳出内层循环
                        existingHostEnd = j - 1;
                        break;
                    }
                    
                    if (strncmp(nextLine, "IdentityFile", 12) == 0) {
                        // 检查IdentityFile是否匹配
                        if (strstr(nextLine, keyName)) {
                            hostExists = 1;  // 相同的Host和IdentityFile已存在
                            existingHostEnd = j;
                            break;
                        }
                    }
                    existingHostEnd = j; // 更新结束位置
                }
                
                if (hostExists) {
                    break;
                }
            }
        }
    }
    
    // 如果Host配置已存在，不再添加
    if (hostExists) {
        free(lines);
        return 1;
    }
    
    // 如果Host配置存在但IdentityFile不同（即不同的密钥使用同一个host），则替换
    if (existingHostStart != -1 && existingHostEnd != -1) {
        // 创建新的Host配置块
        char newBlock[2048];
        snprintf(newBlock, sizeof(newBlock),
            "# Git configuration for %s\n"
            "Host %s\n"
            "\tHostName %s\n"
            "\tUser git\n"
            "\tIdentityFile ~/.ssh/%s\n"
            "\tIdentitiesOnly yes\n"
            "\tPreferredAuthentications publickey\n\n",
            email, host, host, keyName);
        
        FILE* fout = fopen(sshConfigPath, "wb");
        if (!fout) {
            free(lines);
            return 0;
        }
        
        // 写入Host块之前的配置
        for (int i = 0; i < existingHostStart; i++) {
            fwrite(lines[i], 1, strlen(lines[i]), fout);
        }
        
        // 写入新的Host配置块
        fwrite(newBlock, 1, strlen(newBlock), fout);
        
        // 跳过旧的Host块（从existingHostStart到existingHostEnd），写入后面的配置
        for (int i = existingHostEnd + 1; i < lineCount; i++) {
            fwrite(lines[i], 1, strlen(lines[i]), fout);
            if (lines[i][strlen(lines[i])-1] != '\n' && i < lineCount - 1) {
                fwrite("\n", 1, 1, fout);
            }
        }
        
        fclose(fout);
        free(lines);
        return 1;
    }
    
    FILE* fout = fopen(sshConfigPath, "wb");
    if (!fout) {
        free(lines);
        return 0;
    }
    
    // 写回原来的配置
    for (int i = 0; i < lineCount; i++) {
        fwrite(lines[i], 1, strlen(lines[i]), fout);
        if (lines[i][strlen(lines[i])-1] != '\n' && i < lineCount - 1) {
            fwrite("\n", 1, 1, fout);
        }
    }
    if (lineCount > 0 && lines[lineCount-1][strlen(lines[lineCount-1])-1] != '\n') {
        fwrite("\n", 1, 1, fout);
    }
    
    // 添加新的Host配置块
    char newBlock[2048];
    snprintf(newBlock, sizeof(newBlock),
        "# Git configuration for %s\n"
        "Host %s\n"
        "\tHostName %s\n"
        "\tUser git\n"
        "\tIdentityFile ~/.ssh/%s\n"
        "\tIdentitiesOnly yes\n"
        "\tPreferredAuthentications publickey\n\n",
        email, host, host, keyName);
    fwrite(newBlock, 1, strlen(newBlock), fout);
    
    fclose(fout);
    free(lines);
    return 1;
}

// 清理SSH配置文件中与指定密钥相关的所有Host配置（保留需要的hosts）
int CleanupSSHConfigForKey(const char* keyPath, const char* email, const char* const* keepHosts, int keepHostCount) {
    char userProfile[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) return 0;
    
    char sshConfigPath[MAX_PATH];
    snprintf(sshConfigPath, MAX_PATH, "%s\\.ssh\\config", userProfile);
    
    // 读取现有配置
    int maxLines = 1000;
    char (*lines)[512] = malloc(maxLines * 512); // 分配大缓冲区
    int lineCount = 0;
    FILE* f = fopen(sshConfigPath, "rb");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f) && lineCount < maxLines) {
            strcpy(lines[lineCount++], line);
        }
        fclose(f);
    } else {
        free(lines);
        return 1; // 文件不存在，返回成功
    }

    // 标记需要保留的行
    int* keepLine = malloc(lineCount * sizeof(int));
    for (int i = 0; i < lineCount; i++) {
        keepLine[i] = 1; // 默认保留所有行
    }

    // 查找与指定密钥相关的Host块
    char keyName[MAX_PATH];
    const char* lastSlash = strrchr(keyPath, '\\');
    strncpy(keyName, lastSlash ? lastSlash + 1 : keyPath, MAX_PATH - 1);
    
    for (int i = 0; i < lineCount; i++) {
        char* trimmed = lines[i];
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        
        if (strncmp(trimmed, "Host ", 5) == 0) {
            // 检查是否包含对应的IdentityFile
            int hasIdentityFile = 0;
            int hostEnd = i; // 找到Host块的结束位置
            for (int j = i + 1; j < lineCount; j++) { // 遍历直到找到下一个Host或文件末尾
                char* nextLine = lines[j];
                while (*nextLine == ' ' || *nextLine == '\t') nextLine++;
                if (strncmp(nextLine, "Host ", 5) == 0) {
                    // 到达下一个Host块，设置当前Host块的结束位置
                    hostEnd = j - 1;
                    break;
                }
                if (strncmp(nextLine, "IdentityFile", 12) == 0) {
                    if (strstr(nextLine, keyName)) {
                        hasIdentityFile = 1;
                    }
                }
                hostEnd = j; // 更新结束位置
            }

            if (hasIdentityFile) {
                // 提取Host名称
                char* hostStart = trimmed + 5;
                while (*hostStart == ' ' || *hostStart == '\t') hostStart++;
                char host[HOST_LEN];
                int k = 0;
                while (hostStart[k] && hostStart[k] != ' ' && hostStart[k] != '\t' && 
                       hostStart[k] != '\n' && hostStart[k] != '\r') {
                    host[k] = hostStart[k];
                    k++;
                }
                host[k] = '\0';

                // 检查这个host是否需要保留
                int shouldKeep = 0;
                for (int j = 0; j < keepHostCount; j++) {
                    if (strcmp(host, keepHosts[j]) == 0) {
                        shouldKeep = 1;
                        break;
                    }
                }

                if (!shouldKeep) {
                    // 检查前一行是否是注释，如果是相关的配置注释也一并删除
                    int deleteStart = i;
                    if (i > 0) {
                        char* prevLine = lines[i-1];
                        while (*prevLine == ' ' || *prevLine == '\t') prevLine++;
                        if (strncmp(prevLine, "# Git configuration for", 23) == 0) {
                            deleteStart = i - 1; // 也删除前一行的注释
                        }
                    }
                    
                    // 标记整个Host块（包括可能的注释）为删除
                    for (int j = deleteStart; j <= hostEnd && j < lineCount; j++) {
                        keepLine[j] = 0;
                    }
                }
            }
        }
    }

    // 写回保留的行到临时文件
    char tempPath[MAX_PATH];
    snprintf(tempPath, MAX_PATH, "%s.tmp", sshConfigPath);
    FILE* fout = fopen(tempPath, "wb");
    if (!fout) {
        free(lines);
        free(keepLine);
        return 0;
    }

    for (int i = 0; i < lineCount; i++) {
        if (keepLine[i]) {
            fwrite(lines[i], 1, strlen(lines[i]), fout);
            if (lines[i][strlen(lines[i])-1] != '\n' && i < lineCount - 1) {
                fwrite("\n", 1, 1, fout);
            }
        }
    }

    fclose(fout);
    free(lines);
    free(keepLine);

    // 替换原文件
    MoveFileExA(tempPath, sshConfigPath, MOVEFILE_REPLACE_EXISTING);
    return 1;
}

// 为现有的 SSH 密钥创建或更新 SSH config，支持多个hosts
// keyPath: SSH 密钥的完整路径 (如 C:\Users\Admin\.ssh\id_rsa)
// email: 关联邮箱 (用于注释)
// hosts: Git服务Host数组
// hostCount: Host数量
// 返回值: 0 失败, 1 成功
int AddMultipleHostsToSSHConfig(const char* keyPath, const char* email, const char hosts[][HOST_LEN], int hostCount) {
    if (GetFileAttributesA(keyPath) == INVALID_FILE_ATTRIBUTES) return 0;
    
    char keyName[MAX_PATH];
    const char* lastSlash = strrchr(keyPath, '\\');
    strncpy(keyName, lastSlash ? lastSlash + 1 : keyPath, MAX_PATH - 1);
    
    int successCount = 0;
    for (int i = 0; i < hostCount; i++) {
        if (ModifySSHConfigLines(keyName, email, hosts[i])) {
            successCount++;
        }
    }
    return successCount > 0 ? 1 : 0;
}

// 更新 SSH config 文件，添加新密钥的配置
static int UpdateSSHConfigWithKey(const char* keyName, const char* email, const char* host) {
    return ModifySSHConfigLines(keyName, email, host);
}

// 为现有的 SSH 密钥创建或更新 SSH config
// keyPath: SSH 密钥的完整路径 (如 C:\Users\Admin\.ssh\id_rsa)
// email: 关联邮箱 (用于注释)
// host: Git服务Host (例如 github.com, gitlab.com)
// 返回值: 0 失败, 1 成功
int AddExistingKeyToSSHConfig(const char* keyPath, const char* email, const char* host) {
    if (GetFileAttributesA(keyPath) == INVALID_FILE_ATTRIBUTES) return 0;
    
    char keyName[MAX_PATH];
    const char* lastSlash = strrchr(keyPath, '\\');
    strncpy(keyName, lastSlash ? lastSlash + 1 : keyPath, MAX_PATH - 1);
    
    return ModifySSHConfigLines(keyName, email, host);
}

// 生成 SSH 密钥并自动添加到 SSH config 和 Git 全局配置
int GenerateSSHKeyAndUpdateConfig(const char* name, const char* email, const char* type, char* outPath, const char* host) {
    if (!GenerateSSHKey(name, email, type, outPath)) return 0;
    UpdateSSHConfigWithKey(name, email, host);
    SetGlobalConfig(name, email, outPath);
    return 1;
}