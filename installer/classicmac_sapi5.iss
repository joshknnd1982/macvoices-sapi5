; Classic Mac Voices SAPI 5 - Windows installer
;
; Built by tools\build_all.ps1, which passes the paths in as /D defines. To compile it by
; hand:
;
;   ISCC.exe /DStageDir=..\output /DVersion=1.0.0 installer\classicmac_sapi5.iss
;
; Accessibility notes, since this installer is meant to be usable by the people most likely
; to want these voices:
;   * Every page is a standard Inno Setup page built from real Win32 controls, which screen
;     readers read natively. Nothing is owner-drawn and there is no splash screen.
;   * Nothing steals focus, and no page auto-advances.
;   * Every outcome that matters - registration succeeded, registration failed, where the
;     logs are - is stated in text on the final page and written to the log, not signalled
;     by a colour or an icon.
;   * SetupLogging is on, so a failed install always leaves a full log behind.

#ifndef StageDir
  #define StageDir "..\output"
#endif
#ifndef Version
  #define Version "1.0.0"
#endif

#define AppName        "Classic Mac Voices SAPI 5"
#define AppPublisher   "Classic Mac Voices project"
#define EngineDllName  "ClassicMacSAPI5.dll"
#define ConfigExeName  "ClassicMacConfig.exe"
#define HostExeName    "panthera_host.exe"

