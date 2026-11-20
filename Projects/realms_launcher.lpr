program realms_launcher;

{$mode objfpc}{$H+}

uses
  SysUtils, BaseUnix,
  {$IFDEF DARWIN}
  ekron_platform_mac
  {$ELSE}
  ekron_platform_linux
  {$ENDIF};

const
  APP_VERSION = '1.0.0';

var
  ExeDir, EngineBin, AssetsDir, Arg: AnsiString;
  ArgStrings: array of AnsiString;
  ArgPtrs: array of PChar;
  Idx, ArgCount: Integer;
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

  ArgCount := ParamCount + 1;
  SetLength(ArgStrings, ArgCount);
  SetLength(ArgPtrs, ArgCount + 1);

  ArgStrings[0] := EngineBin;
  ArgPtrs[0] := PChar(ArgStrings[0]);
  for Idx := 1 to ParamCount do
  begin
    ArgStrings[Idx] := ParamStr(Idx);
    ArgPtrs[Idx] := PChar(ArgStrings[Idx]);
  end;
  ArgPtrs[ArgCount] := nil;

  FpExecv(PChar(EngineBin), PPChar(@ArgPtrs[0]));
  WriteLn(StdErr, '[Ekron Launcher] FpExecv failed for: ', EngineBin, ' (errno=', fpgeterrno, ')');
  Halt(127);
end.
