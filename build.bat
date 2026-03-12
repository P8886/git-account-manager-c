@echo off
setlocal enabledelayedexpansion

:: 尝试关闭正在运行的进程
taskkill /F /IM GitAccountManager.exe >nul 2>&1

:: 检查 GCC
gcc --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Compiling with GCC...
    
    :: Compile main.c to avoid memory issues
    echo Compiling main.c...
    gcc -c main.c -o main.o -Os -Wall
    if %errorlevel% neq 0 (
        echo Failed to compile main.c
        pause
        exit /b 1
    )
    
    echo Compiling logic.c...
    gcc -c logic.c -o logic.o -Os -Wall
    if %errorlevel% neq 0 (
        echo Failed to compile logic.c
        pause
        exit /b 1
    )
    
    echo Compiling ui_draw.c...
    gcc -c ui_draw.c -o ui_draw.o -Os -Wall
    if %errorlevel% neq 0 (
        echo Failed to compile ui_draw.c
        pause
        exit /b 1
    )
    
    echo Compiling ui_gen_key.c...
    gcc -c ui_gen_key.c -o ui_gen_key.o -Os -Wall
    if %errorlevel% neq 0 (
        echo Failed to compile ui_gen_key.c
        pause
        exit /b 1
    )
    
    :: Compile resource file
    echo Compiling resources...
    windres resource.rc -o resource.o
    if %errorlevel% neq 0 (
        echo Failed to compile resources.
        pause
        exit /b 1
    )

    :: Link executable
    echo Linking executable...
    gcc main.o logic.o ui_draw.o ui_gen_key.o resource.o -o GitAccountManager.exe -mwindows -Os -s -Wl,--gc-sections -luser32 -lgdi32 -lcomdlg32 -lshell32 -ldwmapi
    
    :: Clean up temporary object files
    if exist main.o del main.o
    if exist logic.o del logic.o
    if exist ui_draw.o del ui_draw.o
    if exist ui_gen_key.o del ui_gen_key.o
    if exist resource.o del resource.o
    
    goto :success
)

:: Check for MSVC
cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Compiling with MSVC...
    
    :: Compile resources
    rc /fo resource.res resource.rc
    if %errorlevel% neq 0 (
        echo Failed to compile resources.
        pause
        exit /b 1
    )

    :: Compile main program
    cl main.c logic.c ui_draw.c ui_gen_key.c resource.res /Fe:GitAccountManager.exe /O1 /MD /link /SUBSYSTEM:WINDOWS user32.lib kernel32.lib gdi32.lib comdlg32.lib shell32.lib dwmapi.lib /OPT:REF /OPT:ICF
    
    :: Clean up resource res files
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
if exist GitAccountManager.exe (
    echo Build successful!
    echo GitAccountManager.exe has been created successfully.
    echo.
    pause
) else (
    echo Build failed.
    pause
)
