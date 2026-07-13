#define _CRT_SECURE_NO_WARNINGS
#include "logic.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include <wchar.h>
#include <time.h>

#define GAM_BEGIN_MARKER "# >>> Git Account Manager >>>"
#define GAM_END_MARKER "# <<< Git Account Manager <<<"
#define GAM_OLD_MARKER "# Git Account Manager - "
#define MAX_PRIVATE_KEY_SIZE (16u * 1024u * 1024u)
#define SSH_KEYGEN_TIMEOUT_MS 30000

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

typedef struct {
    const char* text;
    size_t pos;
    size_t len;
} JsonParser;

typedef struct {
    wchar_t* path;
    char* data;
    size_t len;
    int existed;
} FileSnapshot;

typedef struct {
    char host[HOST_LEN];
    int has_port;
    int port;
    int is_ipv6;
} HostSpec;

static char g_logic_error[512];

static void ClearLogicError(void) {
    g_logic_error[0] = 0;
}

static void SetLogicError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(g_logic_error, sizeof(g_logic_error), format, args);
    va_end(args);
    g_logic_error[sizeof(g_logic_error) - 1] = 0;
}

static void SetWindowsError(const char* action, DWORD error) {
    char message[256] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, 0, message, (DWORD)sizeof(message), NULL);
    for (size_t i = strlen(message); i > 0; i--) {
        if (message[i - 1] == '\r' || message[i - 1] == '\n') message[i - 1] = 0;
        else break;
    }
    SetLogicError("%s failed (%lu): %s", action, (unsigned long)error, message);
}

const char* GetLogicErrorMessage(void) {
    return g_logic_error;
}

static char* DupBytes(const char* value, size_t len) {
    char* copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    if (len > 0) memcpy(copy, value, len);
    copy[len] = 0;
    return copy;
}

static wchar_t* DupWide(const wchar_t* value) {
    size_t len = wcslen(value);
    wchar_t* copy = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!copy) return NULL;
    memcpy(copy, value, (len + 1) * sizeof(wchar_t));
    return copy;
}

static wchar_t* Utf8ToWideAlloc(const char* value) {
    if (!value) return NULL;
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
    if (needed <= 0) return NULL;
    wchar_t* result = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (!result) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result, needed)) {
        free(result);
        return NULL;
    }
    return result;
}

static char* WideToUtf8Alloc(const wchar_t* value) {
    if (!value) return NULL;
    int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1,
        NULL, 0, NULL, NULL);
    if (needed <= 0) return NULL;
    char* result = (char*)malloc((size_t)needed);
    if (!result) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, 0, value, -1,
            result, needed, NULL, NULL)) {
        free(result);
        return NULL;
    }
    return result;
}

static wchar_t* JoinPathW(const wchar_t* base, const wchar_t* leaf) {
    size_t base_len = wcslen(base);
    size_t leaf_len = wcslen(leaf);
    int need_slash = base_len > 0 && base[base_len - 1] != L'\\' && base[base_len - 1] != L'/';
    if (base_len > SIZE_MAX - leaf_len - 2) return NULL;
    wchar_t* result = (wchar_t*)malloc((base_len + leaf_len + 2) * sizeof(wchar_t));
    if (!result) return NULL;
    memcpy(result, base, base_len * sizeof(wchar_t));
    size_t pos = base_len;
    if (need_slash) result[pos++] = L'\\';
    memcpy(result + pos, leaf, (leaf_len + 1) * sizeof(wchar_t));
    return result;
}

static int EnsureDirectoryW(const wchar_t* path) {
    int result = SHCreateDirectoryExW(NULL, path, NULL);
    if (result == ERROR_SUCCESS || result == ERROR_FILE_EXISTS || result == ERROR_ALREADY_EXISTS) {
        return 1;
    }
    SetWindowsError("create directory", (DWORD)result);
    return 0;
}

#ifdef GAM_TESTING
static wchar_t* GetTestDirectoryW(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, NULL, 0);
    if (needed == 0) return NULL;
    wchar_t* value = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (!value) return NULL;
    if (!GetEnvironmentVariableW(name, value, needed) || value[0] == 0) {
        free(value);
        return NULL;
    }
    return value;
}
#endif

static wchar_t* GetProfileDirectoryW(void) {
#ifdef GAM_TESTING
    wchar_t* test_dir = GetTestDirectoryW(L"GAM_TEST_PROFILE_DIR");
    if (test_dir) return test_dir;
#endif
    wchar_t path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        SetLogicError("cannot locate the user profile directory");
        return NULL;
    }
    return DupWide(path);
}

static wchar_t* GetConfigDirectoryW(int create) {
#ifdef GAM_TESTING
    wchar_t* test_dir = GetTestDirectoryW(L"GAM_TEST_CONFIG_DIR");
    if (test_dir) {
        if (create && !EnsureDirectoryW(test_dir)) {
            free(test_dir);
            return NULL;
        }
        return test_dir;
    }
#endif
    wchar_t app_data[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, app_data))) {
        SetLogicError("cannot locate the application data directory");
        return NULL;
    }
    wchar_t* result = JoinPathW(app_data, L"git-account-manager-c");
    if (!result) {
        SetLogicError("out of memory");
        return NULL;
    }
    if (create && !EnsureDirectoryW(result)) {
        free(result);
        return NULL;
    }
    return result;
}

static wchar_t* GetSSHDirectoryW(void) {
    wchar_t* profile = GetProfileDirectoryW();
    if (!profile) return NULL;
    wchar_t* path = JoinPathW(profile, L".ssh");
    free(profile);
    if (!path) SetLogicError("out of memory");
    return path;
}

static wchar_t* GetSSHConfigPathW(void) {
    wchar_t* ssh_dir = GetSSHDirectoryW();
    if (!ssh_dir) return NULL;
    wchar_t* path = JoinPathW(ssh_dir, L"config");
    free(ssh_dir);
    if (!path) SetLogicError("out of memory");
    return path;
}

static wchar_t* GetGitConfigPathW(void) {
    wchar_t* profile = GetProfileDirectoryW();
    if (!profile) return NULL;
    wchar_t* path = JoinPathW(profile, L".gitconfig");
    free(profile);
    if (!path) SetLogicError("out of memory");
    return path;
}

static wchar_t* GetAccountsPathW(void) {
    wchar_t* dir = GetConfigDirectoryW(1);
    if (!dir) return NULL;
    wchar_t* path = JoinPathW(dir, L"accounts.json");
    free(dir);
    if (!path) SetLogicError("out of memory");
    return path;
}

void GetConfigDir(char* buffer, int size) {
    if (!buffer || size <= 0) return;
    buffer[0] = 0;
    wchar_t* path = GetConfigDirectoryW(1);
    if (!path) return;
    char* utf8 = WideToUtf8Alloc(path);
    free(path);
    if (!utf8) {
        SetLogicError("cannot encode the config directory as UTF-8");
        return;
    }
    if (strlen(utf8) >= (size_t)size) {
        SetLogicError("config directory path is too long");
        free(utf8);
        return;
    }
    strcpy(buffer, utf8);
    free(utf8);
}

static int SBReserve(StringBuilder* builder, size_t extra) {
    if (extra > SIZE_MAX - builder->len - 1) return 0;
    size_t needed = builder->len + extra + 1;
    if (needed <= builder->cap) return 1;
    size_t cap = builder->cap ? builder->cap : 256;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }
    char* resized = (char*)realloc(builder->data, cap);
    if (!resized) return 0;
    builder->data = resized;
    builder->cap = cap;
    return 1;
}

static int SBAppendN(StringBuilder* builder, const char* value, size_t len) {
    if (!SBReserve(builder, len)) return 0;
    if (len > 0) memcpy(builder->data + builder->len, value, len);
    builder->len += len;
    builder->data[builder->len] = 0;
    return 1;
}

static int SBAppend(StringBuilder* builder, const char* value) {
    return SBAppendN(builder, value, strlen(value));
}

static int SBAppendChar(StringBuilder* builder, char value) {
    return SBAppendN(builder, &value, 1);
}

