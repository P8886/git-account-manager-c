#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../logic.h"

static int g_checks = 0;
static int g_failures = 0;
static wchar_t g_root[MAX_PATH];
static wchar_t g_profile[MAX_PATH];
static wchar_t g_app_data[MAX_PATH];

#define CHECK(condition, message) do { \
    g_checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); \
        g_failures++; \
    } \
} while (0)

static void JoinPath(const wchar_t* base, const wchar_t* leaf,
                     wchar_t* output, size_t capacity) {
    _snwprintf(output, capacity - 1, L"%ls\\%ls", base, leaf);
    output[capacity - 1] = 0;
}

static void EnsureDirectory(const wchar_t* path) {
    int result = SHCreateDirectoryExW(NULL, path, NULL);
    CHECK(result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
          result == ERROR_FILE_EXISTS, "create test directory");
}

static int WriteBytes(const wchar_t* path, const char* data, size_t length) {
    FILE* file = _wfopen(path, L"wb");
    if (!file) return 0;
    int ok = fwrite(data, 1, length, file) == length;
    ok = fclose(file) == 0 && ok;
    return ok;
}

static char* ReadBytes(const wchar_t* path, size_t* length) {
    FILE* file = _wfopen(path, L"rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) { fclose(file); return NULL; }
    char* data = (char*)malloc((size_t)size + 1);
    if (!data) { fclose(file); return NULL; }
    size_t read = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) { free(data); return NULL; }
    data[read] = 0;
    if (length) *length = read;
    return data;
}

static void WritePrivateKey(const wchar_t* path) {
    const char* key = "-----BEGIN OPENSSH PRIVATE KEY-----\n"
                      "test-fixture-not-a-real-secret\n"
                      "-----END OPENSSH PRIVATE KEY-----\n";
    CHECK(WriteBytes(path, key, strlen(key)), "write private key fixture");
}

static int Contains(const char* text, const char* fragment) {
    return text && strstr(text, fragment) != NULL;
}

static void RemoveTree(const wchar_t* path) {
    wchar_t pattern[MAX_PATH];
    JoinPath(path, L"*", pattern, MAX_PATH);
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            wchar_t child[MAX_PATH];
            JoinPath(path, data.cFileName, child, MAX_PATH);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(child);
            else {
                SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child);
            }
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    RemoveDirectoryW(path);
}

