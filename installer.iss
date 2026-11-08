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
WizardImageFile=
WizardSmallImageFile=assets\ui\logo-128.bmp
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
Source: "assets\ui\btn-next.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "assets\ui\btn-install.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "assets\ui\btn-back.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "assets\ui\btn-finish.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "assets\ui\btn-close.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "assets\ui\progress-track.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "assets\ui\progress-fill.bmp"; DestDir: "{tmp}"; Flags: dontcopy

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\卸载 {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Code]
const
  BrandFont = 'Microsoft YaHei UI';
  BrandColor = $00F68B3B;
  TextPrimaryColor = $00202020;
  TextSecondaryColor = $00606060;
  XQ_WM_NCHITTEST = $0084;
  XQ_HTCLIENT = 1;
  XQ_HTCAPTION = 2;
  XQ_GWL_WNDPROC = -4;
  XQ_DWMWA_WINDOW_CORNER_PREFERENCE = 33;
  XQ_DWMWCP_ROUND = 2;
  XQ_SW_SHOWNORMAL = 1;

function SetWindowRgn(hWnd: LongWord; hRgn: LongWord; bRedraw: Boolean): Integer;
  external 'SetWindowRgn@user32.dll stdcall';
function CreateRoundRectRgn(x1, y1, x2, y2, nWidthEllipse, nHeightEllipse: Integer): LongWord;
  external 'CreateRoundRectRgn@gdi32.dll stdcall';
function SetWindowLongW(hWnd: LongWord; nIndex: Integer; dwNewLong: LongWord): LongWord;
  external 'SetWindowLongW@user32.dll stdcall';
function CallWindowProcW(lpPrevWndFunc: LongWord; hWnd: LongWord; Msg: LongWord;
  wParam, lParam: LongWord): LongWord;
  external 'CallWindowProcW@user32.dll stdcall';
function DwmSetWindowAttribute(hwnd: LongWord; dwAttribute: LongWord;
  var pvAttribute: LongWord; cbAttribute: LongWord): LongInt;
  external 'DwmSetWindowAttribute@dwmapi.dll stdcall';

var
  DataDirPage: TInputDirWizardPage;
  CloseButton: TBitmapButton;
  BackButton: TBitmapButton;
  NextButton: TBitmapButton;
  FinishButton: TBitmapButton;
  ProgressTitle: TNewStaticText;
  ProgressHint: TNewStaticText;
  ProgressPercent: TNewStaticText;
  ProgressTrack: TBitmapImage;
  ProgressFill: TBitmapImage;
  FinishTitle: TNewStaticText;
  FinishSubtitle: TNewStaticText;
  PrevWizardWndProc: LongWord;
  CornerPreference: LongWord;
  NextIsInstall: Boolean;

procedure ApplyFont(Font: TFont; Size: Integer; Bold: Boolean; Color: TColor);
begin
  Font.Name := BrandFont;
  Font.Size := Size;
  if Bold then
    Font.Style := [fsBold]
  else
    Font.Style := [];
  Font.Color := Color;
end;

procedure ShiftTop(Control: TControl; Delta: Integer);
begin
  if Control <> nil then
    Control.Top := Control.Top + Delta;
end;

