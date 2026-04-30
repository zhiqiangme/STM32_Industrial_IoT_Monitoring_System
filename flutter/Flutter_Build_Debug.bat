@echo off
setlocal
call "%~dp0Flutter_Env.bat"
pushd "%APP_DIR%" || goto fail

rem 构建调试 APK，便于快速安装到手机排查问题。
"%FLUTTER_BIN%" build apk --debug %DART_DEFINES%
if errorlevel 1 goto fail

echo.
echo [OK] Debug APK: "%DEBUG_APK%"
popd
exit /b 0

:fail
echo.
echo [ERROR] debug apk build failed.
pause
popd >nul 2>nul
exit /b 1