static int SBAppendFormat(StringBuilder* builder, const char* format, ...) {
    va_list args;
    va_list copy;
    va_start(args, format);
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0 || !SBReserve(builder, (size_t)needed)) {
        va_end(args);
        return 0;
    }
    vsnprintf(builder->data + builder->len, builder->cap - builder->len, format, args);
    va_end(args);
    builder->len += (size_t)needed;
    return 1;
}

static void SBFree(StringBuilder* builder) {
    free(builder->data);
    builder->data = NULL;
    builder->len = 0;
    builder->cap = 0;
}

static int ReadFileBytesW(const wchar_t* path, char** data, size_t* length, int* existed) {
    *data = NULL;
    *length = 0;
    *existed = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            *data = DupBytes("", 0);
            if (!*data) SetLogicError("out of memory");
            return *data != NULL;
        }
        SetWindowsError("read file", error);
        return 0;
    }
    *existed = 1;
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart < 0 ||
        (uint64_t)file_size.QuadPart > SIZE_MAX - 1) {
        DWORD error = GetLastError();
        CloseHandle(file);
        SetWindowsError("get file size", error);
        return 0;
    }
    size_t size = (size_t)file_size.QuadPart;
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        CloseHandle(file);
        SetLogicError("out of memory");
        return 0;
    }
    size_t total = 0;
    while (total < size) {
        DWORD chunk = (DWORD)((size - total) > 0x40000000u ? 0x40000000u : (size - total));
        DWORD read = 0;
        if (!ReadFile(file, buffer + total, chunk, &read, NULL) || read == 0) {
            DWORD error = GetLastError();
            free(buffer);
            CloseHandle(file);
            SetWindowsError("read file", error);
            return 0;
        }
        total += read;
    }
    CloseHandle(file);
    buffer[size] = 0;
    *data = buffer;
    *length = size;
    return 1;
}

static int AtomicWriteFileW(const wchar_t* path, const char* data, size_t length) {
    size_t path_len = wcslen(path);
    wchar_t* temp_path = (wchar_t*)malloc((path_len + 80) * sizeof(wchar_t));
    if (!temp_path) {
        SetLogicError("out of memory");
        return 0;
    }
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        swprintf(temp_path, path_len + 80, L"%ls.gam.%lu.%lu.%u.tmp", path,
            (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), attempt);
        file = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        free(temp_path);
        SetWindowsError("create temporary file", error);
        return 0;
    }
    size_t total = 0;
    int ok = 1;
    while (total < length) {
        DWORD chunk = (DWORD)((length - total) > 0x40000000u ? 0x40000000u : (length - total));
        DWORD written = 0;
        if (!WriteFile(file, data + total, chunk, &written, NULL) || written != chunk) {
            SetWindowsError("write temporary file", GetLastError());
            ok = 0;
            break;
        }
        total += written;
    }
    if (ok && !FlushFileBuffers(file)) {
        SetWindowsError("flush temporary file", GetLastError());
        ok = 0;
    }
    if (!CloseHandle(file) && ok) {
        SetWindowsError("close temporary file", GetLastError());
        ok = 0;
    }
    if (ok && !MoveFileExW(temp_path, path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        SetWindowsError("replace file", GetLastError());
        ok = 0;
    }
    if (!ok) DeleteFileW(temp_path);
    free(temp_path);
    return ok;
}

static int TakeFileSnapshot(const wchar_t* path, FileSnapshot* snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->path = DupWide(path);
    if (!snapshot->path) {
        SetLogicError("out of memory");
        return 0;
    }
    if (!ReadFileBytesW(path, &snapshot->data, &snapshot->len, &snapshot->existed)) {
        free(snapshot->path);
        memset(snapshot, 0, sizeof(*snapshot));
        return 0;
    }
    return 1;
}

static int RestoreFileSnapshot(const FileSnapshot* snapshot) {
    if (snapshot->existed) {
        return AtomicWriteFileW(snapshot->path, snapshot->data, snapshot->len);
    }
    if (DeleteFileW(snapshot->path)) return 1;
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 1;
    SetWindowsError("rollback file", error);
    return 0;
}

static void FreeFileSnapshot(FileSnapshot* snapshot) {
    free(snapshot->path);
    free(snapshot->data);
    memset(snapshot, 0, sizeof(*snapshot));
}

static void JsonSkipWhitespace(JsonParser* parser) {
    while (parser->pos < parser->len) {
        char c = parser->text[parser->pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        parser->pos++;
    }
}

static int JsonConsume(JsonParser* parser, char expected) {
    JsonSkipWhitespace(parser);
    if (parser->pos >= parser->len || parser->text[parser->pos] != expected) return 0;
    parser->pos++;
    return 1;
}

static int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int JsonReadHex4(JsonParser* parser, unsigned int* value) {
    if (parser->len - parser->pos < 4) return 0;
    unsigned int result = 0;
    for (int i = 0; i < 4; i++) {
        int digit = HexValue(parser->text[parser->pos++]);
        if (digit < 0) return 0;
        result = result * 16u + (unsigned int)digit;
    }
    *value = result;
    return 1;
}

static int AppendUtf8Codepoint(char* output, size_t capacity, size_t* used,
    unsigned int codepoint, int* overflow) {
    unsigned char bytes[4];
    int count;
    if (codepoint <= 0x7F) {
        bytes[0] = (unsigned char)codepoint;
        count = 1;
    } else if (codepoint <= 0x7FF) {
        bytes[0] = (unsigned char)(0xC0 | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80 | (codepoint & 0x3F));
        count = 2;
    } else if (codepoint <= 0xFFFF) {
        bytes[0] = (unsigned char)(0xE0 | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | (codepoint & 0x3F));
        count = 3;
    } else if (codepoint <= 0x10FFFF) {
        bytes[0] = (unsigned char)(0xF0 | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[3] = (unsigned char)(0x80 | (codepoint & 0x3F));
        count = 4;
    } else {
        return 0;
    }
    if (output) {
        if (*used + (size_t)count >= capacity) {
            *overflow = 1;
        } else {
            memcpy(output + *used, bytes, (size_t)count);
        }
    }
    *used += (size_t)count;
    return 1;
}

static int JsonParseString(JsonParser* parser, char* output, size_t capacity) {
    JsonSkipWhitespace(parser);
    if (parser->pos >= parser->len || parser->text[parser->pos++] != '"') return 0;
    size_t used = 0;
    int overflow = 0;
    while (parser->pos < parser->len) {
        unsigned char c = (unsigned char)parser->text[parser->pos++];
        if (c == '"') {
            if (output && capacity > 0) output[used < capacity ? used : capacity - 1] = 0;
            return !overflow;
        }
        if (c < 0x20) return 0;
        if (c != '\\') {
            if (output) {
                if (used + 1 >= capacity) overflow = 1;
                else output[used] = (char)c;
            }
            used++;
            continue;
        }
        if (parser->pos >= parser->len) return 0;
        char escaped = parser->text[parser->pos++];
        unsigned int codepoint;
        switch (escaped) {
        case '"': codepoint = '"'; break;
        case '\\': codepoint = '\\'; break;
        case '/': codepoint = '/'; break;
        case 'b': codepoint = '\b'; break;
        case 'f': codepoint = '\f'; break;
        case 'n': codepoint = '\n'; break;
        case 'r': codepoint = '\r'; break;
        case 't': codepoint = '\t'; break;
        case 'u': {
            if (!JsonReadHex4(parser, &codepoint)) return 0;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                if (parser->len - parser->pos < 6 || parser->text[parser->pos] != '\\' ||
                    parser->text[parser->pos + 1] != 'u') return 0;
                parser->pos += 2;
                unsigned int low;
                if (!JsonReadHex4(parser, &low) || low < 0xDC00 || low > 0xDFFF) return 0;
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
            } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                return 0;
            }
            break;
        }
        default:
            return 0;
        }
        if (!AppendUtf8Codepoint(output, capacity, &used, codepoint, &overflow)) return 0;
    }
    return 0;
}

