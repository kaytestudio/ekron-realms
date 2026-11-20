unit ekron_platform_linux;

{$mode objfpc}{$H+}

interface

function EkronPlatformName: AnsiString;
function EkronDefaultEngineBinary(const ExeDir: AnsiString): AnsiString;
function EkronDefaultAssetsDir(const ExeDir: AnsiString): AnsiString;

implementation

uses
  SysUtils;

function EkronPlatformName: AnsiString;
begin
  {$IFDEF CPUAARCH64}
  Result := 'Linux arm64';
  {$ELSE}
  {$IFDEF CPUX86_64}
  Result := 'Linux x86_64';
  {$ELSE}
  Result := 'Linux';
  {$ENDIF}
  {$ENDIF}
end;

function EkronDefaultEngineBinary(const ExeDir: AnsiString): AnsiString;
begin
  Result := IncludeTrailingPathDelimiter(ExeDir) + 'ekron-realms';
  if not FileExists(Result) then
    Result := '/usr/bin/ekron-realms';
end;

function EkronDefaultAssetsDir(const ExeDir: AnsiString): AnsiString;
var
  Candidate: AnsiString;
begin
  Candidate := ExpandFileName(IncludeTrailingPathDelimiter(ExeDir) + '../share/ekron-realms');
  if DirectoryExists(Candidate) then
    Exit(Candidate);
  Candidate := ExpandFileName(IncludeTrailingPathDelimiter(ExeDir) + 'sys/android/app/src/main/assets');
  if DirectoryExists(Candidate) then
    Exit(Candidate);
  Candidate := ExpandFileName(IncludeTrailingPathDelimiter(ExeDir) + '../sys/android/app/src/main/assets');
  if DirectoryExists(Candidate) then
    Exit(Candidate);
  Result := '/usr/share/ekron-realms';
end;

end.