static void PrepareSandbox(void) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    _snwprintf(g_root, MAX_PATH - 1, L"%lsgam-tests-%lu-%lu", temp,
               (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    JoinPath(g_root, L"profile", g_profile, MAX_PATH);
    JoinPath(g_root, L"appdata", g_app_data, MAX_PATH);
    EnsureDirectory(g_profile);
    EnsureDirectory(g_app_data);
    _wputenv_s(L"GAM_TEST_PROFILE_DIR", g_profile);
    _wputenv_s(L"GAM_TEST_CONFIG_DIR", g_app_data);

    char config_dir[PATH_LEN];
    GetConfigDir(config_dir, sizeof(config_dir));
    CHECK(config_dir[0] != 0, "test config directory is injectable");
}

static void TestConfigRoundTrip(void) {
    Config saved;
    memset(&saved, 0, sizeof(saved));
    saved.show_identity_badge = TRUE;
    saved.show_taskbar_text = TRUE;
    saved.dark_mode = TRUE;
    saved.account_count = 1;
    strcpy(saved.active_id, "active-1");
    Account* account = &saved.accounts[0];
    strcpy(account->id, "active-1");
    strcpy(account->name, "测试 \"用户\"");
    strcpy(account->email, "user@example.com");
    strcpy(account->ssh_key_path, "C:\\密钥\\id_test");
    strcpy(account->host_list[0], "github.com");
    strcpy(account->host_list[1], "git.example.com:2222");
    account->host_count = 2;
    CHECK(SaveConfig(&saved), "save config round trip");

    Config loaded;
    LoadConfig(&loaded);
    CHECK(loaded.account_count == 1, "load account count");
    CHECK(loaded.show_identity_badge && loaded.show_taskbar_text && loaded.dark_mode,
          "load UI settings");
    CHECK(strcmp(loaded.accounts[0].name, account->name) == 0, "JSON string escaping");
    CHECK(loaded.accounts[0].host_count == 2, "load all hosts");

    wchar_t accounts_path[MAX_PATH];
    JoinPath(g_app_data, L"accounts.json", accounts_path, MAX_PATH);
    const char* reordered =
        "{\"dark_mode\":false,\"accounts\":[{\"host_list\":[\"a.example:22\"],"
        "\"email\":\"a@b.co\",\"id\":\"x\",\"ssh_key_path\":\"\","
        "\"name\":\"A\"}],\"show_identity_badge\":true,"
        "\"show_taskbar_text\":false,\"active_id\":\"x\"}";
    CHECK(WriteBytes(accounts_path, reordered, strlen(reordered)), "write reordered config");
    LoadConfig(&loaded);
    CHECK(loaded.account_count == 1 && loaded.accounts[0].host_count == 1,
          "host list survives arbitrary field order");
    CHECK(strcmp(loaded.accounts[0].host_list[0], "a.example:22") == 0,
          "reordered host value");
    CHECK(!loaded.show_taskbar_text, "taskbar text setting can be disabled");

    const char* legacySettings =
        "{\"accounts\":[],\"show_identity_badge\":true,\"dark_mode\":true}";
    CHECK(WriteBytes(accounts_path, legacySettings, strlen(legacySettings)),
          "write legacy UI settings");
    LoadConfig(&loaded);
    CHECK(loaded.show_taskbar_text, "legacy config defaults taskbar text on");

    const char* legacyDisabled =
        "{\"accounts\":[],\"show_identity_badge\":false,\"dark_mode\":true}";
    CHECK(WriteBytes(accounts_path, legacyDisabled, strlen(legacyDisabled)),
          "write disabled legacy UI settings");
    LoadConfig(&loaded);
    CHECK(!loaded.show_taskbar_text,
          "disabled legacy config keeps taskbar text off");

    CHECK(DeleteFileW(accounts_path), "remove config for new install defaults");
    LoadConfig(&loaded);
    CHECK(!loaded.show_identity_badge && !loaded.show_taskbar_text,
          "new install keeps taskbar reminder off");

    CHECK(WriteBytes(accounts_path, "{broken", 7), "write malformed config");
    LoadConfig(&loaded);
    CHECK(loaded.account_count == 0, "malformed config fails closed");
    CHECK(GetLogicErrorMessage()[0] != 0, "malformed config reports an error");
}

static void TestGitConfig(void) {
    wchar_t path[MAX_PATH];
    JoinPath(g_profile, L".gitconfig", path, MAX_PATH);
    size_t capacity = 20000;
    char* input = (char*)malloc(capacity);
    strcpy(input, "[core]\n\tsshCommand = custom-ssh --flag\n[alias]\n\tlong = ");
    size_t used = strlen(input);
    memset(input + used, 'x', 12000);
    used += 12000;
    strcpy(input + used, "\n[user]\n\tname = Old\n\temail = old@example.com");
    used += strlen(input + used);
    CHECK(WriteBytes(path, input, used), "write large git config");
    free(input);

    CHECK(SetGlobalConfig("新用户", "new@example.com", "ignored-key"),
          "update large git config");
    size_t result_len = 0;
    char* result = ReadBytes(path, &result_len);
    CHECK(result_len > 12000, "large git config was not truncated");
    CHECK(Contains(result, "sshCommand = custom-ssh --flag"),
          "user core.sshCommand is preserved");
    CHECK(Contains(result, "[alias]"), "unrelated git section is preserved");
    free(result);

    char name[NAME_LEN], email[EMAIL_LEN];
    GetGlobalConfig(name, email);
    CHECK(strcmp(name, "新用户") == 0, "read updated UTF-8 Git name");
    CHECK(strcmp(email, "new@example.com") == 0, "read updated Git email");

    const char* legacy = "[core]\n\tsshCommand = ssh -i \"C:/old/id_key\" -o IdentitiesOnly=yes\n"
                         "[user]\n\tname = Old\n\temail = old@example.com\n";
    CHECK(WriteBytes(path, legacy, strlen(legacy)), "write legacy managed sshCommand");
    CHECK(SetGlobalConfig("New", "new@example.com", ""), "migrate legacy sshCommand");
    result = ReadBytes(path, NULL);
    CHECK(!Contains(result, "IdentitiesOnly=yes"), "remove only legacy app-managed sshCommand");
    free(result);

    Config imported;
    memset(&imported, 0, sizeof(imported));
    AutoImportGlobalIdentity(&imported);
    CHECK(imported.account_count == 1, "first-run imports global Git identity");
    CHECK(strcmp(imported.accounts[0].name, "New") == 0 &&
          strcmp(imported.accounts[0].host_list[0], "github.com") == 0,
          "imported account has identity and default host");
}

static void TestValidationAndScanning(void) {
    CHECK(ValidateSSHHost("github.com"), "validate hostname");
    CHECK(ValidateSSHHost("git.example.com:65535"), "validate max port");
    CHECK(ValidateSSHHost("[2001:db8::1]:2222"), "validate bracketed IPv6");
    CHECK(!ValidateSSHHost("git.example.com:0"), "reject zero port");
    CHECK(!ValidateSSHHost("git.example.com:65536"), "reject oversized port");
    CHECK(!ValidateSSHHost("host:22:evil"), "reject malformed IPv6-like host");
    CHECK(!ValidateSSHHost("host:22\nHost evil"), "reject SSH config injection");

    char duplicate[2][HOST_LEN] = {{0}};
    strcpy(duplicate[0], "github.com:22");
    strcpy(duplicate[1], "github.com:2222");
    CHECK(!ValidateSSHHostList((const char (*)[HOST_LEN])duplicate, 2),
          "reject duplicate Host aliases");

    wchar_t ssh_dir[MAX_PATH];
    JoinPath(g_profile, L".ssh", ssh_dir, MAX_PATH);
    EnsureDirectory(ssh_dir);
    wchar_t valid[MAX_PATH], notes[MAX_PATH], public_key[MAX_PATH];
    JoinPath(ssh_dir, L"id_valid", valid, MAX_PATH);
    JoinPath(ssh_dir, L"id_notes.txt", notes, MAX_PATH);
    JoinPath(ssh_dir, L"id_valid.pub", public_key, MAX_PATH);
    WritePrivateKey(valid);
    CHECK(WriteBytes(notes, "not a key\n", 10), "write non-key fixture");
    CHECK(WriteBytes(public_key, "ssh-ed25519 AAAA test\n", 22), "write public key fixture");

    char valid_utf8[PATH_LEN];
    WideCharToMultiByte(CP_UTF8, 0, valid, -1, valid_utf8, PATH_LEN, NULL, NULL);
    CHECK(ValidateSSHPrivateKey(valid_utf8), "recognize private key");
    char notes_utf8[PATH_LEN];
    WideCharToMultiByte(CP_UTF8, 0, notes, -1, notes_utf8, PATH_LEN, NULL, NULL);
    CHECK(!ValidateSSHPrivateKey(notes_utf8), "reject ordinary file as private key");

    char keys[10][PATH_LEN];
    int count = GetSSHKeys(keys, 10);
    CHECK(count == 1, "SSH scan only returns actual private keys");
    CHECK(count == 1 && strcmp(keys[0], valid_utf8) == 0, "SSH scan returns full key path");
}

static void TestSSHConfigPreservation(void) {
    wchar_t ssh_dir[MAX_PATH], config_path[MAX_PATH], external_dir[MAX_PATH], key_path[MAX_PATH];
    JoinPath(g_profile, L".ssh", ssh_dir, MAX_PATH);
    JoinPath(ssh_dir, L"config", config_path, MAX_PATH);
    JoinPath(g_root, L"外部 keys", external_dir, MAX_PATH);
    JoinPath(external_dir, L"id work", key_path, MAX_PATH);
    EnsureDirectory(ssh_dir);
    EnsureDirectory(external_dir);
    WritePrivateKey(key_path);

    const char* original =
        "Host work\n    HostName work.internal\n\n"
        "# Git Account Manager - old.example\nHost old.example\n"
        "    HostName old.example\n    IdentityFile ~/.ssh/id_old\n\n"
        "Host personal\n    HostName personal.internal\n\n"
        "# >>> Git Account Manager >>>\nHost stale.example\n"
        "    IdentityFile \"C:/stale\"\n# <<< Git Account Manager <<<\n";
    CHECK(WriteBytes(config_path, original, strlen(original)), "write mixed SSH config");

    char key_utf8[PATH_LEN];
    WideCharToMultiByte(CP_UTF8, 0, key_path, -1, key_utf8, PATH_LEN, NULL, NULL);
    char hosts[2][HOST_LEN] = {{0}};
    strcpy(hosts[0], "github.com");
    strcpy(hosts[1], "git.example.com:2222");
    CHECK(SwitchAccountSSHConfig(key_utf8, (const char (*)[HOST_LEN])hosts, 2),
          "switch SSH config");
    char* updated = ReadBytes(config_path, NULL);
    CHECK(Contains(updated, "Host work\n"), "preserve custom SSH block before managed block");
    CHECK(Contains(updated, "Host personal\n"), "preserve custom SSH block after legacy block");
    CHECK(!Contains(updated, "old.example"), "remove legacy managed block");
    CHECK(!Contains(updated, "stale.example"), "remove current managed block");
    CHECK(Contains(updated, "Host git.example.com\n") && Contains(updated, "Port 2222\n"),
          "write validated host and port");
    CHECK(Contains(updated, "IdentityFile \"") && Contains(updated, "外部 keys/id work"),
          "write quoted absolute external key path");
    free(updated);

    wchar_t ssh_exe[MAX_PATH];
    if (SearchPathW(NULL, L"ssh.exe", NULL, MAX_PATH, ssh_exe, NULL)) {
        wchar_t resolved_path[MAX_PATH], command[MAX_PATH * 4];
        JoinPath(g_root, L"ssh-resolved.txt", resolved_path, MAX_PATH);
        (void)ssh_exe;
        _snwprintf(command, MAX_PATH * 4 - 1,
            L"ssh -G -F \"%ls\" git.example.com > \"%ls\" 2>nul",
            config_path, resolved_path);
        CHECK(_wsystem(command) == 0, "OpenSSH accepts generated config");
        char* resolved = ReadBytes(resolved_path, NULL);
        CHECK(Contains(resolved, "port 2222"), "ssh -G resolves configured port");
        CHECK(Contains(resolved, "identitiesonly yes"), "ssh -G resolves IdentitiesOnly");
        free(resolved);
    }

    char found_host[HOST_LEN];
    CHECK(GetHostFromSSHConfig(key_utf8, found_host, sizeof(found_host)),
          "find host for absolute key path");
    CHECK(strcmp(found_host, "github.com") == 0, "return first configured host");

    CHECK(ClearAllManagedSSHConfig(), "clear only managed SSH config");
    updated = ReadBytes(config_path, NULL);
    CHECK(Contains(updated, "Host work\n") && Contains(updated, "Host personal\n"),
          "custom SSH config survives clear");
    CHECK(!Contains(updated, "# >>> Git Account Manager >>>"), "managed marker removed by clear");
    free(updated);
}

static void TestAtomicApplyRollback(void) {
    wchar_t git_path[MAX_PATH], ssh_dir[MAX_PATH], ssh_path[MAX_PATH], key_path[MAX_PATH];
    JoinPath(g_profile, L".gitconfig", git_path, MAX_PATH);
    JoinPath(g_profile, L".ssh", ssh_dir, MAX_PATH);
    JoinPath(ssh_dir, L"config", ssh_path, MAX_PATH);
    JoinPath(ssh_dir, L"id_apply", key_path, MAX_PATH);
    EnsureDirectory(ssh_dir);
    WritePrivateKey(key_path);
    const char* git_before = "[user]\n\tname = Before\n\temail = before@example.com\n";
    const char* ssh_before = "Host keep\n    HostName keep.internal\n";
    WriteBytes(git_path, git_before, strlen(git_before));
    WriteBytes(ssh_path, ssh_before, strlen(ssh_before));

    char key_utf8[PATH_LEN];
    WideCharToMultiByte(CP_UTF8, 0, key_path, -1, key_utf8, PATH_LEN, NULL, NULL);
    char bad_hosts[1][HOST_LEN] = {{0}};
    strcpy(bad_hosts[0], "host:99999");
    CHECK(!ApplyAccountSettings("After", "after@example.com", key_utf8,
           (const char (*)[HOST_LEN])bad_hosts, 1), "invalid apply is rejected before writes");
    char* current = ReadBytes(git_path, NULL);
    CHECK(current && strcmp(current, git_before) == 0, "pre-validation leaves Git config unchanged");
    free(current);

    HANDLE lock = CreateFileW(ssh_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(lock != INVALID_HANDLE_VALUE, "lock SSH config for rollback test");
    char hosts[1][HOST_LEN] = {{0}};
    strcpy(hosts[0], "github.com");
    CHECK(!ApplyAccountSettings("After", "after@example.com", key_utf8,
           (const char (*)[HOST_LEN])hosts, 1), "SSH write failure is reported");
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    current = ReadBytes(git_path, NULL);
    CHECK(current && strcmp(current, git_before) == 0, "Git config rolls back after SSH failure");
    free(current);

    CHECK(ApplyAccountSettings("After", "after@example.com", key_utf8,
          (const char (*)[HOST_LEN])hosts, 1), "valid account settings apply atomically");
    char name[NAME_LEN], email[EMAIL_LEN];
    GetGlobalConfig(name, email);
    CHECK(strcmp(name, "After") == 0 && strcmp(email, "after@example.com") == 0,
          "valid apply updates Git identity");
}

static void TestKeyGeneration(void) {
    char generated[PATH_LEN];
    CHECK(!GenerateSSHKey("../escape", "test@example.com", "ed25519", generated),
          "reject key path traversal");
    CHECK(!GenerateSSHKey("config", "test@example.com", "ed25519", generated),
          "reject reserved SSH config name");
    CHECK(!GenerateSSHKey("bad&name", "test@example.com", "ed25519", generated),
          "reject shell metacharacters in key name");
    CHECK(!GenerateSSHKey("NUL.key", "test@example.com", "ed25519", generated),
          "reject reserved Windows device names");
    CHECK(GenerateSSHKey("id_generated_gam_test", "test@example.com", "ed25519", generated),
          "generate Ed25519 key without cmd shell");
    if (generated[0]) {
        CHECK(ValidateSSHPrivateKey(generated), "generated key validates as private key");
        wchar_t key_path[PATH_LEN];
        MultiByteToWideChar(CP_UTF8, 0, generated, -1, key_path, PATH_LEN);
        wchar_t pub_path[PATH_LEN + 5];
        _snwprintf(pub_path, PATH_LEN + 4, L"%ls.pub", key_path);
        DeleteFileW(key_path);
        DeleteFileW(pub_path);
    }
}

int main(void) {
    PrepareSandbox();
    TestConfigRoundTrip();
    TestGitConfig();
    TestValidationAndScanning();
    TestSSHConfigPreservation();
    TestAtomicApplyRollback();
    TestKeyGeneration();

    _wputenv_s(L"GAM_TEST_PROFILE_DIR", L"");
    _wputenv_s(L"GAM_TEST_CONFIG_DIR", L"");
    RemoveTree(g_root);
    if (g_failures) {
        fprintf(stderr, "%d of %d checks failed.\n", g_failures, g_checks);
        return 1;
    }
    printf("All %d isolated logic checks passed.\n", g_checks);
    return 0;
}
