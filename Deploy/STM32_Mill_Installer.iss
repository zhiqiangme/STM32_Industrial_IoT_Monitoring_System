; ======================================================================
; STM32 Mill 控制系统 全家桶 Inno Setup 安装脚本 v2.0.0
; ======================================================================

; -----------------------------
; 应用基本信息与常量定义
; -----------------------------
#define MyAppName            "STM32 Mill 控制中心"
#define MyAppVersion         "2.0.0"
#define MyAppPublisher       "zhiqiangme"
#define MyAppURL             "https://github.com/zhiqiangme"
#define MyAppId              "{{4535FFBB-EB5D-4D28-BF6A-BC7796C8C8B8}}"

; 核心执行文件名
#define MainAppExeName       "MillStudio.exe"
#define OtaAppExeName        "OTA.exe"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; 🌟 权限与安装路径：因为需要静默安装驱动和 .NET 环境，必须提权到管理员级别
PrivilegesRequired=admin
DefaultDirName={autopf}\STM32_Mill
DefaultGroupName={#MyAppName}

; 🌟 外观与资源 (采用纯相对路径)
SetupIconFile=Assets\Setup.ico
UninstallDisplayIcon={app}\{#MainAppExeName}
LicenseFile=Assets\License.txt

; 🌟 输出配置 (直接输出到 Output 文件夹，文件名自带版本号)
OutputDir=Output
OutputBaseFilename=STM32_Mill_Setup_v{#MyAppVersion}

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes

WizardStyle=modern
SolidCompression=yes
Compression=lzma2

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; 1. 搬运主上位机程序 (输出到安装根目录)
Source: "Artifacts\Windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; 2. 搬运 OTA 升级工具 (输出到 OTA_Tool 子目录)
Source: "Artifacts\OTA\*"; DestDir: "{app}\OTA_Tool"; Flags: ignoreversion recursesubdirs createallsubdirs

; 3. 环境驱动搬运 (发到用户的系统临时目录，安装完自动删除)
Source: "Drivers\dotnet-sdk-10.0.201-win-x64.exe"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall
Source: "Drivers\CH341SER.exe"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall

[Icons]
; 主程序快捷方式 (开始菜单 + 桌面)
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MainAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MainAppExeName}"; Tasks: desktopicon

; OTA 工具快捷方式 (开始菜单 + 桌面)
Name: "{autoprograms}\STM32 OTA 升级工具"; Filename: "{app}\OTA_Tool\{#OtaAppExeName}"
Name: "{autodesktop}\STM32 OTA 升级工具"; Filename: "{app}\OTA_Tool\{#OtaAppExeName}"; Tasks: desktopicon

[Run]
; 1. 静默安装 .NET 10 SDK 环境 (使用微软标准的 /quiet /norestart 参数)
Filename: "{tmp}\dotnet-sdk-10.0.201-win-x64.exe"; Parameters: "/quiet /norestart"; Description: "正在安装 .NET 10 运行环境..."; Flags: waituntilterminated runascurrentuser skipifdoesntexist

; 2. 静默安装 CH340 串口驱动 (使用常规的 /S 静默参数)
Filename: "{tmp}\CH341SER.exe"; Parameters: "/S"; Description: "正在安装 CH340 串口驱动..."; Flags: waituntilterminated runascurrentuser skipifdoesntexist

; 3. 运行主程序
Filename: "{app}\{#MainAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent