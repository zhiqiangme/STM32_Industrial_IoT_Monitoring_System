@echo off
setlocal
call "%~dp0Flutter_Env.bat"
pushd "%APP_DIR%" || goto fail

rem 拉取 Flutter 依赖；首次构建或 pubspec 变化后运行。
"%FLUTTER_BIN%" pub get
if errorlevel 1 goto fail

popd
exit /b 0

:fail
echo.
echo [ERROR] pub get failed.
pause
popd >nul 2>nul
exit /b 1
