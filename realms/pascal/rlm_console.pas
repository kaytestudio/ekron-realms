{ ----------------------------------------------------------------------------
  Realms Engine - console output and host services (Pascal core)

  Equivalent of Com_Printf / Com_DPrintf from Quake 2 qcommon/common.c,
  except that output is routed through the C++ host (stdout, Android logcat,
  iOS os_log, the in-game drop-down console, ...).
  ---------------------------------------------------------------------------- }
unit rlm_console;

{$mode objfpc}{$H+}

interface

uses
  ctypes, rlm_bridge;

var
  Host: TRealmsHostApi;
  HostBound: Boolean = False;

procedure BindHost(api: PRealmsHostApi);
procedure UnbindHost;

procedure Com_Printf(const text: AnsiString);
procedure Com_Warning(const text: AnsiString);
procedure Com_Error(const text: AnsiString);
procedure Com_DPrintf(const text: AnsiString);

{ Reads a whole host file into a Pascal string. Returns False if missing. }
function HostReadAll(const path: AnsiString; out data: AnsiString): Boolean;
{ Reads [offset, offset+size) from a host file. Returns bytes read or -1. }
function HostReadRange(const path: AnsiString; offset: Int64; size: LongInt;
  buffer: Pointer): LongInt;
function HostHasRangeReads: Boolean;
function HostWriteAll(const path, data: AnsiString): Boolean;
procedure HostCommand(const args: array of AnsiString);
function HostMilliseconds: Double;

implementation

procedure BindHost(api: PRealmsHostApi);
begin
  FillChar(Host, SizeOf(Host), 0);
  if api = nil then
  begin
    HostBound := False;
    Exit;
  end;
  { Copy only what the host actually provides (older/newer struct sizes). }
  if (api^.struct_size > 0) and (api^.struct_size < SizeOf(Host)) then
    Move(api^, Host, api^.struct_size)
  else
    Host := api^;
  HostBound := True;
end;

procedure UnbindHost;
begin
  FillChar(Host, SizeOf(Host), 0);
  HostBound := False;
end;

procedure Emit(level: cint32; const text: AnsiString);
begin
  if HostBound and Assigned(Host.print) then
    Host.print(Host.user, level, PChar(text));
end;

procedure Com_Printf(const text: AnsiString);
begin
  Emit(REALMS_PRINT_INFO, text);
end;

procedure Com_Warning(const text: AnsiString);
begin
  Emit(REALMS_PRINT_WARNING, text);
end;

procedure Com_Error(const text: AnsiString);
begin
  Emit(REALMS_PRINT_ERROR, text);
end;

procedure Com_DPrintf(const text: AnsiString);
begin
  Emit(REALMS_PRINT_DEVELOPER, text);
end;

function HostReadAll(const path: AnsiString; out data: AnsiString): Boolean;
var
  raw: Pointer;
  size: cint32;
begin
  data := '';
  Result := False;
  if not (HostBound and Assigned(Host.read_file)) then
    Exit;
  raw := nil;
  size := 0;
  if Host.read_file(Host.user, PChar(path), @raw, @size) <> 1 then
    Exit;
  if (raw <> nil) and (size > 0) then
  begin
    SetLength(data, size);
    Move(raw^, data[1], size);
  end;
  if (raw <> nil) and Assigned(Host.free_file) then
    Host.free_file(Host.user, raw);
  Result := True;
end;

function HostHasRangeReads: Boolean;
begin
  Result := HostBound and Assigned(Host.read_file_range);
end;

function HostReadRange(const path: AnsiString; offset: Int64; size: LongInt;
  buffer: Pointer): LongInt;
begin
  Result := -1;
  if HostHasRangeReads then
    Result := Host.read_file_range(Host.user, PChar(path), offset, size, buffer);
end;

function HostWriteAll(const path, data: AnsiString): Boolean;
begin
  Result := False;
  if HostBound and Assigned(Host.write_file) then
    Result := Host.write_file(Host.user, PChar(path), PChar(data), Length(data)) = 1;
end;

procedure HostCommand(const args: array of AnsiString);
var
  argv: array of PChar;
  i: Integer;
begin
  if not (HostBound and Assigned(Host.command)) or (Length(args) = 0) then
    Exit;
  SetLength(argv, Length(args) + 1);
  for i := 0 to High(args) do
    argv[i] := PChar(args[i]);
  argv[Length(args)] := nil;
  Host.command(Host.user, Length(args), @argv[0]);
end;

function HostMilliseconds: Double;
begin
  Result := 0;
  if HostBound and Assigned(Host.milliseconds) then
    Result := Host.milliseconds(Host.user);
end;

end.
