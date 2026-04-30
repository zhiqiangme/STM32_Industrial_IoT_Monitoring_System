@echo off
setlocal
call "%~dp0Flutter_Env.bat"
pushd "%APP_DIR%" || goto fail

rem 静态检查 Dart/Flutter 代码；发布前建议运行一次。
"%FLUTTER_BIN%" analyze
if errorlevel 1 goto fail

popd
exit /b 0

:fail
echo.
echo [ERROR] flutter analyze failed.
pause
popd >nul 2>nul
exit /b 1
