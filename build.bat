@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_DIR=%~dp0"
set "FALLBACK_BIN=D:\_App\_Code\TDM-GCC-64\bin"
set "OUTPUT=GitAccountManager.exe"
set "SIZE_LIMIT=5242880"
set "OBJECTS=main.o logic.o ui_draw.o ui_gen_key.o ui_taskbar.o ui_tray.o resource.o"
set "CFLAGS=-std=c11 -Os -flto -Wall -Wextra -ffunction-sections"
set "LDFLAGS=-mwindows -Os -flto -s -Wl,--gc-sections"
set "LIBS=-luser32 -lgdi32 -lcomdlg32 -lshell32 -ldwmapi"

pushd "%PROJECT_DIR%" >nul || (
    echo Error: cannot enter project directory.
    exit /b 1
)

call :find_tools
if errorlevel 1 goto :fail

call :clean
if errorlevel 1 goto :fail

echo Compiler: "%GCC%"
echo Resource compiler: "%WINDRES%"

call :compile main.c main.o
if errorlevel 1 goto :fail
call :compile logic.c logic.o
if errorlevel 1 goto :fail
call :compile ui_draw.c ui_draw.o
if errorlevel 1 goto :fail
call :compile ui_gen_key.c ui_gen_key.o
if errorlevel 1 goto :fail
call :compile ui_taskbar.c ui_taskbar.o
if errorlevel 1 goto :fail
call :compile ui_tray.c ui_tray.o
if errorlevel 1 goto :fail

echo Compiling resource.rc...
"%WINDRES%" resource.rc -O coff -o resource.o
if errorlevel 1 (
    echo Error: resource compilation failed.
    goto :fail
)

echo Linking %OUTPUT%...
"%GCC%" %OBJECTS% -o "%OUTPUT%" %LDFLAGS% %LIBS%
if errorlevel 1 (
    echo Error: linking failed.
    goto :fail
)

if not exist "%OUTPUT%" (
    echo Error: linker did not create %OUTPUT%.
    goto :fail
)

for %%I in ("%OUTPUT%") do set "OUTPUT_SIZE=%%~zI"
if !OUTPUT_SIZE! GTR %SIZE_LIMIT% (
    echo Error: %OUTPUT% is !OUTPUT_SIZE! bytes; limit is %SIZE_LIMIT% bytes.
    del /q "%OUTPUT%" >nul 2>&1
    goto :fail
)

call :clean_objects
echo Build successful: %OUTPUT% ^(!OUTPUT_SIZE! bytes^).
popd
exit /b 0

:find_tools
set "GCC="
set "WINDRES="

for %%I in (gcc.exe) do set "GCC=%%~$PATH:I"
for %%I in (windres.exe) do set "WINDRES=%%~$PATH:I"

if defined GCC if not defined WINDRES (
    for %%I in ("!GCC!") do set "GCC_BIN=%%~dpI"
    if exist "!GCC_BIN!windres.exe" set "WINDRES=!GCC_BIN!windres.exe"
)

if not defined GCC if exist "%FALLBACK_BIN%\gcc.exe" (
    set "GCC=%FALLBACK_BIN%\gcc.exe"
)
if not defined WINDRES if exist "%FALLBACK_BIN%\windres.exe" (
    set "WINDRES=%FALLBACK_BIN%\windres.exe"
)

if not defined GCC (
    echo Error: gcc.exe was not found on PATH or in %FALLBACK_BIN%.
    exit /b 1
)
if not defined WINDRES (
    echo Error: windres.exe was not found on PATH or in %FALLBACK_BIN%.
    exit /b 1
)
exit /b 0

:compile
echo Compiling %~1...
"%GCC%" %CFLAGS% -c "%~1" -o "%~2"
if errorlevel 1 (
    echo Error: failed to compile %~1.
    exit /b 1
)
exit /b 0

:clean
call :clean_objects
if exist "%OUTPUT%" del /q "%OUTPUT%" >nul 2>&1
if exist "%OUTPUT%" (
    echo Error: cannot remove the previous %OUTPUT%.
    exit /b 1
)
exit /b 0

:clean_objects
for %%I in (%OBJECTS%) do if exist "%%I" del /q "%%I" >nul 2>&1
exit /b 0

:fail
call :clean_objects
popd
exit /b 1