static int JsonSkipValue(JsonParser* parser, int depth) {
    if (depth > 64) return 0;
    JsonSkipWhitespace(parser);
    if (parser->pos >= parser->len) return 0;
    char c = parser->text[parser->pos];
    if (c == '"') return JsonParseString(parser, NULL, 0);
    if (c == '{') {
        parser->pos++;
        JsonSkipWhitespace(parser);
        if (JsonConsume(parser, '}')) return 1;
        for (;;) {
            if (!JsonParseString(parser, NULL, 0) || !JsonConsume(parser, ':') ||
                !JsonSkipValue(parser, depth + 1)) return 0;
            if (JsonConsume(parser, '}')) return 1;
            if (!JsonConsume(parser, ',')) return 0;
        }
    }
    if (c == '[') {
        parser->pos++;
        JsonSkipWhitespace(parser);
        if (JsonConsume(parser, ']')) return 1;
        for (;;) {
            if (!JsonSkipValue(parser, depth + 1)) return 0;
            if (JsonConsume(parser, ']')) return 1;
            if (!JsonConsume(parser, ',')) return 0;
        }
    }
    size_t start = parser->pos;
    while (parser->pos < parser->len) {
        c = parser->text[parser->pos];
        if (c == ',' || c == ']' || c == '}' || isspace((unsigned char)c)) break;
        parser->pos++;
    }
    return parser->pos > start;
}

static int JsonParseBool(JsonParser* parser, BOOL* value) {
    JsonSkipWhitespace(parser);
    if (parser->len - parser->pos >= 4 &&
        memcmp(parser->text + parser->pos, "true", 4) == 0) {
        parser->pos += 4;
        *value = TRUE;
        return 1;
    }
    if (parser->len - parser->pos >= 5 &&
        memcmp(parser->text + parser->pos, "false", 5) == 0) {
        parser->pos += 5;
        *value = FALSE;
        return 1;
    }
    return 0;
}

static int JsonParseHostArray(JsonParser* parser, Account* account) {
    if (!JsonConsume(parser, '[')) return 0;
    account->host_count = 0;
    if (JsonConsume(parser, ']')) return 1;
    for (;;) {
        char host[HOST_LEN];
        if (!JsonParseString(parser, host, sizeof(host))) return 0;
        if (account->host_count < 10) {
            strcpy(account->host_list[account->host_count++], host);
        }
        if (JsonConsume(parser, ']')) return 1;
        if (!JsonConsume(parser, ',')) return 0;
    }
}

static int JsonParseAccount(JsonParser* parser, Account* account) {
    if (!JsonConsume(parser, '{')) return 0;
    memset(account, 0, sizeof(*account));
    int saw_host_list = 0;
    char legacy_host[HOST_LEN] = "";
    if (JsonConsume(parser, '}')) return 1;
    for (;;) {
        char key[64];
        if (!JsonParseString(parser, key, sizeof(key)) || !JsonConsume(parser, ':')) return 0;
        if (strcmp(key, "id") == 0) {
            if (!JsonParseString(parser, account->id, sizeof(account->id))) return 0;
        } else if (strcmp(key, "name") == 0) {
            if (!JsonParseString(parser, account->name, sizeof(account->name))) return 0;
        } else if (strcmp(key, "email") == 0) {
            if (!JsonParseString(parser, account->email, sizeof(account->email))) return 0;
        } else if (strcmp(key, "ssh_key_path") == 0) {
            if (!JsonParseString(parser, account->ssh_key_path, sizeof(account->ssh_key_path))) return 0;
        } else if (strcmp(key, "host_list") == 0) {
            if (!JsonParseHostArray(parser, account)) return 0;
            saw_host_list = 1;
        } else if (strcmp(key, "git_host") == 0) {
            if (!JsonParseString(parser, legacy_host, sizeof(legacy_host))) return 0;
        } else if (!JsonSkipValue(parser, 0)) {
            return 0;
        }
        if (JsonConsume(parser, '}')) break;
        if (!JsonConsume(parser, ',')) return 0;
    }
    if (!saw_host_list && legacy_host[0]) {
        strcpy(account->host_list[0], legacy_host);
        account->host_count = 1;
    }
    return 1;
}

static int JsonParseAccounts(JsonParser* parser, Config* config) {
    if (!JsonConsume(parser, '[')) return 0;
    config->account_count = 0;
    if (JsonConsume(parser, ']')) return 1;
    for (;;) {
        if (config->account_count < MAX_ACCOUNTS) {
            Account account;
            if (!JsonParseAccount(parser, &account)) return 0;
            config->accounts[config->account_count++] = account;
        } else if (!JsonSkipValue(parser, 0)) {
            return 0;
        }
        if (JsonConsume(parser, ']')) return 1;
        if (!JsonConsume(parser, ',')) return 0;
    }
}

static int ParseConfigJson(const char* data, size_t length, Config* config) {
    JsonParser parser = { data, 0, length };
    Config parsed;
    memset(&parsed, 0, sizeof(parsed));
    int saw_show_taskbar_text = 0;
    if (!JsonConsume(&parser, '{')) return 0;
    if (!JsonConsume(&parser, '}')) {
        for (;;) {
            char key[64];
            if (!JsonParseString(&parser, key, sizeof(key)) || !JsonConsume(&parser, ':')) return 0;
            if (strcmp(key, "accounts") == 0) {
                if (!JsonParseAccounts(&parser, &parsed)) return 0;
            } else if (strcmp(key, "active_id") == 0) {
                if (!JsonParseString(&parser, parsed.active_id, sizeof(parsed.active_id))) return 0;
            } else if (strcmp(key, "show_identity_badge") == 0) {
                if (!JsonParseBool(&parser, &parsed.show_identity_badge)) return 0;
            } else if (strcmp(key, "show_taskbar_text") == 0) {
                if (!JsonParseBool(&parser, &parsed.show_taskbar_text)) return 0;
                saw_show_taskbar_text = 1;
            } else if (strcmp(key, "dark_mode") == 0) {
                if (!JsonParseBool(&parser, &parsed.dark_mode)) return 0;
            } else if (!JsonSkipValue(&parser, 0)) {
                return 0;
            }
            if (JsonConsume(&parser, '}')) break;
            if (!JsonConsume(&parser, ',')) return 0;
        }
    }
    JsonSkipWhitespace(&parser);
    if (parser.pos != parser.len) return 0;
    if (!saw_show_taskbar_text) {
        parsed.show_taskbar_text = parsed.show_identity_badge;
    }
    *config = parsed;
    return 1;
}

static int SBAppendJsonString(StringBuilder* builder, const char* value) {
    static const char hex[] = "0123456789abcdef";
    if (!SBAppendChar(builder, '"')) return 0;
    for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
        switch (*p) {
        case '"': if (!SBAppend(builder, "\\\"")) return 0; break;
        case '\\': if (!SBAppend(builder, "\\\\")) return 0; break;
        case '\b': if (!SBAppend(builder, "\\b")) return 0; break;
        case '\f': if (!SBAppend(builder, "\\f")) return 0; break;
        case '\n': if (!SBAppend(builder, "\\n")) return 0; break;
        case '\r': if (!SBAppend(builder, "\\r")) return 0; break;
        case '\t': if (!SBAppend(builder, "\\t")) return 0; break;
        default:
            if (*p < 0x20) {
                char escaped[7] = { '\\', 'u', '0', '0', hex[*p >> 4], hex[*p & 0x0F], 0 };
                if (!SBAppend(builder, escaped)) return 0;
            } else if (!SBAppendChar(builder, (char)*p)) {
                return 0;
            }
        }
    }
    return SBAppendChar(builder, '"');
}

void LoadConfig(Config* config) {
    if (!config) return;
    ClearLogicError();
    memset(config, 0, sizeof(*config));
    wchar_t* path = GetAccountsPathW();
    if (!path) return;
    char* data;
    size_t length;
    int existed;
    if (!ReadFileBytesW(path, &data, &length, &existed)) {
        free(path);
        return;
    }
    free(path);
    if (existed && length > 0 && !ParseConfigJson(data, length, config)) {
        memset(config, 0, sizeof(*config));
        SetLogicError("accounts.json is not valid JSON");
    }
    free(data);
}

