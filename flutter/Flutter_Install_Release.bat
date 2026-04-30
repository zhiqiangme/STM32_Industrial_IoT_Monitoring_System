@echo off
setlocal
call "%~dp0Flutter_Env.bat"
pushd "%APP_DIR%" || goto fail

set "ADB_BIN="
where adb >nul 2>nul
if not errorlevel 1 set "ADB_BIN=adb"
if not defined ADB_BIN if exist "%ANDROID_HOME%\platform-tools\adb.exe" set "ADB_BIN=%ANDROID_HOME%\platform-tools\adb.exe"
if not defined ADB_BIN if exist "%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe" set "ADB_BIN=%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe"
if not defined ADB_BIN (
    echo [ERROR] adb was not found. Install Android platform-tools or add adb to PATH.
    goto fail
)

if not exist "%RELEASE_APK%" (
    rem Build first when release APK is missing.
    call "%APP_DIR%\Flutter_Build_Release.bat"
    if errorlevel 1 goto fail
)

rem Replace install only. Do not uninstall first, or saved login will be removed.
"%ADB_BIN%" install -r -d "%RELEASE_APK%"
if errorlevel 1 goto fail

popd
exit /b 0

:fail
echo.
echo [ERROR] install release apk failed.
pause
popd >nul 2>nul
exit /b 1
