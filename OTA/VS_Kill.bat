@echo off
setlocal

set "ROOT=%~dp0"
set "FAILED=0"
set "HAS_SOLUTION=0"

for %%F in ("%ROOT%*.slnx" "%ROOT%*.sln") do (
    if exist "%%~F" set "HAS_SOLUTION=1"
)

if not "%HAS_SOLUTION%"=="1" (
    echo No .slnx or .sln file was found under:
    echo %ROOT%
    echo.
    echo Cleanup was skipped to avoid deleting files from the wrong directory.
    pause
    exit /b 1
)

echo Cleaning Visual Studio and MSBuild generated files under:
echo %ROOT%
echo.

call :RemoveDir "%ROOT%.vs"
call :RemoveDir "%ROOT%.dotnet"

for /d /r "%ROOT%" %%D in (bin obj TestResults) do (
    if exist "%%D" call :RemoveDir "%%D"
)

if "%FAILED%"=="0" (
    exit /b 0
)

echo.
echo Cleanup failed. Check whether Visual Studio, dotnet, or another process is using these files.
pause
exit /b 1

:RemoveDir
if not exist "%~1" exit /b 0

echo Removing "%~1"
rmdir /s /q "%~1"
if errorlevel 1 (
    echo Failed to remove "%~1"
    set "FAILED=1"
)
exit /b 0
