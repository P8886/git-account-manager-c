# Git Account Manager (C Version)

这是一个使用 C 语言 (Win32 API) 重写的轻量级 Git 账户管理工具。
旨在解决原 Go 版本打包体积过大 (20MB+) 的问题，同时保持完全的功能兼容性。

## ✨ 核心特性

*   **极致轻量**: 经过极致编译优化，单文件体积极小 (原 Go 版本约 24MB)。
*   **原生性能**: 基于 Win32 API 开发，启动瞬间完成，资源占用极低。
*   **独立配置**: 使用独立配置文件 (`%APPDATA%\git-account-manager-c\accounts.json`)，与 Go 版本分离。
*   **智能初始化**: 首次启动时自动检测并导入当前 Git 全局身份，无需手动添加。
*   **高分屏支持**: 自动适配 2K/4K 高 DPI 屏幕，字体和控件自动缩放。
*   **界面美化**: 
    *   原生 Windows 控件风格，布局整洁。
    *   **夜间模式**: 支持一键切换深色主题 (Sun/Moon 图标切换)，保护视力。
    *   **字体优化**: 全面适配 `Microsoft YaHei UI`，统一 18px 字号，解决中文显示问题。
    *   **细节打磨**: 统一控件高度，优化对齐，解决状态栏重影问题。
*   **SSH 管理**:
    *   **自动扫描**: 启动时自动扫描 `~/.ssh/` 目录下的私钥。
    *   **可视化选择**: 采用下拉组合框 (ComboBox) 展示 SSH Key，支持超长路径完整显示。
    *   **密钥生成**: 内置 SSH 密钥生成工具 (Ed25519/RSA)。
*   **多账号隔离**:
    *   使用 Host 别名机制，支持同一 Git 服务（如 github.com）配置多个账号。
    *   Host 别名格式：`<host>-<email>`（如 `github.com-user@example.com`）。
*   **中文支持**: 全中文界面及源代码注释，便于二次开发。

## ⚠️ 环境要求

*   **Git 版本**: 2.0+ （推荐 2.10+）
*   **OpenSSH 版本**: 6.0+
*   **操作系统**: Windows 7/8/10/11

> **关于 Host 别名中的 `@` 符号**: 
> SSH config 中 Host 别名支持使用 `@` 符号。经测试，Git 2.0+ 和 OpenSSH 6.0+ 均可正常解析。
> 如果您使用的是极老旧的 Git 版本（< 2.0），可能会出现解析问题，建议升级到最新版本。

## 📖 使用说明

### 多账号配置原理

本工具使用 **Host 别名** 机制实现同一 Git 服务的多账号支持。

例如，您有两个 GitHub 账号：
- 账号A：`userA@example.com`
- 账号B：`userB@example.com`

配置后，SSH config 文件会生成如下配置：

```ssh
# Git configuration for userA@example.com
Host github.com-userA@example.com
    HostName github.com
    User git
    IdentityFile ~/.ssh/id_userA
    IdentitiesOnly yes
    PreferredAuthentications publickey

# Git configuration for userB@example.com
Host github.com-userB@example.com
    HostName github.com
    User git
    IdentityFile ~/.ssh/id_userB
    IdentitiesOnly yes
    PreferredAuthentications publickey
```

### Clone 仓库时使用别名

当您需要使用特定账号 clone 仓库时，**需要使用 Host 别名**：

```bash
# 使用账号A clone
git clone git@github.com-userA@example.com:username/repo.git

# 使用账号B clone
git clone git@github.com-userB@example.com:username/repo.git
```

### 已有仓库切换账号

如果您已经 clone 了仓库，可以修改 `.git/config` 中的 remote URL：

```bash
# 查看当前 remote
git remote -v

# 修改 remote URL 使用特定账号
git remote set-url origin git@github.com-userA@example.com:username/repo.git
```

## 🚀 最近更新 (Changelog)

### v1.2.1 - 细节完善与编码修复
*   **编码修复**:
    *   **彻底解决乱码**: 强制 Git 命令输出 UTF-8 编码，彻底解决了状态栏中文名乱码和显示截断的问题 (修复了 `swprintf` 格式化错误)。
