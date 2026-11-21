{ ----------------------------------------------------------------------------
  Realms Engine - virtual filesystem (Pascal core)

  Quake 2 qcommon/files.c search path model: PAK archives ("PACK" header,
  64 byte directory entries, see game/qcommon/qfiles.pas) are mounted on top
  of the host filesystem. Lookups are case-insensitive, accept '\' or '/' and
  search the most recently mounted archive first, then fall back to a loose
  file provided by the C++ host (disk, Android assets, iOS bundle).
  ---------------------------------------------------------------------------- }
unit rlm_files;

{$mode objfpc}{$H+}

interface

const
  MAX_FILES_IN_PACK = 4096;
  PAK_HEADER_SIZE = 12;
  PAK_ENTRY_SIZE = 64;

{ Returns the number of entries mounted, or -1 if the archive is invalid. }
function FS_AddPak(const path: AnsiString): LongInt;
{ Loads a file into memory allocated with GetMem. Caller frees with FreeMem. }
function FS_LoadFile(const path: AnsiString; out data: Pointer; out size: LongInt): Boolean;
function FS_LoadText(const path: AnsiString; out text: AnsiString): Boolean;
function FS_PakCount: LongInt;
procedure FS_Shutdown;
function FS_NormalizePath(const path: AnsiString): AnsiString;

implementation

uses
  SysUtils, rlm_console;

type
  TPakEntry = record
    Name: AnsiString;  { normalised, lower case }
    FilePos: LongInt;
    FileLen: LongInt;
  end;

  PPack = ^TPack;
  TPack = record
    HostPath: AnsiString;
    Entries: array of TPakEntry;
    Resident: AnsiString; { whole archive when the host has no range reads }
    Next: PPack;          { linked newest first, like fs_searchpaths }
  end;

var
  Packs: PPack = nil;

function FS_NormalizePath(const path: AnsiString): AnsiString;
var
  i: Integer;
begin
  Result := LowerCase(path);
  for i := 1 to Length(Result) do
    if Result[i] = '\' then
      Result[i] := '/';
  while (Length(Result) > 0) and (Result[1] = '/') do
    Delete(Result, 1, 1);
end;

function SafeRelativePath(const path: AnsiString): Boolean;
begin
  { Same guard as Quake 2 FS_FOpenFile callers: no absolute or parent paths. }
  Result := (Length(path) > 0) and (path[1] <> '/') and (Pos('..', path) = 0) and
            (Pos(':', path) = 0);
end;

function LE32(const buf: AnsiString; offset: Integer): LongInt;
begin
  { Archives are little endian on every platform. offset is 1-based. }
  Result := LongInt(LongWord(Ord(buf[offset])) or
                    (LongWord(Ord(buf[offset + 1])) shl 8) or
                    (LongWord(Ord(buf[offset + 2])) shl 16) or
                    (LongWord(Ord(buf[offset + 3])) shl 24));
end;

function ReadBytes(const hostPath: AnsiString; offset: Int64; size: LongInt;
  out buf: AnsiString): Boolean;
var
  got: LongInt;
begin
  SetLength(buf, size);
  if size = 0 then
    Exit(True);
  got := HostReadRange(hostPath, offset, size, @buf[1]);
  Result := got = size;
end;

function FS_AddPak(const path: AnsiString): LongInt;
var
  header, dir, resident: AnsiString;
  dirofs, dirlen, count, i, j, base: LongInt;
  pack: PPack;
  entryName: AnsiString;
  total: Int64;
