#define _CRT_SECURE_NO_WARNINGS
#include "logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

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
        snprintf(buffer, size, "%s\\git-account-manager-go", path);
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
                                    char val[1024];
                                    parse_string(&p, val, sizeof(val));
                                    
                                    if (strcmp(accKey, "id") == 0) strcpy(acc->id, val);
                                    else if (strcmp(accKey, "name") == 0) strcpy(acc->name, val);
                                    else if (strcmp(accKey, "email") == 0) strcpy(acc->email, val);
                                    else if (strcmp(accKey, "ssh_key_path") == 0) strcpy(acc->ssh_key_path, val);
                                    
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
        
        fprintf(f, "      \"ssh_key_path\": \"%s\"\n", sshEscaped);
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

    // 加上 cmd /c 以确保能找到 PATH 中的命令
    char fullCmd[2048];
    snprintf(fullCmd, sizeof(fullCmd), "cmd /c %s", cmd);

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

void GetGlobalConfig(char* name, char* email) {
    RunCmd("git config --global --get user.name", name, NAME_LEN);
    RunCmd("git config --global --get user.email", email, EMAIL_LEN);
}

int SetGlobalConfig(const char* name, const char* email, const char* sshKeyPath) {
    char cmd[2048];
    
    snprintf(cmd, sizeof(cmd), "git config --global --replace-all user.name \"%s\"", name);
    if (!RunCmd(cmd, NULL, 0)) return 0;
    
    snprintf(cmd, sizeof(cmd), "git config --global --replace-all user.email \"%s\"", email);
    if (!RunCmd(cmd, NULL, 0)) return 0;
    
    if (strlen(sshKeyPath) > 0) {
        char sshPath[1024];
        int j = 0;
        for (int i = 0; sshKeyPath[i]; i++) {
             if (sshKeyPath[i] == '\\') sshPath[j++] = '/';
             else sshPath[j++] = sshKeyPath[i];
        }
        sshPath[j] = 0;
        
        // 转义引号
        snprintf(cmd, sizeof(cmd), "git config --global --replace-all core.sshCommand \"ssh -i \\\"%s\\\" -o IdentitiesOnly=yes\"", sshPath);
        if (!RunCmd(cmd, NULL, 0)) return 0;
    } else {
        RunCmd("git config --global --unset-all core.sshCommand", NULL, 0);
    }
    
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