procedure LoadButtonBitmap(Button: TBitmapButton; const FileName: String);
begin
  ExtractTemporaryFile(FileName);
  Button.Bitmap.LoadFromFile(ExpandConstant('{tmp}\' + FileName));
end;

procedure LayoutHeader;
begin
  WizardForm.pageNameLabel.Top := ScaleY(78);
  WizardForm.pageDescriptionLabel.Top := ScaleY(104);
  WizardForm.WizardSmallBitmapImage.Visible := True;
  WizardForm.WizardSmallBitmapImage.Stretch := True;
  WizardForm.WizardSmallBitmapImage.Width := ScaleX(64);
  WizardForm.WizardSmallBitmapImage.Height := ScaleY(64);
  WizardForm.WizardSmallBitmapImage.Left := (WizardForm.ClientWidth - ScaleX(64)) div 2;
  WizardForm.WizardSmallBitmapImage.Top := ScaleY(10);
  CloseButton.Left := WizardForm.ClientWidth - ScaleX(32) - ScaleX(12);
  CloseButton.Top := ScaleY(8);
end;

procedure ApplyRoundedWindow;
var
  Region: LongWord;
begin
  Region := CreateRoundRectRgn(0, 0, WizardForm.Width + 1, WizardForm.Height + 1,
                               ScaleX(24), ScaleY(24));
  if Region <> 0 then
    SetWindowRgn(WizardForm.Handle, Region, True);
  CornerPreference := XQ_DWMWCP_ROUND;
  DwmSetWindowAttribute(WizardForm.Handle, XQ_DWMWA_WINDOW_CORNER_PREFERENCE,
                        CornerPreference, 4);
end;

function WizardWndProc(hWnd, Msg, wParam, lParam: LongWord): LongWord;
var
  X, Y: Integer;
  Hit: LongWord;
begin
  if Msg = XQ_WM_NCHITTEST then
  begin
    Hit := CallWindowProcW(PrevWizardWndProc, hWnd, Msg, wParam, lParam);
    if Hit = XQ_HTCLIENT then
    begin
      X := lParam and $FFFF;
      Y := (lParam shr 16) and $FFFF;
      if (Y >= WizardForm.Top) and (Y < WizardForm.Top + ScaleY(72)) and
         (X < WizardForm.Left + WizardForm.ClientWidth - ScaleX(48)) then
        Hit := XQ_HTCAPTION;
    end;
    Result := Hit;
  end
  else
    Result := CallWindowProcW(PrevWizardWndProc, hWnd, Msg, wParam, lParam);
end;

procedure OnNextClick(Sender: TObject);
begin
  WizardForm.NextButton.OnClick(nil);
end;

procedure OnBackClick(Sender: TObject);
begin
  WizardForm.BackButton.OnClick(nil);
end;

procedure OnCloseClick(Sender: TObject);
begin
  if WizardForm.CurPageID = wpFinished then
    WizardForm.Close
  else
    WizardForm.CancelButton.OnClick(nil);
end;

procedure OnFinishClick(Sender: TObject);
var
  ErrorCode: Integer;
begin
  ShellExec('', ExpandConstant('{app}\{#MyAppExeName}'), '', '',
            XQ_SW_SHOWNORMAL, ewNoWait, ErrorCode);
  WizardForm.Close;
end;

procedure SetNextButtonArt(InstallMode: Boolean);
begin
  if InstallMode <> NextIsInstall then
  begin
    if InstallMode then
      LoadButtonBitmap(NextButton, 'btn-install.bmp')
    else
      LoadButtonBitmap(NextButton, 'btn-next.bmp');
    NextIsInstall := InstallMode;
  end;
end;

procedure CreateModernControls;
begin
  CloseButton := TBitmapButton.Create(WizardForm);
  CloseButton.Parent := WizardForm;
  CloseButton.Width := ScaleX(32);
  CloseButton.Height := ScaleY(32);
  CloseButton.Stretch := True;
  CloseButton.Cursor := crHandPoint;
  CloseButton.Caption := '关闭';
  CloseButton.OnClick := @OnCloseClick;
  LoadButtonBitmap(CloseButton, 'btn-close.bmp');

  BackButton := TBitmapButton.Create(WizardForm);
  BackButton.Parent := WizardForm;
  BackButton.Width := ScaleX(120);
  BackButton.Height := ScaleY(36);
  BackButton.Stretch := True;
  BackButton.Cursor := crHandPoint;
  BackButton.Caption := '上一步';
  BackButton.OnClick := @OnBackClick;
  LoadButtonBitmap(BackButton, 'btn-back.bmp');

  NextButton := TBitmapButton.Create(WizardForm);
  NextButton.Parent := WizardForm;
  NextButton.Width := ScaleX(120);
  NextButton.Height := ScaleY(36);
  NextButton.Stretch := True;
  NextButton.Cursor := crHandPoint;
  NextButton.Caption := '下一步';
  NextButton.OnClick := @OnNextClick;
  LoadButtonBitmap(NextButton, 'btn-next.bmp');
  NextIsInstall := False;

  NextButton.Left := WizardForm.ClientWidth - ScaleX(120) - ScaleX(24);
  NextButton.Top := WizardForm.ClientHeight - ScaleY(36) - ScaleY(20);
  BackButton.Left := NextButton.Left - ScaleX(120) - ScaleX(12);
  BackButton.Top := NextButton.Top;

  FinishButton := TBitmapButton.Create(WizardForm.FinishedPage);
  FinishButton.Parent := WizardForm.FinishedPage;
  FinishButton.Width := ScaleX(220);
  FinishButton.Height := ScaleY(52);
  FinishButton.Stretch := True;
  FinishButton.Cursor := crHandPoint;
  FinishButton.Caption := '开始使用';
  FinishButton.OnClick := @OnFinishClick;
  LoadButtonBitmap(FinishButton, 'btn-finish.bmp');
  FinishButton.Left := (WizardForm.ClientWidth - FinishButton.Width) div 2;
  FinishButton.Top := ScaleY(232);

  FinishTitle := TNewStaticText.Create(WizardForm.FinishedPage);
  FinishTitle.Parent := WizardForm.FinishedPage;
  FinishTitle.AutoSize := False;
  FinishTitle.Width := ScaleX(440);
  FinishTitle.Height := ScaleY(34);
  FinishTitle.Left := (WizardForm.ClientWidth - FinishTitle.Width) div 2;
  FinishTitle.Top := ScaleY(112);
  FinishTitle.Alignment := taCenter;
  FinishTitle.Caption := '安装完成';
  ApplyFont(FinishTitle.Font, 24, True, TextPrimaryColor);

  FinishSubtitle := TNewStaticText.Create(WizardForm.FinishedPage);
  FinishSubtitle.Parent := WizardForm.FinishedPage;
  FinishSubtitle.AutoSize := False;
  FinishSubtitle.Width := ScaleX(440);
  FinishSubtitle.Height := ScaleY(24);
  FinishSubtitle.Left := (WizardForm.ClientWidth - FinishSubtitle.Width) div 2;
  FinishSubtitle.Top := ScaleY(156);
  FinishSubtitle.Alignment := taCenter;
  FinishSubtitle.Caption := '撷签已准备就绪，点击下方按钮开始使用。';
  ApplyFont(FinishSubtitle.Font, 10, False, TextSecondaryColor);

  ProgressTitle := TNewStaticText.Create(WizardForm.InstallingPage);
  ProgressTitle.Parent := WizardForm.InstallingPage;
  ProgressTitle.AutoSize := False;
  ProgressTitle.Width := ScaleX(440);
  ProgressTitle.Height := ScaleY(30);
  ProgressTitle.Left := (WizardForm.ClientWidth - ProgressTitle.Width) div 2;
  ProgressTitle.Top := ScaleY(132);
  ProgressTitle.Alignment := taCenter;
  ProgressTitle.Caption := '正在安装撷签…';
  ApplyFont(ProgressTitle.Font, 16, True, TextPrimaryColor);

  ProgressHint := TNewStaticText.Create(WizardForm.InstallingPage);
  ProgressHint.Parent := WizardForm.InstallingPage;
  ProgressHint.AutoSize := False;
  ProgressHint.Width := ScaleX(440);
  ProgressHint.Height := ScaleY(22);
  ProgressHint.Left := (WizardForm.ClientWidth - ProgressHint.Width) div 2;
  ProgressHint.Top := ScaleY(170);
  ProgressHint.Alignment := taCenter;
  ProgressHint.Caption := '正在写入文件，请稍候';
  ApplyFont(ProgressHint.Font, 9, False, TextSecondaryColor);

  ProgressPercent := TNewStaticText.Create(WizardForm.InstallingPage);
  ProgressPercent.Parent := WizardForm.InstallingPage;
  ProgressPercent.AutoSize := False;
  ProgressPercent.Width := ScaleX(440);
  ProgressPercent.Height := ScaleY(42);
  ProgressPercent.Left := (WizardForm.ClientWidth - ProgressPercent.Width) div 2;
  ProgressPercent.Top := ScaleY(210);
  ProgressPercent.Alignment := taCenter;
  ProgressPercent.Caption := '0%';
  ApplyFont(ProgressPercent.Font, 26, True, BrandColor);

  ProgressTrack := TBitmapImage.Create(WizardForm.InstallingPage);
  ProgressTrack.Parent := WizardForm.InstallingPage;
  ProgressTrack.Width := ScaleX(400);
  ProgressTrack.Height := ScaleY(8);
  ProgressTrack.Left := (WizardForm.ClientWidth - ProgressTrack.Width) div 2;
  ProgressTrack.Top := ScaleY(270);
  ProgressTrack.Stretch := True;
  ProgressTrack.BackColor := clWhite;
  ExtractTemporaryFile('progress-track.bmp');
  ProgressTrack.Bitmap.LoadFromFile(ExpandConstant('{tmp}\progress-track.bmp'));

  ProgressFill := TBitmapImage.Create(WizardForm.InstallingPage);
  ProgressFill.Parent := WizardForm.InstallingPage;
  ProgressFill.Width := 1;
  ProgressFill.Height := ScaleY(8);
  ProgressFill.Left := ProgressTrack.Left;
  ProgressFill.Top := ProgressTrack.Top;
  ProgressFill.Stretch := True;
  ProgressFill.BackColor := clWhite;
  ExtractTemporaryFile('progress-fill.bmp');
  ProgressFill.Bitmap.LoadFromFile(ExpandConstant('{tmp}\progress-fill.bmp'));
end;

procedure InitializeWizard;
var
  Shift: Integer;
begin
  // 无边框 + 大圆角 + 纯白沉浸式底
  WizardForm.BorderStyle := bsNone;
  WizardForm.Caption := '撷签 XieQian 安装向导';
  WizardForm.Color := clWhite;
  WizardForm.WelcomePage.Color := clWhite;
  WizardForm.InnerPage.Color := clWhite;
  WizardForm.SelectDirPage.Color := clWhite;
  WizardForm.SelectTasksPage.Color := clWhite;
  WizardForm.InstallingPage.Color := clWhite;
  WizardForm.FinishedPage.Color := clWhite;
  WizardForm.Font.Name := BrandFont;
  WizardForm.Font.Size := 9;

  WizardForm.MainPanel.Visible := False;
  WizardForm.Bevel.Visible := False;
  WizardForm.Bevel1.Visible := False;
  WizardForm.WizardBitmapImage.Visible := False;
  WizardForm.WizardBitmapImage2.Visible := False;
  WizardForm.WizardSmallBitmapImage.Visible := True;

  WizardForm.PageNameLabel.AutoSize := False;
  WizardForm.PageNameLabel.Height := ScaleY(26);
  ApplyFont(WizardForm.PageNameLabel.Font, 16, True, BrandColor);
  WizardForm.PageDescriptionLabel.AutoSize := False;
  WizardForm.PageDescriptionLabel.Height := ScaleY(22);
  ApplyFont(WizardForm.PageDescriptionLabel.Font, 9, False, TextSecondaryColor);

  ApplyFont(WizardForm.SelectDirLabel.Font, 10, False, TextPrimaryColor);
  ApplyFont(WizardForm.DirEdit.Font, 10, False, TextPrimaryColor);
  ApplyFont(WizardForm.SelectDirBrowseLabel.Font, 9, False, TextSecondaryColor);
  ApplyFont(WizardForm.SelectTasksLabel.Font, 10, False, TextPrimaryColor);
  ApplyFont(WizardForm.TasksList.Font, 10, False, TextPrimaryColor);

  // 在“选择目标位置”页之后，新增“选择数据存储位置”页
  DataDirPage := CreateInputDirPage(wpSelectDir,
    '选择数据存储位置', '您希望将名单和历史记录保存在哪里？',
    '若在带有还原卡的机房使用，请务必选择 D 盘或 E 盘等非系统盘，否则重启后名单将丢失。',
    False, '');
  DataDirPage.Add('数据存储目录:');
  DataDirPage.Values[0] := ExpandConstant('{localappdata}\XieQian');
  ApplyFont(DataDirPage.SubCaptionLabel.Font, 9, False, TextSecondaryColor);
  ApplyFont(DataDirPage.PromptLabels[0].Font, 9, False, TextPrimaryColor);
  ApplyFont(DataDirPage.Edits[0].Font, 10, False, TextPrimaryColor);

  // 为顶部 64px Logo 让出空间，页面内容统一下移，标题手动定位
  Shift := ScaleY(72);
  ShiftTop(WizardForm.SelectDirLabel, Shift);
  ShiftTop(WizardForm.DirEdit, Shift);
  ShiftTop(WizardForm.DirBrowseButton, Shift);
  ShiftTop(WizardForm.SelectDirBrowseLabel, Shift);
  ShiftTop(WizardForm.SelectTasksLabel, Shift);
  ShiftTop(WizardForm.TasksList, Shift);
  ShiftTop(DataDirPage.SubCaptionLabel, Shift);
  ShiftTop(DataDirPage.PromptLabels[0], Shift);
  ShiftTop(DataDirPage.Edits[0], Shift);
  ShiftTop(DataDirPage.Buttons[0], Shift);

  CreateModernControls;
  ApplyRoundedWindow;
  LayoutHeader;

  // 原生 Next/Back/Cancel 只保留逻辑，不参与绘制
  WizardForm.NextButton.Visible := False;
  WizardForm.BackButton.Visible := False;
  WizardForm.CancelButton.Visible := False;

  PrevWizardWndProc := SetWindowLongW(WizardForm.Handle, XQ_GWL_WNDPROC,
                                      CreateCallback(@WizardWndProc));
end;

procedure CurPageChanged(CurPageID: Integer);
var
  OnWizardPage: Boolean;
begin
  LayoutHeader;
  WizardForm.NextButton.Visible := False;
  WizardForm.BackButton.Visible := False;
  WizardForm.CancelButton.Visible := False;
  OnWizardPage := (CurPageID = wpSelectDir) or (CurPageID = DataDirPage.ID) or
                  (CurPageID = wpSelectTasks);

  NextButton.Visible := OnWizardPage;
  BackButton.Visible := (CurPageID = DataDirPage.ID) or (CurPageID = wpSelectTasks);
  FinishButton.Visible := CurPageID = wpFinished;

  if NextButton.Visible then
  begin
    SetNextButtonArt(CurPageID = wpSelectTasks);
    NextButton.Left := WizardForm.ClientWidth - ScaleX(120) - ScaleX(24);
    NextButton.Top := WizardForm.ClientHeight - ScaleY(36) - ScaleY(20);
    BackButton.Left := NextButton.Left - ScaleX(120) - ScaleX(12);
    BackButton.Top := NextButton.Top;
  end;

  if CurPageID = wpInstalling then
  begin
    WizardForm.StatusLabel.Visible := False;
    WizardForm.FilenameLabel.Visible := False;
    WizardForm.ProgressGauge.Visible := False;
    ProgressFill.Width := 1;
    ProgressPercent.Caption := '0%';
  end;

  if CurPageID = wpFinished then
  begin
    WizardForm.FinishedHeadingLabel.Visible := False;
    WizardForm.FinishedLabel.Visible := False;
    WizardForm.RunList.Visible := False;
    WizardForm.YesRadio.Visible := False;
    WizardForm.NoRadio.Visible := False;
    WizardForm.WizardBitmapImage2.Visible := False;
  end;
end;

procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
begin
  if MaxProgress > 0 then
  begin
    ProgressFill.Width := (ProgressTrack.Width * CurProgress) div MaxProgress;
    if ProgressFill.Width < 1 then
      ProgressFill.Width := 1;
    ProgressPercent.Caption := IntToStr((CurProgress * 100) div MaxProgress) + '%';
  end;
end;

procedure DeinitializeSetup;
begin
  if PrevWizardWndProc <> 0 then
    SetWindowLongW(WizardForm.Handle, XQ_GWL_WNDPROC, PrevWizardWndProc);
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
