program realms_launcher;

{$mode objfpc}{$H+}

uses
  SysUtils, Classes, Process,
  {$IFDEF DARWIN}
  ekron_platform_mac
  {$ELSE}
  ekron_platform_linux
  {$ENDIF};

const
  APP_VERSION = '1.0.0';

var
  ExeDir, EngineBin, AssetsDir, Arg: AnsiString;
  Proc: TProcess;
  Idx: Integer;
begin
  ExeDir := ExtractFilePath(ExpandFileName(ParamStr(0)));
  EngineBin := EkronDefaultEngineBinary(ExeDir);
  AssetsDir := EkronDefaultAssetsDir(ExeDir);

  for Idx := 1 to ParamCount do
  begin
    Arg := ParamStr(Idx);
    if (Arg = '--version') or (Arg = '-v') then
    begin
      WriteLn('Ekron Realms Pascal Launcher ', APP_VERSION, ' (', EkronPlatformName, ')');
      Halt(0);
    end
    else if (Arg = '--info') then
    begin
      WriteLn('Platform: ', EkronPlatformName);
      WriteLn('EngineBinary: ', EngineBin);
      WriteLn('AssetsDir: ', AssetsDir);
      Halt(0);
    end;
  end;

  if not FileExists(EngineBin) then
  begin
    WriteLn(StdErr, '[Ekron Launcher] Native sandbox binary not found at: ', EngineBin);
    Halt(1);
  end;

  Proc := TProcess.Create(nil);
  try
    Proc.Executable := EngineBin;
    Proc.Options := [poWaitOnExit];
    for Idx := 1 to ParamCount do
      Proc.Parameters.Add(ParamStr(Idx));
    Proc.Execute;
    Halt(Proc.ExitStatus);
  finally
    Proc.Free;
  end;
end.
