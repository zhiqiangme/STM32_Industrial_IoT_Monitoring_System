@echo off
setlocal
call "%~dp0Flutter_Env.bat"
pushd "%APP_DIR%" || goto fail

if not exist "%APP_DIR%\android\key.properties" (
    echo [ERROR] Missing "%APP_DIR%\android\key.properties".
    echo Run Flutter_Create_Release_Keystore.bat first, then copy android\key.properties.example to android\key.properties and fill passwords.
    goto fail
)

rem 构建正式签名 APK；签名配置来自 android\key.properties。
"%FLUTTER_BIN%" build apk --release %DART_DEFINES%
if errorlevel 1 goto fail

echo.
echo [OK] Release APK: "%RELEASE_APK%"
popd
exit /b 0

:fail
echo.
echo [ERROR] release apk build failed.
pause
popd >nul 2>nul
exit /b 1
