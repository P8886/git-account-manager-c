@echo off
setlocal EnableExtensions

set "PROJECT_DIR=%~dp0"
set "FALLBACK_BIN=D:\_App\_Code\TDM-GCC-64\bin"
set "TEST_EXE=%TEMP%\git-account-manager-tests-%RANDOM%-%RANDOM%.exe"

for %%I in (gcc.exe) do set "GCC=%%~$PATH:I"
if not defined GCC if exist "%FALLBACK_BIN%\gcc.exe" set "GCC=%FALLBACK_BIN%\gcc.exe"
if not defined GCC (
    echo Error: gcc.exe was not found.
    exit /b 1
)

pushd "%PROJECT_DIR%" >nul || exit /b 1
"%GCC%" -DGAM_TESTING -std=c11 -O1 -Wall -Wextra -I. tests\test_logic.c logic.c ^
    -o "%TEST_EXE%" -lshell32
if errorlevel 1 (
    popd
    exit /b 1
)

"%TEST_EXE%"
set "RESULT=%ERRORLEVEL%"
del /q "%TEST_EXE%" >nul 2>&1
popd
exit /b %RESULT%

