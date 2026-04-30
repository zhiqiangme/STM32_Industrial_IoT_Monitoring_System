@echo off
setlocal
for %%I in ("%~dp0.") do set "APP_DIR=%%~fI"
pushd "%APP_DIR%" || goto fail

set "KEYSTORE_DIR=%APP_DIR%\android\release"
set "KEYSTORE_FILE=%KEYSTORE_DIR%\flowmeter-release.jks"
set "ANDROID_STUDIO_JBR=C:\Program Files\Android\Android Studio\jbr"

if not exist "%KEYSTORE_DIR%" mkdir "%KEYSTORE_DIR%"
if exist "%KEYSTORE_FILE%" (
    echo [INFO] Keystore already exists: "%KEYSTORE_FILE%"
    goto done
)

set "KEYTOOL_BIN=keytool"
if exist "%ANDROID_STUDIO_JBR%\bin\keytool.exe" set "KEYTOOL_BIN=%ANDROID_STUDIO_JBR%\bin\keytool.exe"
if defined JAVA_HOME if exist "%JAVA_HOME%\bin\keytool.exe" set "KEYTOOL_BIN=%JAVA_HOME%\bin\keytool.exe"

rem 本地生成 release keystore，真实密码只在 keytool 交互中输入。
"%KEYTOOL_BIN%" -genkeypair -v -keystore "%KEYSTORE_FILE%" -storetype JKS -keyalg RSA -keysize 2048 -validity 10000 -alias flowmeter-release
if errorlevel 1 goto fail
if not exist "%KEYSTORE_FILE%" goto fail

:done
echo.
echo [OK] Keystore: "%KEYSTORE_FILE%"
echo [NEXT] Copy android\key.properties.example to android\key.properties and fill passwords.
popd
exit /b 0

:fail
echo.
echo [ERROR] create release keystore failed.
pause
popd >nul 2>nul
exit /b 1
