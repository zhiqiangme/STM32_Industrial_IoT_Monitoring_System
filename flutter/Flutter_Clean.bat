@echo off
setlocal

pushd "%~dp0"
if errorlevel 1 (
    echo [ERROR] Failed to enter script directory: %~dp0
    goto fail
)

where flutter >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Flutter was not found in PATH.
    goto fail
)

if not exist "pubspec.yaml" (
    echo [ERROR] This folder is not a Flutter project: pubspec.yaml was not found.
    goto fail
)

findstr /C:"sdk: flutter" pubspec.yaml >nul 2>nul
if errorlevel 1 (
    echo [ERROR] This folder is not a Flutter project: pubspec.yaml does not contain "sdk: flutter".
    goto fail
)

set "LOOKS_LIKE_FLUTTER_APP="
if exist "android\" set "LOOKS_LIKE_FLUTTER_APP=1"
if exist "ios\" set "LOOKS_LIKE_FLUTTER_APP=1"
if exist "web\" set "LOOKS_LIKE_FLUTTER_APP=1"
if exist "windows\" set "LOOKS_LIKE_FLUTTER_APP=1"
if exist "linux\" set "LOOKS_LIKE_FLUTTER_APP=1"
if exist "macos\" set "LOOKS_LIKE_FLUTTER_APP=1"
if exist "lib\main.dart" set "LOOKS_LIKE_FLUTTER_APP=1"

if not defined LOOKS_LIKE_FLUTTER_APP (
    echo [ERROR] This folder does not look like a Flutter app project.
    echo [ERROR] Missing platform folders and lib\main.dart.
    goto fail
)

echo Cleaning Flutter build files in "%CD%"...
set "CLEAN_LOG=%TEMP%\flutter_clean_%RANDOM%_%RANDOM%.log"
flutter clean > "%CLEAN_LOG%" 2>&1
set "CLEAN_EXIT_CODE=%ERRORLEVEL%"
type "%CLEAN_LOG%"

if not "%CLEAN_EXIT_CODE%"=="0" (
    echo [ERROR] flutter clean failed.
    goto fail
)

findstr /I /C:"Failed to remove" /C:"A program may still be using" /C:"Cannot delete" /C:"Access is denied" /C:"Error:" /C:"Exception" "%CLEAN_LOG%" >nul 2>nul
if not errorlevel 1 (
    echo [ERROR] flutter clean reported an error.
    goto fail
)

echo Flutter clean completed.
del "%CLEAN_LOG%" >nul 2>nul
popd
exit /b 0

:fail
echo.
echo [ERROR] Flutter clean did not complete successfully.
echo This window will stay open so you can read the error above.
if defined CLEAN_LOG if exist "%CLEAN_LOG%" echo [ERROR] Log file: "%CLEAN_LOG%"
pause
popd >nul 2>nul
exit /b 1