int SaveConfig(const Config* config) {
    ClearLogicError();
    if (!config || config->account_count < 0 || config->account_count > MAX_ACCOUNTS) {
        SetLogicError("invalid account configuration");
        return 0;
    }
    StringBuilder output = {0};
    int ok = SBAppend(&output, "{\n  \"accounts\": [\n");
    for (int i = 0; ok && i < config->account_count; i++) {
        const Account* account = &config->accounts[i];
        if (account->host_count < 0 || account->host_count > 10) {
            SetLogicError("invalid host count");
            ok = 0;
            break;
        }
        ok = SBAppend(&output, "    {\n      \"id\": ") &&
            SBAppendJsonString(&output, account->id) &&
            SBAppend(&output, ",\n      \"name\": ") &&
            SBAppendJsonString(&output, account->name) &&
            SBAppend(&output, ",\n      \"email\": ") &&
            SBAppendJsonString(&output, account->email) &&
            SBAppend(&output, ",\n      \"ssh_key_path\": ") &&
            SBAppendJsonString(&output, account->ssh_key_path) &&
            SBAppend(&output, ",\n      \"host_list\": [");
        for (int j = 0; ok && j < account->host_count; j++) {
            if (j > 0) ok = SBAppend(&output, ", ");
            if (ok) ok = SBAppendJsonString(&output, account->host_list[j]);
        }
        if (ok) ok = SBAppend(&output, "]\n    }") &&
            SBAppend(&output, i + 1 < config->account_count ? ",\n" : "\n");
    }
    if (ok) {
        ok = SBAppend(&output, "  ],\n  \"active_id\": ") &&
            SBAppendJsonString(&output, config->active_id) &&
            SBAppend(&output, config->show_identity_badge ?
                ",\n  \"show_identity_badge\": true" :
                ",\n  \"show_identity_badge\": false") &&
            SBAppend(&output, config->show_taskbar_text ?
                ",\n  \"show_taskbar_text\": true" :
                ",\n  \"show_taskbar_text\": false") &&
            SBAppend(&output, config->dark_mode ?
                ",\n  \"dark_mode\": true\n}\n" :
                ",\n  \"dark_mode\": false\n}\n");
    }
    if (!ok) {
        if (!g_logic_error[0]) SetLogicError("out of memory while saving accounts.json");
        SBFree(&output);
        return 0;
    }
    wchar_t* path = GetAccountsPathW();
    if (!path) {
        SBFree(&output);
        return 0;
    }
    ok = AtomicWriteFileW(path, output.data, output.len);
    free(path);
    SBFree(&output);
    return ok;
}

static int IsAsciiSpace(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int EqualsIgnoreCaseN(const char* left, size_t left_len, const char* right) {
    size_t right_len = strlen(right);
    if (left_len != right_len) return 0;
    for (size_t i = 0; i < left_len; i++) {
        if (tolower((unsigned char)left[i]) != tolower((unsigned char)right[i])) return 0;
    }
    return 1;
}

static void TrimSlice(const char** start, const char** end) {
    while (*start < *end && IsAsciiSpace(**start)) (*start)++;
    while (*end > *start && IsAsciiSpace((*end)[-1])) (*end)--;
}

static int IsSectionLine(const char* line, size_t length, const char* section) {
    const char* start = line;
    const char* end = line + length;
    TrimSlice(&start, &end);
    if (end - start < 3 || *start != '[' || end[-1] != ']') return 0;
    start++;
    end--;
    TrimSlice(&start, &end);
    return EqualsIgnoreCaseN(start, (size_t)(end - start), section);
}

static int GetConfigKey(const char* line, size_t length,
                        const char** key_start, size_t* key_length,
                        const char** value_start, size_t* value_length) {
    const char* start = line;
    const char* end = line + length;
    while (end > start && (end[-1] == '\r' || end[-1] == '\n')) end--;
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    if (start == end || *start == '#' || *start == ';' || *start == '[') return 0;
    const char* equals = memchr(start, '=', (size_t)(end - start));
    if (!equals) return 0;
    const char* key_end = equals;
    while (key_end > start && (key_end[-1] == ' ' || key_end[-1] == '\t')) key_end--;
    const char* value = equals + 1;
    while (value < end && (*value == ' ' || *value == '\t')) value++;
    const char* value_end = end;
    while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
    *key_start = start;
    *key_length = (size_t)(key_end - start);
    *value_start = value;
    *value_length = (size_t)(value_end - value);
    return *key_length > 0;
}

static int AppendGitQuotedValue(StringBuilder* output, const char* value) {
    if (!SBAppendChar(output, '"')) return 0;
    for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
        if (*p == '"' || *p == '\\') {
            if (!SBAppendChar(output, '\\') || !SBAppendChar(output, (char)*p)) return 0;
        } else if (*p == '\n') {
            if (!SBAppend(output, "\\n")) return 0;
        } else if (*p == '\t') {
            if (!SBAppend(output, "\\t")) return 0;
        } else if (*p == '\b') {
            if (!SBAppend(output, "\\b")) return 0;
        } else if (*p < 0x20) {
            SetLogicError("Git identity contains unsupported control characters");
            return 0;
        } else if (!SBAppendChar(output, (char)*p)) {
            return 0;
        }
    }
    return SBAppendChar(output, '"');
}

static int AppendGitIdentityLine(StringBuilder* output, const char* key,
                                 const char* value, const char* newline) {
    return SBAppend(output, "\t") && SBAppend(output, key) &&
        SBAppend(output, " = ") && AppendGitQuotedValue(output, value) &&
        SBAppend(output, newline);
}

static int EnsureOutputEndsWithNewline(StringBuilder* output, const char* newline) {
    return output->len == 0 || output->data[output->len - 1] == '\n' ||
        SBAppend(output, newline);
}

static int ValidateIdentity(const char* name, const char* email) {
    if (!name || !email || !name[0] || !email[0]) {
        SetLogicError("Git user name and email cannot be empty");
        return 0;
    }
    if (strlen(name) >= NAME_LEN || strlen(email) >= EMAIL_LEN) {
        SetLogicError("Git identity is too long");
        return 0;
    }
    for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
        if (*p < 0x20 && *p != '\t') {
            SetLogicError("Git user name contains control characters");
            return 0;
        }
    }
    for (const unsigned char* p = (const unsigned char*)email; *p; p++) {
        if (*p < 0x21 || *p == 0x7F) {
            SetLogicError("Git email contains whitespace or control characters");
            return 0;
        }
    }
    return 1;
}

static int DecodeGitValue(const char* value, size_t length, char* output, size_t capacity);

static int IsLegacyManagedSSHCommand(const char* value) {
    const char* prefix = "ssh -i \"";
    const char* suffix = "\" -o IdentitiesOnly=yes";
    size_t value_len = strlen(value);
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    return value_len > prefix_len + suffix_len &&
        _strnicmp(value, prefix, prefix_len) == 0 &&
        _stricmp(value + value_len - suffix_len, suffix) == 0;
}