begin
  Result := -1;
  resident := '';
  if HostHasRangeReads then
  begin
    if not ReadBytes(path, 0, PAK_HEADER_SIZE, header) then
    begin
      Com_Warning('FS_AddPak: could not read ' + path);
      Exit;
    end;
  end
  else
  begin
    if not HostReadAll(path, resident) then
    begin
      Com_Warning('FS_AddPak: could not read ' + path);
      Exit;
    end;
    if Length(resident) < PAK_HEADER_SIZE then
    begin
      Com_Warning(path + ' is not a packfile');
      Exit;
    end;
    header := Copy(resident, 1, PAK_HEADER_SIZE);
  end;

  if Copy(header, 1, 4) <> 'PACK' then
  begin
    Com_Warning(path + ' is not a packfile');
    Exit;
  end;
  dirofs := LE32(header, 5);
  dirlen := LE32(header, 9);
  if (dirofs < PAK_HEADER_SIZE) or (dirlen < 0) or ((dirlen mod PAK_ENTRY_SIZE) <> 0) then
  begin
    Com_Warning(path + ' has a corrupt directory');
    Exit;
  end;
  count := dirlen div PAK_ENTRY_SIZE;
  if count > MAX_FILES_IN_PACK then
  begin
    Com_Warning(path + ' has ' + IntToStr(count) + ' files');
    Exit;
  end;

  if resident <> '' then
  begin
    if Int64(dirofs) + dirlen > Length(resident) then
    begin
      Com_Warning(path + ' has a truncated directory');
      Exit;
    end;
    dir := Copy(resident, dirofs + 1, dirlen);
  end
  else if not ReadBytes(path, dirofs, dirlen, dir) then
  begin
    Com_Warning(path + ' has a truncated directory');
    Exit;
  end;

  New(pack);
  pack^.HostPath := path;
  pack^.Resident := resident;
  SetLength(pack^.Entries, count);
  for i := 0 to count - 1 do
  begin
    base := i * PAK_ENTRY_SIZE + 1;
    entryName := '';
    for j := 0 to 55 do
    begin
      if dir[base + j] = #0 then
        Break;
      entryName := entryName + dir[base + j];
    end;
    pack^.Entries[i].Name := FS_NormalizePath(entryName);
    pack^.Entries[i].FilePos := LE32(dir, base + 56);
    pack^.Entries[i].FileLen := LE32(dir, base + 60);
    total := Int64(pack^.Entries[i].FilePos) + pack^.Entries[i].FileLen;
    if (pack^.Entries[i].FilePos < 0) or (pack^.Entries[i].FileLen < 0) or
       ((resident <> '') and (total > Length(resident))) then
    begin
      Com_Warning(path + ': entry ' + entryName + ' is out of range');
      Dispose(pack);
      Exit;
    end;
  end;

  pack^.Next := Packs;
  Packs := pack;
  Com_Printf('Added packfile ' + path + ' (' + IntToStr(count) + ' files)');
  Result := count;
end;

function FindEntry(pack: PPack; const name: AnsiString): Integer;
var
  i: Integer;
begin
  for i := High(pack^.Entries) downto 0 do
    if pack^.Entries[i].Name = name then
      Exit(i);
  Result := -1;
end;

function FS_LoadFile(const path: AnsiString; out data: Pointer; out size: LongInt): Boolean;
var
  name, blob: AnsiString;
  pack: PPack;
  idx: Integer;
  got: LongInt;
begin
  data := nil;
  size := 0;
  Result := False;
  name := FS_NormalizePath(path);
  if not SafeRelativePath(name) then
    Exit;

  pack := Packs;
  while pack <> nil do
  begin
    idx := FindEntry(pack, name);
    if idx >= 0 then
    begin
      size := pack^.Entries[idx].FileLen;
      data := GetMem(size + 1);
      PByte(data)[size] := 0; { convenient NUL for text consumers }
      if size > 0 then
      begin
        if pack^.Resident <> '' then
          Move(pack^.Resident[pack^.Entries[idx].FilePos + 1], data^, size)
        else
        begin
          got := HostReadRange(pack^.HostPath, pack^.Entries[idx].FilePos, size, data);
          if got <> size then
          begin
            FreeMem(data);
            data := nil;
            size := 0;
            Exit;
          end;
        end;
      end;
      Exit(True);
    end;
    pack := pack^.Next;
  end;

  { Loose host file (keep the caller's spelling for case-sensitive hosts). }
  if HostReadAll(StringReplace(path, '\', '/', [rfReplaceAll]), blob) then
  begin
    size := Length(blob);
    data := GetMem(size + 1);
    PByte(data)[size] := 0;
    if size > 0 then
      Move(blob[1], data^, size);
    Result := True;
  end;
end;

function FS_LoadText(const path: AnsiString; out text: AnsiString): Boolean;
var
  data: Pointer;
  size: LongInt;
begin
  text := '';
  Result := FS_LoadFile(path, data, size);
  if not Result then
    Exit;
  SetLength(text, size);
  if size > 0 then
    Move(data^, text[1], size);
  FreeMem(data);
end;

function FS_PakCount: LongInt;
var
  pack: PPack;
begin
  Result := 0;
  pack := Packs;
  while pack <> nil do
  begin
    Inc(Result);
    pack := pack^.Next;
  end;
end;

procedure FS_Shutdown;
var
  pack, n: PPack;
begin
  pack := Packs;
  while pack <> nil do
  begin
    n := pack^.Next;
    Dispose(pack);
    pack := n;
  end;
  Packs := nil;
end;

end.
