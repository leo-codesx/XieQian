#define MyAppName "撷签"
#define MyAppFullName "撷签 XieQian"
#define MyAppVersion "1.2.1"
#define MyAppPublisher "XieQian"
#define MyAppExeName "XieQian.exe"

[Setup]
AppId={{16AD439B-0142-4C04-85FC-28C98CC55920}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppFullName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
SetupWindowTitle={#MyAppFullName} 安装向导
DefaultDirName={autopf}\XieQian
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=installer-output
OutputBaseFilename=XieQian-Setup-{#MyAppVersion}
SetupIconFile=assets\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardImageFile=assets\wizard-large.bmp,assets\wizard-large-2x.bmp
WizardSmallImageFile=assets\wizard-small.bmp,assets\wizard-small-2x.bmp
WizardImageBackColor=$00F7F2F2
WizardSmallImageBackColor=$00F7F2F2
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
const
  BrandFont = 'Microsoft YaHei UI';
  BrandColor = $00F68B3B;
  TextPrimaryColor = $00202020;
  TextSecondaryColor = $00606060;

var
  DataDirPage: TInputDirWizardPage;

procedure ApplyWizardFonts;
begin
  WizardForm.Font.Name := BrandFont;
  WizardForm.Font.Size := 9;

  WizardForm.WelcomeLabel1.Font.Name := BrandFont;
  WizardForm.WelcomeLabel1.Font.Size := 22;
  WizardForm.WelcomeLabel1.Font.Style := [fsBold];
  WizardForm.WelcomeLabel1.Font.Color := TextPrimaryColor;

  WizardForm.WelcomeLabel2.Font.Name := BrandFont;
  WizardForm.WelcomeLabel2.Font.Size := 11;
  WizardForm.WelcomeLabel2.Font.Color := TextSecondaryColor;

  WizardForm.PageNameLabel.Font.Name := BrandFont;
  WizardForm.PageNameLabel.Font.Size := 15;
  WizardForm.PageNameLabel.Font.Style := [fsBold];
  WizardForm.PageNameLabel.Font.Color := BrandColor;

  WizardForm.PageDescriptionLabel.Font.Name := BrandFont;
  WizardForm.PageDescriptionLabel.Font.Size := 9;
  WizardForm.PageDescriptionLabel.Font.Color := TextSecondaryColor;

  WizardForm.FinishedHeadingLabel.Font.Name := BrandFont;
  WizardForm.FinishedHeadingLabel.Font.Size := 18;
  WizardForm.FinishedHeadingLabel.Font.Style := [fsBold];
  WizardForm.FinishedHeadingLabel.Font.Color := TextPrimaryColor;

  WizardForm.FinishedLabel.Font.Name := BrandFont;
  WizardForm.FinishedLabel.Font.Size := 9;
  WizardForm.FinishedLabel.Font.Color := TextSecondaryColor;

  WizardForm.NextButton.Font.Name := BrandFont;
  WizardForm.NextButton.Font.Style := [fsBold];
  WizardForm.BackButton.Font.Name := BrandFont;
  WizardForm.CancelButton.Font.Name := BrandFont;
  WizardForm.HelpButton.Font.Name := BrandFont;
end;

procedure InitializeWizard;
begin
  WizardForm.Color := clWhite;
  WizardForm.WelcomePage.Color := clWhite;
  WizardForm.InnerPage.Color := clWhite;
  WizardForm.FinishedPage.Color := clWhite;

  ApplyWizardFonts;
  WizardForm.WelcomeLabel2.Caption := '妙择一签，落定心安';

  // 在“选择目标位置”页之后，新增“选择数据目录”页
  DataDirPage := CreateInputDirPage(wpSelectDir,
    '选择数据存储位置', '您希望将名单和历史记录保存在哪里？',
    '如果您在学校机房使用（带有还原卡），请务必选择非系统盘（如 D:\ 或 E:\），否则重启后数据会丢失。',
    False, '');
  DataDirPage.Add('数据存储目录:');
  // 默认路径设为目标安装目录下的 Data 文件夹，或者默认的 AppData
  DataDirPage.Values[0] := ExpandConstant('{localappdata}\XieQian');

  DataDirPage.SubCaptionLabel.Font.Name := BrandFont;
  DataDirPage.SubCaptionLabel.Font.Size := 9;
  DataDirPage.SubCaptionLabel.Font.Color := TextSecondaryColor;
  DataDirPage.PromptLabels[0].Font.Name := BrandFont;
  DataDirPage.PromptLabels[0].Font.Size := 9;
  DataDirPage.PromptLabels[0].Font.Color := TextPrimaryColor;
  DataDirPage.Edits[0].Font.Name := BrandFont;
  DataDirPage.Edits[0].Font.Size := 10;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // 安装完成后，将用户选择的路径写入当前用户的注册表
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
