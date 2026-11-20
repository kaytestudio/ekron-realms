unit ekron_platform_mac;

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
  Result := 'macOS arm64';
  {$ELSE}
  {$IFDEF CPUX86_64}
  Result := 'macOS x86_64';
  {$ELSE}
  Result := 'macOS';
  {$ENDIF}
  {$ENDIF}
end;

function EkronDefaultEngineBinary(const ExeDir: AnsiString): AnsiString;
var
  Candidate: AnsiString;
begin
  Candidate := IncludeTrailingPathDelimiter(ExeDir) + 'EkronRealms';
  if FileExists(Candidate) then
    Exit(Candidate);
  Candidate := IncludeTrailingPathDelimiter(ExeDir) + 'ekron-realms';
  if FileExists(Candidate) then
    Exit(Candidate);
  Result := Candidate;
end;

function EkronDefaultAssetsDir(const ExeDir: AnsiString): AnsiString;
var
  Candidate: AnsiString;
begin
  Candidate := ExpandFileName(IncludeTrailingPathDelimiter(ExeDir) + '../Resources');
  if DirectoryExists(Candidate) then
    Exit(Candidate);
  Candidate := ExpandFileName(IncludeTrailingPathDelimiter(ExeDir) + 'sys/android/app/src/main/assets');
  if DirectoryExists(Candidate) then
    Exit(Candidate);
  Result := ExpandFileName(IncludeTrailingPathDelimiter(ExeDir) + '../sys/android/app/src/main/assets');
end;

end.