static int BuildGitConfig(const char* input, size_t input_len,
                          const char* name, const char* email,
                          StringBuilder* output) {
    if (!ValidateIdentity(name, email)) return 0;
    const char* newline = (input && strstr(input, "\r\n")) ? "\r\n" : "\n";
    int in_user = 0;
    int in_core = 0;
    int saw_user = 0;
    int wrote_name = 0;
    int wrote_email = 0;
    size_t pos = 0;

    while (pos < input_len) {
        size_t line_start = pos;
        while (pos < input_len && input[pos] != '\n') pos++;
        if (pos < input_len) pos++;
        size_t line_len = pos - line_start;
        const char* line = input + line_start;

        const char* trimmed = line;
        const char* trimmed_end = line + line_len;
        TrimSlice(&trimmed, &trimmed_end);
        if (trimmed < trimmed_end && *trimmed == '[') {
            if (in_user) {
                if (!wrote_name && !AppendGitIdentityLine(output, "name", name, newline)) return 0;
                if (!wrote_email && !AppendGitIdentityLine(output, "email", email, newline)) return 0;
            }
            in_user = IsSectionLine(line, line_len, "user");
            in_core = IsSectionLine(line, line_len, "core");
            if (in_user) {
                saw_user = 1;
                wrote_name = 0;
                wrote_email = 0;
            }
            if (!SBAppendN(output, line, line_len)) return 0;
            continue;
        }

        if (in_user) {
            const char* key;
            const char* value;
            size_t key_len;
            size_t value_len;
            if (GetConfigKey(line, line_len, &key, &key_len, &value, &value_len)) {
                (void)value;
                (void)value_len;
                if (EqualsIgnoreCaseN(key, key_len, "name")) {
                    if (!wrote_name && !AppendGitIdentityLine(output, "name", name, newline)) return 0;
                    wrote_name = 1;
                    continue;
                }
                if (EqualsIgnoreCaseN(key, key_len, "email")) {
                    if (!wrote_email && !AppendGitIdentityLine(output, "email", email, newline)) return 0;
                    wrote_email = 1;
                    continue;
                }
            }
        }
        if (in_core) {
            const char* key;
            const char* value;
            size_t key_len;
            size_t value_len;
            if (GetConfigKey(line, line_len, &key, &key_len, &value, &value_len) &&
                EqualsIgnoreCaseN(key, key_len, "sshCommand")) {
                char decoded[PATH_LEN + 128];
                if (DecodeGitValue(value, value_len, decoded, sizeof(decoded)) &&
                    IsLegacyManagedSSHCommand(decoded)) {
                    continue;
                }
            }
        }
        if (!SBAppendN(output, line, line_len)) return 0;
    }

    if (in_user) {
        if (!EnsureOutputEndsWithNewline(output, newline)) return 0;
        if (!wrote_name && !AppendGitIdentityLine(output, "name", name, newline)) return 0;
        if (!wrote_email && !AppendGitIdentityLine(output, "email", email, newline)) return 0;
    }
    if (!saw_user) {
        if (output->len && output->data[output->len - 1] != '\n' && !SBAppend(output, newline)) return 0;
        if (!SBAppend(output, "[user]") || !SBAppend(output, newline) ||
            !AppendGitIdentityLine(output, "name", name, newline) ||
            !AppendGitIdentityLine(output, "email", email, newline)) return 0;
    }
    return 1;
}

static int DecodeGitValue(const char* value, size_t length, char* output, size_t capacity) {
    if (!output || capacity == 0) return 0;
    size_t used = 0;
    size_t pos = 0;
    int quoted = length >= 2 && value[0] == '"' && value[length - 1] == '"';
    if (quoted) {
        pos++;
        length--;
    }
    while (pos < length) {
        unsigned char c = (unsigned char)value[pos++];
        if (c == '\\' && pos < length) {
            c = (unsigned char)value[pos++];
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b';
        }
        if (used + 1 >= capacity) return 0;
        output[used++] = (char)c;
    }
    output[used] = 0;
    return 1;
}

void GetGlobalConfig(char* name, char* email) {
    if (!name || !email) return;
    name[0] = 0;
    email[0] = 0;
    wchar_t* path = GetGitConfigPathW();
    if (!path) return;
    char* data;
    size_t length;
    int existed;
    if (!ReadFileBytesW(path, &data, &length, &existed)) {
        free(path);
        return;
    }
    free(path);
    int in_user = 0;
    size_t pos = 0;
    while (pos < length) {
        size_t start = pos;
        while (pos < length && data[pos] != '\n') pos++;
        if (pos < length) pos++;
        size_t line_len = pos - start;
        if (IsSectionLine(data + start, line_len, "user")) {
            in_user = 1;
            continue;
        }
        const char* trimmed = data + start;
        const char* end = trimmed + line_len;
        TrimSlice(&trimmed, &end);
        if (trimmed < end && *trimmed == '[') {
            in_user = 0;
            continue;
        }
        if (!in_user) continue;
        const char* key;
        const char* value;
        size_t key_len;
        size_t value_len;
        if (!GetConfigKey(data + start, line_len, &key, &key_len, &value, &value_len)) continue;
        if (EqualsIgnoreCaseN(key, key_len, "name")) {
            DecodeGitValue(value, value_len, name, NAME_LEN);
        } else if (EqualsIgnoreCaseN(key, key_len, "email")) {
            DecodeGitValue(value, value_len, email, EMAIL_LEN);
        }
    }
    free(data);
}

int SetGlobalConfig(const char* name, const char* email, const char* sshKeyPath) {
    (void)sshKeyPath;
    ClearLogicError();
    wchar_t* path = GetGitConfigPathW();
    if (!path) return 0;
    char* original;
    size_t original_len;
    int existed;
    if (!ReadFileBytesW(path, &original, &original_len, &existed)) {
        free(path);
        return 0;
    }
    StringBuilder updated = {0};
    int ok = BuildGitConfig(original, original_len, name, email, &updated);
    free(original);
    if (ok) ok = AtomicWriteFileW(path, updated.data, updated.len);
    if (!ok && !g_logic_error[0]) SetLogicError("cannot update the global Git config");
    SBFree(&updated);
    free(path);
    return ok;
}

void AutoImportGlobalIdentity(Config* config) {
    if (!config || config->account_count > 0) return;
    char name[NAME_LEN] = "";
    char email[EMAIL_LEN] = "";
    GetGlobalConfig(name, email);
    if (!name[0] || !email[0]) return;

    Account* account = &config->accounts[0];
    memset(account, 0, sizeof(*account));
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    unsigned long long id = ((unsigned long long)now.dwHighDateTime << 32) | now.dwLowDateTime;
    snprintf(account->id, sizeof(account->id), "%llu", id);
    strncpy(account->name, name, sizeof(account->name) - 1);
    strncpy(account->email, email, sizeof(account->email) - 1);
    strcpy(account->host_list[0], "github.com");
    account->host_count = 1;
    config->account_count = 1;
    strcpy(config->active_id, account->id);
}

static int ParsePort(const char* text, int* port) {
    if (!text || !*text || strlen(text) > 5) return 0;
    unsigned int value = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        if (!isdigit(*p)) return 0;
        value = value * 10u + (unsigned int)(*p - '0');
    }
    if (value == 0 || value > 65535u) return 0;
    *port = (int)value;
    return 1;
}

static int ParseHostSpec(const char* value, HostSpec* result) {
    if (!value || !value[0] || strlen(value) >= HOST_LEN) return 0;
    memset(result, 0, sizeof(*result));
    const char* host_start = value;
    const char* host_end = value + strlen(value);
    const char* port_start = NULL;

    if (value[0] == '[') {
        const char* close = strchr(value + 1, ']');
        if (!close || close == value + 1) return 0;
        host_start = value + 1;
        host_end = close;
        result->is_ipv6 = 1;
        if (close[1]) {
            if (close[1] != ':' || !close[2]) return 0;
            port_start = close + 2;
        }
    } else {
        const char* first_colon = strchr(value, ':');
        const char* last_colon = strrchr(value, ':');
        if (first_colon && first_colon == last_colon) {
            host_end = first_colon;
            port_start = first_colon + 1;
        } else if (first_colon) {
            result->is_ipv6 = 1;
        }
    }

    size_t host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(result->host)) return 0;
    int ipv6_zone = 0;
    for (size_t i = 0; i < host_len; i++) {
        unsigned char c = (unsigned char)host_start[i];
        if (c <= 0x20 || c == 0x7F || c == '#' || c == '"' || c == '\'' ||
            c == '*' || c == '?' || c == '!' || c == '/' || c == '\\' || c == '@') return 0;
        if (!result->is_ipv6 && !(isalnum(c) || c == '.' || c == '-' || c == '_')) return 0;
        if (result->is_ipv6) {
            if (c == '%') {
                if (ipv6_zone || i == 0 || i + 1 == host_len) return 0;
                ipv6_zone = 1;
            } else if (!ipv6_zone && !(isxdigit(c) || c == ':' || c == '.')) {
                return 0;
            } else if (ipv6_zone && !(isalnum(c) || c == '-' || c == '_')) {
                return 0;
            }
        }
    }
    memcpy(result->host, host_start, host_len);
    result->host[host_len] = 0;
    if (port_start) {
        if (!ParsePort(port_start, &result->port)) return 0;
        result->has_port = 1;
    }
    return 1;
}

int ValidateSSHHost(const char* host) {
    ClearLogicError();
    HostSpec parsed;
    if (!ParseHostSpec(host, &parsed)) {
        SetLogicError("invalid SSH host or port: %s", host ? host : "");
        return 0;
    }
    return 1;
}