*   **UI 微调**:
    *   **高度统一**: 进一步微调了下拉框高度 (24px)，与输入框更加匹配。
    *   **图标修复**: 修复了太阳图标在切换后变黑的问题，并略微放大了图标尺寸以提升辨识度。
*   **构建优化**: 优化 `build.bat`，在编译前自动清理残留进程，避免文件占用错误。

### v1.2.0 - UI 细节打磨与编译优化
*   **布局优化**: 
    *   SSH Key 区域改为双行布局，解决长路径显示不全的问题。
    *   统一所有按钮、输入框和下拉框的视觉高度，界面更加整齐划一。
*   **视觉升级**: 
    *   主题切换按钮采用图形化图标 (☀️/🌙)，并优化了图标颜色。
    *   修复了白色主题下状态栏文字重叠的 Bug。
*   **编译优化**: 
    *   新增 GCC 极限体积优化参数 (`-Os -s -ffunction-sections` 等)，进一步缩减体积。
    *   集成应用图标 (`.ico`) 资源。
*   **代码维护**: 源代码注释全量中文化，新增 `ui_draw` 和 `ui_gen_key` 模块分离 UI 逻辑。

### v1.1.0 - UI 重构与体验优化
*   **界面重构**: 引入 GroupBox 分组布局，优化控件间距，视觉更清爽。
*   **交互升级**: SSH Key 输入框升级为下拉组合框 (ComboBox)，支持自动扫描和手动输入。
*   **体积优化**: 优化编译参数 (`-Os -s`) 和代码结构，体积进一步压缩至 ~180KB。
*   **细节修复**: 修复了部分字体渲染问题，优化了夜间模式的配色细节。

### v1.0.0 - 初始发布 (Initial Release)
*   **C 语言重写**: 完整复刻 Go 版本核心逻辑。
*   **功能实现**:
    *   多账户增删改查。
    *   一键切换全局 Git 用户名、邮箱和 SSH Key。
    *   解决 `system()` 调用导致的黑框问题 (使用 `CreateProcess`)。
    *   轻量级 JSON 解析器 (无第三方依赖)。

## 🛠️ 构建指南

项目自带自动编译脚本，支持 GCC (MinGW/TDM-GCC)。

### 1. 环境配置 (如果打包报错)
如果您运行 `build.bat` 时出现 `Error: Neither GCC nor MSVC found`，请按照以下步骤安装编译器：

*   **推荐方案 (TDM-GCC)**:
1.  访问 [TDM-GCC 官网](https://jmeubank.github.io/tdm-gcc/) 下载安装包。
2.  运行安装程序，点击 "Create"。
3.  选择 "MinGW-w64/TDM64" 版本。
4.  **重要**: 在安装选项中，务必勾选 **"Add to PATH"** (添加到系统环境变量)。
5.  安装完成后，**重启电脑**或重新打开命令行窗口。

### 2. 构建/打包步骤
1.  双击运行项目目录下的 `build.bat`。
2.  脚本会自动检测编译器 (GCC) 并开始编译。
3.  编译成功后，会在同级目录下生成 `GitAccountManager.exe`。
4.  这就是最终的可执行文件，您可以直接发送给其他人使用 (无需安装依赖)。

## 📂 目录结构

*   `main.c`: 主程序入口，负责窗口创建、消息循环及主界面布局。
*   `logic.c` / `logic.h`: 核心业务逻辑（Git 配置读写、JSON 解析、命令执行）。
*   `ui_draw.c` / `ui_draw.h`: 自定义 UI 绘制逻辑（自绘按钮、图标、配色方案）。
*   `ui_gen_key.c` / `ui_gen_key.h`: SSH 密钥生成对话框的 UI 与逻辑。
*   `shared.h`: 全局常量、宏定义及公共数据结构。
*   `resource.rc`: Windows 资源文件（应用图标等）。
*   `build.bat`: 自动构建脚本。
