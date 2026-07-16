@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

pushd "%~dp0" >nul || (
    echo 错误：无法进入项目目录。
    exit /b 1
)

where git.exe >nul 2>&1 || (
    echo 错误：未找到 git.exe。
    goto :fail
)

for /f "delims=" %%I in ('git branch --show-current') do set "BRANCH=%%I"
if /I not "%BRANCH%"=="main" (
    echo 错误：请在 main 分支发布，当前分支为 %BRANCH%。
    goto :fail
)

set "DIRTY="
for /f "delims=" %%I in ('git status --porcelain') do set "DIRTY=1"
if defined DIRTY (
    echo 错误：工作区存在未提交的改动，请先提交或清理。
    goto :fail
)

set /p "VERSION=请输入版本号（例如 1.1.1）："
if /I "!VERSION:~0,1!"=="v" set "VERSION=!VERSION:~1!"
set "RELEASE_VERSION=!VERSION!"

powershell -NoProfile -Command ^
    "if ($env:RELEASE_VERSION -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { exit 1 }"
if errorlevel 1 (
    echo 错误：版本号格式必须为 1.1.1。
    goto :fail
)

for /f "tokens=1-3 delims=." %%A in ("%VERSION%") do (
    set "VERSION_MAJOR=%%A"
    set "VERSION_MINOR=%%B"
    set "VERSION_PATCH=%%C"
)
set "TAG=v%VERSION%"

git show-ref --verify --quiet "refs/tags/%TAG%"
if not errorlevel 1 (
    echo 错误：本地标签 %TAG% 已存在。
    goto :fail
)

git ls-remote --exit-code --tags origin "refs/tags/%TAG%" >nul 2>&1
set "REMOTE_TAG_RESULT=%ERRORLEVEL%"
if "%REMOTE_TAG_RESULT%"=="0" (
    echo 错误：远端标签 %TAG% 已存在。
    goto :fail
)
if not "%REMOTE_TAG_RESULT%"=="2" (
    echo 错误：无法检查远端标签，请确认网络和 origin 配置。
    goto :fail
)

echo 正在运行发布前测试...
call test.bat
if errorlevel 1 (
    echo 错误：测试失败，已停止发布。
    goto :fail
)

powershell -NoProfile -Command "$utf8 = [Text.UTF8Encoding]::new($false); $headerPath = 'resource.h'; $header = [IO.File]::ReadAllText($headerPath); $header = [regex]::Replace($header, '(#define\s+APP_VERSION_MAJOR\s+)\d+', '${1}' + $env:VERSION_MAJOR); $header = [regex]::Replace($header, '(#define\s+APP_VERSION_MINOR\s+)\d+', '${1}' + $env:VERSION_MINOR); $header = [regex]::Replace($header, '(#define\s+APP_VERSION_PATCH\s+)\d+', '${1}' + $env:VERSION_PATCH); [IO.File]::WriteAllText($headerPath, $header, $utf8); $resourcePath = 'resource.rc'; $resource = [IO.File]::ReadAllText($resourcePath); $resource = [regex]::Replace($resource, '(VALUE\s+\x22FileVersion\x22,\s+\x22)\d+\.\d+\.\d+\.\d+', '${1}' + $env:RELEASE_VERSION + '.0'); $resource = [regex]::Replace($resource, '(VALUE\s+\x22ProductVersion\x22,\s+\x22)\d+\.\d+\.\d+\.\d+', '${1}' + $env:RELEASE_VERSION + '.0'); [IO.File]::WriteAllText($resourcePath, $resource, $utf8)"
if errorlevel 1 (
    echo 错误：更新程序版本失败。
    goto :fail
)

git add -- resource.h resource.rc
git diff --cached --quiet
if not errorlevel 1 (
    echo 错误：程序版本没有发生变化。
    goto :fail
)

git commit -m "chore: 将发布版本调整为 %VERSION%"
if errorlevel 1 (
    echo 错误：提交版本号失败。
    goto :fail
)

git push origin main
if errorlevel 1 (
    echo 错误：推送 main 分支失败，尚未创建标签。
    goto :fail
)

git tag "%TAG%"
if errorlevel 1 (
    echo 错误：创建标签 %TAG% 失败。
    goto :fail
)

git push origin "%TAG%"
if errorlevel 1 (
    echo 错误：推送标签 %TAG% 失败；本地标签已保留，可修复问题后手动推送。
    goto :fail
)

echo 发布标签 %TAG% 已推送，GitHub Actions 将自动创建 Release。
popd
exit /b 0

:fail
popd
exit /b 1