int ValidateSSHHostList(const char hosts[][HOST_LEN], int hostCount) {
    ClearLogicError();
    if (hostCount < 0 || hostCount > 10 || (hostCount > 0 && !hosts)) {
        SetLogicError("invalid SSH host count");
        return 0;
    }
    HostSpec parsed[10];
    for (int i = 0; i < hostCount; i++) {
        if (!ParseHostSpec(hosts[i], &parsed[i])) {
            SetLogicError("invalid SSH host or port: %s", hosts[i]);
            return 0;
        }
        for (int j = 0; j < i; j++) {
            if (_stricmp(parsed[i].host, parsed[j].host) == 0) {
                SetLogicError("duplicate SSH host: %s", parsed[i].host);
                return 0;
            }
        }
    }
    return 1;
}

static int IsPrivateKeyFileW(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    ULARGE_INTEGER size;
    size.LowPart = attributes.nFileSizeLow;
    size.HighPart = attributes.nFileSizeHigh;
    if (size.QuadPart == 0 || size.QuadPart > MAX_PRIVATE_KEY_SIZE) return 0;

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    char header[4097];
    DWORD read = 0;
    int ok = ReadFile(file, header, sizeof(header) - 1, &read, NULL) && read > 0;
    CloseHandle(file);
    if (!ok) return 0;
    header[read] = 0;
    return strstr(header, "-----BEGIN OPENSSH PRIVATE KEY-----") != NULL ||
        strstr(header, "-----BEGIN RSA PRIVATE KEY-----") != NULL ||
        strstr(header, "-----BEGIN EC PRIVATE KEY-----") != NULL ||
        strstr(header, "-----BEGIN DSA PRIVATE KEY-----") != NULL ||
        strstr(header, "-----BEGIN PRIVATE KEY-----") != NULL ||
        strstr(header, "-----BEGIN ENCRYPTED PRIVATE KEY-----") != NULL;
}

int ValidateSSHPrivateKey(const char* keyPath) {
    ClearLogicError();
    wchar_t* path = Utf8ToWideAlloc(keyPath);
    if (!path) {
        SetLogicError("invalid UTF-8 SSH key path");
        return 0;
    }
    int valid = IsPrivateKeyFileW(path);
    free(path);
    if (!valid) SetLogicError("the selected file is not a readable SSH private key");
    return valid;
}

int GetSSHKeys(char keys[][PATH_LEN], int maxKeys) {
    ClearLogicError();
    if (!keys || maxKeys <= 0) return 0;
    wchar_t* ssh_dir = GetSSHDirectoryW();
    if (!ssh_dir) return 0;
    wchar_t* pattern = JoinPathW(ssh_dir, L"id_*");
    if (!pattern) {
        free(ssh_dir);
        SetLogicError("out of memory");
        return 0;
    }
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE) {
        free(ssh_dir);
        return 0;
    }
    int count = 0;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || count >= maxKeys) continue;
        wchar_t* full_path = JoinPathW(ssh_dir, found.cFileName);
        if (!full_path) continue;
        if (IsPrivateKeyFileW(full_path)) {
            char* utf8 = WideToUtf8Alloc(full_path);
            if (utf8) {
                if (strlen(utf8) < PATH_LEN) strcpy(keys[count++], utf8);
                free(utf8);
            }
        }
        free(full_path);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    free(ssh_dir);
    return count;
}

static int LineStartsWith(const char* line, size_t length, const char* prefix) {
    const char* start = line;
    const char* end = line + length;
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    size_t prefix_len = strlen(prefix);
    return (size_t)(end - start) >= prefix_len && memcmp(start, prefix, prefix_len) == 0;
}

static int IsBlankLine(const char* line, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n') return 0;
    }
    return 1;
}

static int StripManagedSSHBlocks(const char* input, size_t input_len,
                                 StringBuilder* output) {
    size_t pos = 0;
    int in_new_block = 0;
    int in_legacy_block = 0;
    int legacy_saw_host = 0;

    while (pos < input_len) {
        size_t start = pos;
        while (pos < input_len && input[pos] != '\n') pos++;
        if (pos < input_len) pos++;
        size_t length = pos - start;
        const char* line = input + start;

        if (in_new_block) {
            if (LineStartsWith(line, length, GAM_END_MARKER)) in_new_block = 0;
            continue;
        }
        if (LineStartsWith(line, length, GAM_BEGIN_MARKER)) {
            if (strstr(input + pos, GAM_END_MARKER)) {
                in_new_block = 1;
                continue;
            }
            if (!SBAppendN(output, line, length)) return 0;
            continue;
        }

        if (in_legacy_block) {
            if (legacy_saw_host && IsBlankLine(line, length)) {
                in_legacy_block = 0;
                legacy_saw_host = 0;
                continue;
            }
            if (LineStartsWith(line, length, "Host ")) {
                if (legacy_saw_host) {
                    in_legacy_block = 0;
                    legacy_saw_host = 0;
                } else {
                    legacy_saw_host = 1;
                    continue;
                }
            } else if (legacy_saw_host && LineStartsWith(line, length, "Match ")) {
                in_legacy_block = 0;
                legacy_saw_host = 0;
            } else {
                continue;
            }
        }

        if (LineStartsWith(line, length, GAM_OLD_MARKER) &&
            !LineStartsWith(line, length, GAM_BEGIN_MARKER)) {
            in_legacy_block = 1;
            legacy_saw_host = 0;
            continue;
        }
        if (!SBAppendN(output, line, length)) return 0;
    }
    return 1;
}

static int AppendSSHQuotedPath(StringBuilder* output, const char* path) {
    if (!SBAppendChar(output, '"')) return 0;
    for (const unsigned char* p = (const unsigned char*)path; *p; p++) {
        if (*p < 0x20 || *p == 0x7F || *p == '"') {
            SetLogicError("SSH key path contains unsupported characters");
            return 0;
        }
        char c = *p == '\\' ? '/' : (char)*p;
        if (!SBAppendChar(output, c)) return 0;
    }
    return SBAppendChar(output, '"');
}

static int BuildSSHConfig(const char* input, size_t input_len,
                          const char* keyPath,
                          const char hosts[][HOST_LEN], int hostCount,
                          StringBuilder* output) {
    if (!StripManagedSSHBlocks(input, input_len, output)) return 0;
    if (!keyPath || !keyPath[0] || hostCount == 0) return 1;
    if (!ValidateSSHPrivateKey(keyPath) || !ValidateSSHHostList(hosts, hostCount)) return 0;

    if (output->len > 0 && output->data[output->len - 1] != '\n' && !SBAppendChar(output, '\n')) return 0;
    if (output->len > 0 && (output->len < 2 || output->data[output->len - 2] != '\n') &&
        !SBAppendChar(output, '\n')) return 0;
    if (!SBAppend(output, GAM_BEGIN_MARKER "\n") ||
        !SBAppend(output, "# 此区域由 Git Account Manager 自动维护，请在区域外编辑自定义配置。\n")) return 0;

    for (int i = 0; i < hostCount; i++) {
        HostSpec parsed;
        if (!ParseHostSpec(hosts[i], &parsed)) return 0;
        if (i > 0 && !SBAppendChar(output, '\n')) return 0;
        if (!SBAppend(output, "Host ") || !SBAppend(output, parsed.host) ||
            !SBAppend(output, "\n    HostName ") || !SBAppend(output, parsed.host) ||
            !SBAppend(output, "\n    User git\n")) return 0;
        if (parsed.has_port && !SBAppendFormat(output, "    Port %d\n", parsed.port)) return 0;
        if (!SBAppend(output, "    IdentityFile ") || !AppendSSHQuotedPath(output, keyPath) ||
            !SBAppend(output, "\n    IdentitiesOnly yes\n")) return 0;
    }
    return SBAppend(output, GAM_END_MARKER "\n");
}

static int PrepareSSHUpdate(const char* keyPath,
                            const char hosts[][HOST_LEN], int hostCount,
                            wchar_t** path, char** original, size_t* original_len,
                            int* original_existed, StringBuilder* updated) {
    *path = NULL;
    *original = NULL;
    wchar_t* ssh_dir = GetSSHDirectoryW();
    if (!ssh_dir) return 0;
    if (!EnsureDirectoryW(ssh_dir)) {
        free(ssh_dir);
        return 0;
    }
    *path = JoinPathW(ssh_dir, L"config");
    free(ssh_dir);
    if (!*path) {
        SetLogicError("out of memory");
        return 0;
    }
    if (!ReadFileBytesW(*path, original, original_len, original_existed)) return 0;
    if (!BuildSSHConfig(*original, *original_len, keyPath, hosts, hostCount, updated)) return 0;
    return 1;
}

int SwitchAccountSSHConfig(const char* keyPath, const char hosts[][HOST_LEN], int hostCount) {
    ClearLogicError();
    if (!keyPath || !keyPath[0]) return ClearAllManagedSSHConfig();
    wchar_t* path;
    char* original;
    size_t original_len;
    int original_existed;
    StringBuilder updated = {0};
    int ok = PrepareSSHUpdate(keyPath, hosts, hostCount, &path, &original,
                              &original_len, &original_existed, &updated);
    (void)original_existed;
    if (ok) ok = AtomicWriteFileW(path, updated.data ? updated.data : "", updated.len);
    free(path);
    free(original);
    SBFree(&updated);
    if (!ok && !g_logic_error[0]) SetLogicError("cannot update SSH config");
    return ok;
}

int ClearAllManagedSSHConfig(void) {
    ClearLogicError();
    wchar_t* path;
    char* original;
    size_t original_len;
    int original_existed;
    StringBuilder updated = {0};
    int ok = PrepareSSHUpdate(NULL, NULL, 0, &path, &original,
                              &original_len, &original_existed, &updated);
    if (ok && original_existed) ok = AtomicWriteFileW(path, updated.data ? updated.data : "", updated.len);
    free(path);
    free(original);
    SBFree(&updated);
    return ok;
}

int CleanupSSHConfigForKey(const char* keyPath, const char* email,
                           const char* const* keepHosts, int keepHostCount) {
    (void)keyPath;
    (void)email;
    (void)keepHosts;
    (void)keepHostCount;
    return ClearAllManagedSSHConfig();
}

int AddMultipleHostsToSSHConfig(const char* keyPath, const char* email,
                                const char hosts[][HOST_LEN], int hostCount) {
    (void)email;
    return SwitchAccountSSHConfig(keyPath, hosts, hostCount);
}

int AddExistingKeyToSSHConfig(const char* keyPath, const char* email, const char* host) {
    (void)email;
    char hosts[1][HOST_LEN];
    if (!host || strlen(host) >= HOST_LEN) {
        SetLogicError("invalid SSH host");
        return 0;
    }
    strcpy(hosts[0], host);
    return SwitchAccountSSHConfig(keyPath, (const char (*)[HOST_LEN])hosts, 1);
}

static wchar_t* ExpandIdentityPathW(const char* value) {
    const char* start = value;
    size_t length = strlen(value);
    while (length > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        length--;
    }
    while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t' ||
                           start[length - 1] == '\r' || start[length - 1] == '\n')) length--;
    if (length >= 2 && start[0] == '"' && start[length - 1] == '"') {
        start++;
        length -= 2;
    }
    char* copy = DupBytes(start, length);
    if (!copy) return NULL;
    for (size_t i = 0; i < length; i++) if (copy[i] == '/') copy[i] = '\\';

    wchar_t* result = NULL;
    if (copy[0] == '~' && copy[1] == '\\') {
        wchar_t* profile = GetProfileDirectoryW();
        wchar_t* tail = Utf8ToWideAlloc(copy + 2);
        if (profile && tail) result = JoinPathW(profile, tail);
        free(profile);
        free(tail);
    } else {
        result = Utf8ToWideAlloc(copy);
    }
    free(copy);
    if (!result) return NULL;
    DWORD needed = GetFullPathNameW(result, 0, NULL, NULL);
    if (needed == 0) return result;
    wchar_t* full = (wchar_t*)malloc((size_t)needed * sizeof(wchar_t));
    if (full && GetFullPathNameW(result, needed, full, NULL)) {
        free(result);
        return full;
    }
    free(full);
    return result;
}

int GetHostFromSSHConfig(const char* keyPath, char* outHost, int maxLen) {
    if (!outHost || maxLen <= 0) return 0;
    outHost[0] = 0;
    if (!keyPath || !keyPath[0]) return 0;
    wchar_t* wanted = ExpandIdentityPathW(keyPath);
    wchar_t* config_path = GetSSHConfigPathW();
    if (!wanted || !config_path) {
        free(wanted);
        free(config_path);
        return 0;
    }
    char* data;
    size_t length;
    int existed;
    if (!ReadFileBytesW(config_path, &data, &length, &existed)) {
        free(wanted);
        free(config_path);
        return 0;
    }
    free(config_path);
    if (!existed) {
        free(wanted);
        free(data);
        return 0;
    }

    char current_host[HOST_LEN] = "";
    size_t pos = 0;
    int found = 0;
    while (pos < length && !found) {
        size_t start = pos;
        while (pos < length && data[pos] != '\n') pos++;
        if (pos < length) pos++;
        const char* line = data + start;
        size_t line_len = pos - start;
        const char* trimmed = line;
        const char* end = line + line_len;
        TrimSlice(&trimmed, &end);
        if ((size_t)(end - trimmed) > 5 && _strnicmp(trimmed, "Host ", 5) == 0) {
            const char* host = trimmed + 5;
            while (host < end && (*host == ' ' || *host == '\t')) host++;
            const char* host_end = host;
            while (host_end < end && *host_end != ' ' && *host_end != '\t') host_end++;
            size_t host_len = (size_t)(host_end - host);
            if (host_len < sizeof(current_host)) {
                memcpy(current_host, host, host_len);
                current_host[host_len] = 0;
            }
        } else if ((size_t)(end - trimmed) > 12 &&
                   _strnicmp(trimmed, "IdentityFile", 12) == 0 &&
                   (trimmed[12] == ' ' || trimmed[12] == '\t')) {
            const char* path_value = trimmed + 12;
            while (path_value < end && (*path_value == ' ' || *path_value == '\t')) path_value++;
            char* encoded = DupBytes(path_value, (size_t)(end - path_value));
            wchar_t* candidate = encoded ? ExpandIdentityPathW(encoded) : NULL;
            free(encoded);
            if (candidate && _wcsicmp(candidate, wanted) == 0 && current_host[0]) {
                strncpy(outHost, current_host, (size_t)maxLen - 1);
                outHost[maxLen - 1] = 0;
                found = 1;
            }
            free(candidate);
        }
    }
    free(wanted);
    free(data);
    return found;
}

static int IsSafeKeyName(const char* name) {
    if (!name || !name[0] || strlen(name) >= 64 || name[0] == '.') return 0;
    for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) return 0;
    }
    static const char* reserved[] = {
        "config", "known_hosts", "known_hosts2", "authorized_keys",
        "authorized_keys2", "environment", "rc", "con", "prn", "aux", "nul"
    };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (_stricmp(name, reserved[i]) == 0) return 0;
    }
    size_t len = strlen(name);
    if (name[len - 1] == '.' || (len >= 4 && _stricmp(name + len - 4, ".pub") == 0)) return 0;
    char base[16];
    size_t base_len = strcspn(name, ".");
    if (base_len >= sizeof(base)) base_len = sizeof(base) - 1;
    memcpy(base, name, base_len);
    base[base_len] = 0;
    if (_stricmp(base, "con") == 0 || _stricmp(base, "prn") == 0 ||
        _stricmp(base, "aux") == 0 || _stricmp(base, "nul") == 0) return 0;
    if ((strlen(base) == 4 && (_strnicmp(base, "com", 3) == 0 ||
                               _strnicmp(base, "lpt", 3) == 0)) &&
        base[3] >= '1' && base[3] <= '9') return 0;
    return 1;
}

