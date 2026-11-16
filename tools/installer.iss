; EP-H205 安装包脚本（Inno Setup 6）
;
; 先跑 tools\package_client.bat 生成 dist\EPH205，再用 ISCC 编译本脚本。
; 也可以在 package_client.bat 里自动调用（检测到 ISCC 时会编译）。
;
; 手动编译：
;   ISCC.exe /DStageDir=<repo>\dist\EPH205 /DRepoRoot=<repo> ^
;     /DBuildVersion=0.1.0 /DBuildArch=x64 /DOutputDir=<repo>\dist ^
;     tools\installer.iss

#ifndef StageDir
  #error StageDir is required
#endif
#ifndef RepoRoot
  #error RepoRoot is required
#endif
#ifndef BuildVersion
  #define BuildVersion "0.1.0"
#endif
#ifndef BuildArch
  #define BuildArch "x64"
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif

#define AppName "EP-H205 综合测试系统"
#define AppExe "EPH205.exe"
#define AppPublisher "EP-H205"

[Setup]
; 升级时按 AppId 识别同一个产品，别改
AppId={{7F3A6C41-2D8B-4E52-9C1A-5B0E7D4A91C6}
AppName={#AppName}
AppVersion={#BuildVersion}
AppVerName={#AppName} {#BuildVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\EP-H205
DefaultGroupName=EP-H205
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=EP-H205-{#BuildVersion}-{#BuildArch}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
PrivilegesRequired=admin
UsePreviousAppDir=yes
; 与 main.cpp 创建的同名互斥体（会话内命名空间）：程序在跑时安装器会提示先关闭
AppMutex=EPH205_SingleInstance
UninstallDisplayIcon={app}\{#AppExe}
VersionInfoVersion={#BuildVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName}
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#BuildVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; \
  GroupDescription: "附加任务:"; Flags: checkedonce

[Files]
; 舞台目录由 package_client.bat 组装：exe、CEF 运行时、www 全在里面
Source: "{#StageDir}\*"; DestDir: "{app}"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
; 程序把数据与导出写在自己目录下；装到 Program Files 时，
; 这两个目录必须给普通用户写权限，否则保存记录/导出会失败
Name: "{app}\data"; Permissions: users-modify
Name: "{app}\exports"; Permissions: users-modify

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; \
  WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "启动 {#AppName}"; \
  Flags: nowait postinstall skipifsilent
