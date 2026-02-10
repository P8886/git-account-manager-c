@echo off
setlocal enabledelayedexpansion

:: Check for GCC
gcc --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Compiling with GCC...
    
    :: Compile resource file
    windres resource.rc -o resource.o
    if %errorlevel% neq 0 (
        echo Failed to compile resources.
        pause
        exit /b 1
    )

    :: Compile main program
    gcc main.c logic.c resource.o -o GitManager.exe -mwindows -Os -s -ffunction-sections -fdata-sections -Wl,--gc-sections -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lshell32 -ldwmapi
    
    :: Clean up resource object file
    if exist resource.o del resource.o
    
    goto :success
)

:: Check for MSVC
cl >nul 2>&1
if %errorlevel% equ 0 (
    echo Compiling with MSVC...
    
    :: Compile resource file
    rc /fo resource.res resource.rc
    if %errorlevel% neq 0 (
        echo Failed to compile resources.
        pause
        exit /b 1
    )

    :: Compile main program
    cl main.c logic.c resource.res /Fe:GitManager.exe /O1 /MD /link /SUBSYSTEM:WINDOWS user32.lib kernel32.lib gdi32.lib comdlg32.lib shell32.lib dwmapi.lib /OPT:REF /OPT:ICF
    
    :: Clean up resource res file
    if exist resource.res del resource.res
    if exist main.obj del main.obj
    if exist logic.obj del logic.obj
    
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
