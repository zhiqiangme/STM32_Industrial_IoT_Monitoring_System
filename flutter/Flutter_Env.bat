@echo off
rem Flowmeter Flutter 脚本公共配置；由其他 Flutter_*.bat 通过 call 引入。

for %%I in ("%~dp0.") do set "APP_DIR=%%~fI"

set "FLUTTER_BIN=D:\Portable\Path\flutter\bin\flutter.bat"
if not exist "%FLUTTER_BIN%" (
    where flutter >nul 2>nul
    if not errorlevel 1 set "FLUTTER_BIN=flutter"
)

rem Android Gradle 需要 Java 11+；优先使用 Android Studio 自带 JBR，避免系统 Java 8 影响构建。
if exist "C:\Program Files\Android\Android Studio\jbr\bin\java.exe" (
    set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
    set "PATH=%JAVA_HOME%\bin;%PATH%"
)

set "API_BASE=https://api.varka.cn"
set "WS_URL=wss://api.varka.cn/ws/live"
set "DEVICE_ID=FM001"
set "DART_DEFINES=--dart-define=API_BASE=%API_BASE% --dart-define=WS_URL=%WS_URL% --dart-define=DEVICE_ID=%DEVICE_ID%"

set "RELEASE_APK=%APP_DIR%\build\app\outputs\flutter-apk\app-release.apk"
set "DEBUG_APK=%APP_DIR%\build\app\outputs\flutter-apk\app-debug.apk"