[Setup]
#ifdef Probe
; A separate identity so an accessibility probe run can never be mistaken for, or clash
; with, a real installation.
AppId={{49177a8d-7a27-42be-802b-ab160a6c4f3c}
#else
AppId={{c055cc82-4f1e-4a0e-9767-042dbb9a696f}
#endif
AppName={#AppName}
AppVersion={#Version}
AppVerName={#AppName} {#Version}
AppPublisher={#AppPublisher}
AppComments=The 24 Mac OS X Leopard voices, including Alex, available to any SAPI 5 application.
UninstallDisplayName={#AppName}
DefaultDirName={autopf}\ClassicMacVoices
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#StageDir}\..\dist
#ifdef Probe
OutputBaseFilename=ClassicMacVoices_AccessibilityProbe
#else
OutputBaseFilename=ClassicMacVoices_Setup_{#Version}
#endif
; Alex's bank is AAC inside, which barely compresses; a fast setting keeps the build
; usable without costing meaningful size.
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern

; Both the CLSID registrations and the speech TokenEnums key live under HKLM.
; Compiling with /DProbe builds the same wizard without elevation and without the 700 MB
; of payload, which is how the pages are checked against a screen reader.
#ifdef Probe
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif

; Install into the 64-bit Program Files on a 64-bit Windows so that {sys} means the
; 64-bit System32 and {syswow64} means the 32-bit one, which is what the registration
; steps below depend on. On 32-bit Windows only the 32-bit half is installed.
ArchitecturesInstallIn64BitMode=x64compatible

; A full log is written to the temp folder and copied beside the program at the end.
SetupLogging=yes

InfoBeforeFile={#StageDir}\..\installer\before_install.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
#ifdef Probe
Source: "{#StageDir}\..\installer\before_install.txt"; DestDir: "{app}"
#else
; ---- the SAPI 5 interfaces -----------------------------------------------------------
; The 32-bit DLL serves 32-bit applications and the 64-bit DLL 64-bit ones; both drive
; the same 32-bit engine host over pipes, so there is exactly one engine to install.
Source: "{#StageDir}\{#EngineDllName}";      DestDir: "{app}";     Flags: ignoreversion
Source: "{#StageDir}\{#ConfigExeName}";      DestDir: "{app}";     Flags: ignoreversion
Source: "{#StageDir}\cmv_speak.exe";         DestDir: "{app}";     Flags: ignoreversion
Source: "{#StageDir}\cmv_sapitest.exe";      DestDir: "{app}";     Flags: ignoreversion
Source: "{#StageDir}\..\installer\open_logs.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\x64\{#EngineDllName}";  DestDir: "{app}\x64"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\cmv_speak.exe";     DestDir: "{app}\x64"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\cmv_sapitest.exe";  DestDir: "{app}\x64"; Flags: ignoreversion; Check: Is64BitInstallMode

; ---- the engine and every voice ------------------------------------------------------
; Everything the host needs, however large: MacinTalk, the SpeechDictionary framework,
; Apple's C++ runtime and all 24 voice bundles, Alex's 670 MB bank included.
Source: "{#StageDir}\engine\{#HostExeName}"; DestDir: "{app}\engine"; Flags: ignoreversion
Source: "{#StageDir}\engine\leopard\*";      DestDir: "{app}\engine\leopard"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
Name: "{group}\Classic Mac Voices settings"; Filename: "{app}\{#ConfigExeName}"; Comment: "Adjust voice, rate, pitch and reading behaviour"
Name: "{group}\Speak a test sentence"; Filename: "{app}\cmv_speak.exe"; Comment: "Speaks one sentence using a Classic Mac voice"
Name: "{group}\List installed voices"; Filename: "{app}\cmv_speak.exe"; Parameters: "--list"; Comment: "Lists every SAPI 5 voice Windows can see"
Name: "{group}\Open the log folder";   Filename: "{app}\open_logs.cmd"; Comment: "Opens the folder the speech engine writes its logs to"
Name: "{autodesktop}\Classic Mac Voices settings"; Filename: "{app}\{#ConfigExeName}"; Comment: "Adjust the Classic Mac voices"

[Run]
Filename: "{app}\cmv_speak.exe"; Description: "Speak a test sentence now"; Flags: postinstall nowait skipifsilent unchecked
Filename: "{app}\{#ConfigExeName}"; Description: "Open the settings utility"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\engine"
Type: files;          Name: "{app}\install.log"
Type: files;          Name: "{app}\open_logs.cmd"

[Code]
var
  RegisteredX86: Boolean;
  RegisteredX64: Boolean;
  RegistrationNotes: String;

procedure Note(const S: String);
begin
  Log('[classicmac] ' + S);
  if RegistrationNotes <> '' then
    RegistrationNotes := RegistrationNotes + #13#10;
  RegistrationNotes := RegistrationNotes + S;
end;

{ The engine host stays resident inside whichever program last spoke, and while it is
  alive its executable cannot be replaced. Stopping it is safe: the next utterance
  starts a fresh one. }
procedure StopHost;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM {#HostExeName} /F', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

{ regsvr32 is bitness-specific: the copy in System32 registers 64-bit DLLs, the copy in
  SysWOW64 registers 32-bit ones. Getting these the wrong way round is the classic way to
  end up with a voice that is registered but never enumerated. }
function RunRegsvr(const Regsvr32, DllPath, Args: String): Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec(Regsvr32, Args + ' "' + DllPath + '"', '', SW_HIDE,
                 ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  Log(Format('[classicmac] %s %s "%s" -> %d', [Regsvr32, Args, DllPath, ResultCode]));
end;

function VoicesRegistered: Boolean;
begin
  Result := RegKeyExists(HKEY_LOCAL_MACHINE,
    'SOFTWARE\Microsoft\Speech\Voices\TokenEnums\ClassicMacVoices');
  if not Result then
    Result := RegKeyExists(HKEY_LOCAL_MACHINE,
      'SOFTWARE\WOW6432Node\Microsoft\Speech\Voices\TokenEnums\ClassicMacVoices');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  LogPath: String;
begin
  if CurStep = ssInstall then
  begin
    StopHost;
  end
  else if CurStep = ssPostInstall then
  begin
    RegistrationNotes := '';

    if Is64BitInstallMode then
    begin
      RegisteredX64 := RunRegsvr(ExpandConstant('{sys}\regsvr32.exe'),
                                 ExpandConstant('{app}\x64\{#EngineDllName}'), '/s');
      RegisteredX86 := RunRegsvr(ExpandConstant('{syswow64}\regsvr32.exe'),
                                 ExpandConstant('{app}\{#EngineDllName}'), '/s');
      if RegisteredX64 then
        Note('The 64-bit SAPI 5 interface was registered.')
      else
        Note('The 64-bit SAPI 5 interface could NOT be registered.');
      if RegisteredX86 then
        Note('The 32-bit SAPI 5 interface was registered.')
      else
        Note('The 32-bit SAPI 5 interface could NOT be registered.');
    end
    else
    begin
      RegisteredX64 := True;  { nothing to do on 32-bit Windows }
      RegisteredX86 := RunRegsvr(ExpandConstant('{sys}\regsvr32.exe'),
                                 ExpandConstant('{app}\{#EngineDllName}'), '/s');
      if RegisteredX86 then
        Note('The 32-bit SAPI 5 interface was registered.')
      else
        Note('The 32-bit SAPI 5 interface could NOT be registered.');
    end;

    if VoicesRegistered then
      Note('Windows speech settings can now see the 24 Classic Mac voices.')
    else
      Note('Warning: the speech voice list was not updated. See the log named below.');

    { Keep the installer's own log with the program, where a bug report can find it. }
    LogPath := ExpandConstant('{log}');
    if LogPath <> '' then
    begin
      CopyFile(LogPath, ExpandConstant('{app}\install.log'), False);
      Note('A full installation log was saved as ' + ExpandConstant('{app}\install.log') + '.');
    end;
    { Written as an unexpanded environment variable on purpose: under an administrative
      install the localappdata constant resolves to the administrator's folder, not the
      folder the person who actually uses the voices will find their logs in. }
    Note('The speech engine writes its own logs to '
         + '%LOCALAPPDATA%\ClassicMacVoices\Logs'
         + ' - there is a shortcut to it in the Start menu.');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopHost;
    if IsWin64 then
    begin
      RunRegsvr(ExpandConstant('{sys}\regsvr32.exe'),
                ExpandConstant('{app}\x64\{#EngineDllName}'), '/s /u');
      RunRegsvr(ExpandConstant('{syswow64}\regsvr32.exe'),
                ExpandConstant('{app}\{#EngineDllName}'), '/s /u');
    end
    else
      RunRegsvr(ExpandConstant('{sys}\regsvr32.exe'),
                ExpandConstant('{app}\{#EngineDllName}'), '/s /u');
    { Give the loader a moment to let go of the DLLs before the files are deleted. }
    Sleep(1500);
  end;
end;

{ The finish page normally shows one fixed line. Replacing it with the notes above means a
  screen reader reads the actual outcome - which interfaces registered, where the logs are -
  instead of a generic success message. }
procedure CurPageChanged(CurPageID: Integer);
var
  Blank: String;
  Summary: String;
begin
  if (CurPageID = wpFinished) and (RegistrationNotes <> '') then
  begin
    Blank := #13#10 + #13#10;
    Summary := '{#AppName} has been installed.' + Blank + RegistrationNotes + Blank +
      'Choose a voice in your screen reader or in Windows speech settings, or tick a box ' +
      'below to hear one now or open the settings utility.';
    WizardForm.FinishedLabel.AutoSize := False;
    WizardForm.FinishedLabel.Height := WizardForm.FinishedLabel.Parent.ClientHeight - 8;
    WizardForm.FinishedLabel.Caption := Summary;
  end;
end;
