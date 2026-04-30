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

echo [WARN] This will uninstall the app and clear saved login data.
choice /C YN /M "Continue with clean reinstall"
if errorlevel 2 goto cancel

rem Clean reinstall is only for reset testing; it intentionally removes app data.
"%ADB_BIN%" uninstall com.varka.pipemonitor
"%ADB_BIN%" install "%RELEASE_APK%"
if errorlevel 1 goto fail

popd
exit /b 0

:cancel
echo Canceled.
popd
exit /b 0

:fail
echo.
echo [ERROR] clean reinstall failed.
pause
popd >nul 2>nul
exit /b 1