static wchar_t* FindSSHKeygenW(void) {
    wchar_t system_dir[MAX_PATH];
    UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        wchar_t* openssh = JoinPathW(system_dir, L"OpenSSH\\ssh-keygen.exe");
        if (openssh && GetFileAttributesW(openssh) != INVALID_FILE_ATTRIBUTES) return openssh;
        free(openssh);
    }
    DWORD env_needed = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t* env_path = env_needed ? (wchar_t*)malloc((size_t)env_needed * sizeof(wchar_t)) : NULL;
    if (env_path && GetEnvironmentVariableW(L"PATH", env_path, env_needed)) {
        DWORD needed = SearchPathW(env_path, L"ssh-keygen.exe", NULL, 0, NULL, NULL);
        if (needed > 0) {
            wchar_t* path = (wchar_t*)malloc(((size_t)needed + 1) * sizeof(wchar_t));
            if (path && SearchPathW(env_path, L"ssh-keygen.exe", NULL,
                                    needed + 1, path, NULL)) {
                free(env_path);
                return path;
            }
            free(path);
        }
    }
    free(env_path);
    return NULL;
}

int GenerateSSHKey(const char* name, const char* email, const char* type, char* outPath) {
    ClearLogicError();
    if (outPath) outPath[0] = 0;
    if (!IsSafeKeyName(name)) {
        SetLogicError("invalid SSH key file name");
        return 0;
    }
    if (!type || (_stricmp(type, "ed25519") != 0 && _stricmp(type, "rsa") != 0)) {
        SetLogicError("unsupported SSH key type");
        return 0;
    }
    if (!email || strlen(email) >= EMAIL_LEN) {
        SetLogicError("invalid SSH key comment");
        return 0;
    }
    for (const unsigned char* p = (const unsigned char*)email; *p; p++) {
        if (*p < 0x20 || *p == 0x7F || *p == '"') {
            SetLogicError("SSH key comment contains unsupported characters");
            return 0;
        }
    }

    wchar_t* ssh_dir = GetSSHDirectoryW();
    wchar_t* wide_name = Utf8ToWideAlloc(name);
    wchar_t* wide_email = Utf8ToWideAlloc(email);
    wchar_t* wide_type = Utf8ToWideAlloc(type);
    if (!ssh_dir || !wide_name || !wide_email || !wide_type || !EnsureDirectoryW(ssh_dir)) {
        free(ssh_dir); free(wide_name); free(wide_email); free(wide_type);
        if (!g_logic_error[0]) SetLogicError("cannot prepare SSH key path");
        return 0;
    }
    wchar_t* key_path = JoinPathW(ssh_dir, wide_name);
    free(ssh_dir);
    free(wide_name);
    if (!key_path) {
        free(wide_email); free(wide_type);
        SetLogicError("out of memory");
        return 0;
    }
    size_t key_len = wcslen(key_path);
    wchar_t* pub_path = (wchar_t*)malloc((key_len + 5) * sizeof(wchar_t));
    if (!pub_path) {
        free(key_path); free(wide_email); free(wide_type);
        SetLogicError("out of memory");
        return 0;
    }
    swprintf(pub_path, key_len + 5, L"%ls.pub", key_path);
    if (GetFileAttributesW(key_path) != INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(pub_path) != INVALID_FILE_ATTRIBUTES) {
        free(key_path); free(pub_path); free(wide_email); free(wide_type);
        SetLogicError("SSH key or public key already exists");
        return 0;
    }
    wchar_t* executable = FindSSHKeygenW();
    if (!executable) {
        free(key_path); free(pub_path); free(wide_email); free(wide_type);
        SetLogicError("ssh-keygen.exe was not found");
        return 0;
    }
    size_t command_cap = wcslen(executable) + wcslen(wide_type) +
        wcslen(wide_email) + wcslen(key_path) + 80;
    wchar_t* command = (wchar_t*)malloc(command_cap * sizeof(wchar_t));
    if (!command) {
        free(executable); free(key_path); free(pub_path); free(wide_email); free(wide_type);
        SetLogicError("out of memory");
        return 0;
    }
    swprintf(command, command_cap, L"\"%ls\" -q -t %ls -C \"%ls\" -f \"%ls\" -N \"\"",
             executable, wide_type, wide_email, key_path);
    free(wide_email);
    free(wide_type);

    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    int ok = CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                            NULL, NULL, &startup, &process) != FALSE;
    free(command);
    free(executable);
    if (ok) {
        DWORD wait = WaitForSingleObject(process.hProcess, SSH_KEYGEN_TIMEOUT_MS);
        if (wait == WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            SetLogicError("ssh-keygen timed out");
            ok = 0;
        } else if (wait != WAIT_OBJECT_0) {
            SetWindowsError("wait for ssh-keygen", GetLastError());
            ok = 0;
        }
        DWORD exit_code = 1;
        if (ok && (!GetExitCodeProcess(process.hProcess, &exit_code) || exit_code != 0)) {
            SetLogicError("ssh-keygen failed with exit code %lu", (unsigned long)exit_code);
            ok = 0;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        SetWindowsError("start ssh-keygen", GetLastError());
    }
    if (ok && (!IsPrivateKeyFileW(key_path) ||
               GetFileAttributesW(pub_path) == INVALID_FILE_ATTRIBUTES)) {
        SetLogicError("ssh-keygen did not create a complete key pair");
        ok = 0;
    }
    if (!ok) {
        DeleteFileW(key_path);
        DeleteFileW(pub_path);
    } else if (outPath) {
        char* utf8 = WideToUtf8Alloc(key_path);
        if (!utf8 || strlen(utf8) >= PATH_LEN) {
            free(utf8);
            DeleteFileW(key_path);
            DeleteFileW(pub_path);
            SetLogicError("generated SSH key path is too long");
            ok = 0;
        } else {
            strcpy(outPath, utf8);
            free(utf8);
        }
    }
    free(key_path);
    free(pub_path);
    return ok;
}

int GenerateSSHKeyAndUpdateConfig(const char* name, const char* email,
                                  const char* type, char* outPath, const char* host) {
    (void)host;
    return GenerateSSHKey(name, email, type, outPath);
}

int ApplyAccountSettings(const char* name, const char* email,
                         const char* sshKeyPath,
                         const char hosts[][HOST_LEN], int hostCount) {
    ClearLogicError();
    if (!ValidateIdentity(name, email)) return 0;
    if (sshKeyPath && sshKeyPath[0]) {
        if (!ValidateSSHPrivateKey(sshKeyPath) ||
            !ValidateSSHHostList(hosts, hostCount)) return 0;
    }

    wchar_t* git_path = GetGitConfigPathW();
    if (!git_path) return 0;
    wchar_t* ssh_path = NULL;
    char* ssh_original = NULL;
    size_t ssh_original_len = 0;
    int ssh_original_existed = 0;
    StringBuilder ssh_updated = {0};
    if (!PrepareSSHUpdate(sshKeyPath, hosts, sshKeyPath && sshKeyPath[0] ? hostCount : 0,
                          &ssh_path, &ssh_original, &ssh_original_len,
                          &ssh_original_existed, &ssh_updated)) {
        free(git_path); free(ssh_path); free(ssh_original); SBFree(&ssh_updated);
        return 0;
    }
    FileSnapshot git_snapshot;
    if (!TakeFileSnapshot(git_path, &git_snapshot)) {
        free(git_path); free(ssh_path); free(ssh_original); SBFree(&ssh_updated);
        return 0;
    }
    StringBuilder git_updated = {0};
    int ok = BuildGitConfig(git_snapshot.data, git_snapshot.len, name, email, &git_updated);
    if (ok) ok = AtomicWriteFileW(git_path, git_updated.data, git_updated.len);
    if (ok) ok = AtomicWriteFileW(ssh_path, ssh_updated.data ? ssh_updated.data : "", ssh_updated.len);
    if (!ok) {
        char original_error[sizeof(g_logic_error)];
        strncpy(original_error, g_logic_error, sizeof(original_error) - 1);
        original_error[sizeof(original_error) - 1] = 0;
        if (!RestoreFileSnapshot(&git_snapshot)) {
            SetLogicError("%s; Git config rollback also failed", original_error);
        } else {
            strncpy(g_logic_error, original_error, sizeof(g_logic_error) - 1);
            g_logic_error[sizeof(g_logic_error) - 1] = 0;
        }
    }
    FreeFileSnapshot(&git_snapshot);
    free(git_path);
    free(ssh_path);
    free(ssh_original);
    (void)ssh_original_len;
    (void)ssh_original_existed;
    SBFree(&ssh_updated);
    SBFree(&git_updated);
    return ok;
}
