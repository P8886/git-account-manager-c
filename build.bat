@echo off
gcc --version >nul 2>&1
if %errorlevel% neq 0 (
    echo GCC not found. Please install MinGW-w64 or TDM-GCC.
    echo Attempting to check for MSVC...
    cl >nul 2>&1
    if %errorlevel% neq 0 (
        echo MSVC not found either.
        pause
        exit /b 1
    ) else (
        echo Compiling with MSVC...
        cl main.c logic.c /Fe:GitManager.exe user32.lib kernel32.lib gdi32.lib comdlg32.lib shell32.lib /link /SUBSYSTEM:WINDOWS
    )
) else (
    echo Compiling with GCC...
    gcc main.c logic.c -o GitManager.exe -mwindows -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lshell32
)

if exist GitManager.exe (
    echo Build successful!
    echo Running...
    start GitManager.exe
) else (
    echo Build failed.
    pause
)
