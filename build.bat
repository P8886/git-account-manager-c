@echo off
setlocal enabledelayedexpansion

:: 检查 GCC
gcc --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Compiling with GCC...
    
    :: 编译资源文件
    windres resource.rc -o resource.o
    if %errorlevel% neq 0 (
        echo Failed to compile resources.
        pause
        exit /b 1
    )

    :: 编译主程序
    gcc main.c logic.c ui_draw.c ui_gen_key.c resource.o -o GitManager.exe -mwindows -Os -s -ffunction-sections -fdata-sections -fno-ident -fno-asynchronous-unwind-tables -Wl,--gc-sections -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lshell32 -ldwmapi
    
    :: 清理资源对象文件
    if exist resource.o del resource.o
    
    goto :success
)

:: 检查 MSVC
cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Compiling with MSVC...
    
    :: 编译资源文件
    rc /fo resource.res resource.rc
    if %errorlevel% neq 0 (
        echo Failed to compile resources.
        pause
        exit /b 1
    )

    :: 编译主程序
    cl main.c logic.c ui_draw.c ui_gen_key.c resource.res /Fe:GitManager.exe /O1 /MD /link /SUBSYSTEM:WINDOWS user32.lib kernel32.lib gdi32.lib comdlg32.lib shell32.lib dwmapi.lib /OPT:REF /OPT:ICF
    
    :: 清理资源 res 文件
    if exist resource.res del resource.res
    if exist main.obj del main.obj
    if exist logic.obj del logic.obj
    if exist ui_draw.obj del ui_draw.obj
    if exist ui_gen_key.obj del ui_gen_key.obj
    
    goto :success
)

echo Error: Neither GCC nor MSVC found. Please install a compiler.
pause
exit /b 1

:success
if exist GitManager.exe (
    echo Build successful!
    echo Running...
    start GitManager.exe
) else (
    echo Build failed.
    pause
)
