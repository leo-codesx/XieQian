#define MyAppName "撷签"
#define MyAppFullName "撷签 XieQian"
#define MyAppVersion "1.2.5"
#define MyAppPublisher "XieQian"
#define MyAppExeName "XieQian.exe"

[Setup]
AppId={{16AD439B-0142-4C04-85FC-28C98CC55920}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppFullName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\XieQian
DefaultGroupName={#MyAppName}
DisableWelcomePage=yes
DisableDirPage=no
DisableProgramGroupPage=yes
DisableReadyPage=yes
OutputDir=installer-output
OutputBaseFilename=XieQian-Setup-{#MyAppVersion}
SetupIconFile=assets\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardImageBackColor=clWhite
WizardSmallImageBackColor=clWhite
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："

[Files]
Source: "deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "运行 {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  DataDirPage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  // 在“选择目标位置”页之后，新增“选择数据存储位置”页
  DataDirPage := CreateInputDirPage(wpSelectDir,
    '选择数据存储位置', '您希望将名单和历史记录保存在哪里？',
    '若在带有还原卡的机房使用，请务必选择 D 盘或 E 盘等非系统盘，否则重启后名单将丢失。',
    False, '');
  DataDirPage.Add('数据存储目录:');
  DataDirPage.Values[0] := ExpandConstant('{localappdata}\XieQian');

  // 纯白背景，其余保持 Inno Setup 默认的现代向导布局与按钮
  WizardForm.Color := clWhite;
  WizardForm.WelcomePage.Color := clWhite;
  WizardForm.InnerPage.Color := clWhite;
  WizardForm.SelectDirPage.Color := clWhite;
  WizardForm.SelectTasksPage.Color := clWhite;
  WizardForm.InstallingPage.Color := clWhite;
  WizardForm.FinishedPage.Color := clWhite;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    RegWriteStringValue(HKCU, 'Software\XieQian', 'DataPath', DataDirPage.Values[0]);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    // 卸载时清理注册表，但不删除用户的数据文件（保护数据）
    RegDeleteKeyIfEmpty(HKCU, 'Software\XieQian');
  end;
end;
