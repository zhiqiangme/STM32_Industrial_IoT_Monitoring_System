@echo off
setlocal
call "%~dp0Flutter_Env.bat"
pushd "%APP_DIR%" || goto fail

rem 连接手机调试运行，默认接入真实后端。
"%FLUTTER_BIN%" run %DART_DEFINES%
if errorlevel 1 goto fail

popd
exit /b 0

:fail
echo.
echo [ERROR] flutter run failed.
pause
popd >nul 2>nul
exit /b 1
