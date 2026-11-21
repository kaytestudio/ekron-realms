{ ----------------------------------------------------------------------------
  Realms Engine - console variables (Pascal core)

  Port of Quake 2 qcommon/cvar.c (via the Quake2 Delphi game/qcommon/CVar.pas
  conversion) made self-contained so it can be compiled as a shared/static
  library and driven through the Realms bridge ABI.

  Semantics follow Quake 2:
    - Cvar_Get creates a variable or ORs new flags into an existing one.
    - NOSET variables are write protected outside of Cvar_ForceSet.
    - LATCH variables store the request and apply it on the next restart.
    - USERINFO/SERVERINFO values may not contain \ " or ;
  Realms additions: CHEAT flag gated by sv_cheats and a remembered default.
  ---------------------------------------------------------------------------- }
unit rlm_cvar;

{$mode objfpc}{$H+}

interface

uses
  rlm_bridge;

type
  PCvar = ^TCvar;
  TCvar = record
    Name: AnsiString;
    Value: AnsiString;
    DefaultValue: AnsiString;
    LatchedValue: AnsiString;
    HasLatched: Boolean;
    Flags: LongInt;
    Modified: Boolean;
    NumValue: Single;
    Next: PCvar;
  end;

var
  CvarVars: PCvar = nil;
  { Mirrors Quake 2's global userinfo_modified. }
  UserinfoModified: Boolean = False;

function Cvar_Find(const name: AnsiString): PCvar;
function Cvar_Get(const name, defaultValue: AnsiString; flags: LongInt): PCvar;
function Cvar_Set(const name, value: AnsiString): LongInt;
function Cvar_ForceSet(const name, value: AnsiString): LongInt;
function Cvar_FullSet(const name, value: AnsiString; flags: LongInt): PCvar;
function Cvar_VariableValue(const name: AnsiString): Single;
function Cvar_VariableString(const name: AnsiString): AnsiString;
procedure Cvar_GetLatchedVars;
procedure Cvar_ClearAll;
function Cvar_Count: LongInt;
function Cvar_ArchiveText: AnsiString;

{ atof() compatible: parses the longest numeric prefix, 0 if none. }
function RealmsAtof(const s: AnsiString): Single;
function SameNameCI(const a, b: AnsiString): Boolean;

implementation

uses
  SysUtils, rlm_console;

function SameNameCI(const a, b: AnsiString): Boolean;
begin
  Result := (Length(a) = Length(b)) and (LowerCase(a) = LowerCase(b));
end;

function RealmsAtof(const s: AnsiString): Single;
var
  i, n, startDigits: Integer;
  sign, value, scale, expSign: Double;
  expo: Integer;
  sawDigit: Boolean;
begin
  Result := 0;
  n := Length(s);
  i := 1;
  while (i <= n) and (s[i] in [' ', #9, #10, #13]) do
    Inc(i);
  sign := 1;
  if (i <= n) and (s[i] in ['+', '-']) then
  begin
    if s[i] = '-' then
      sign := -1;
    Inc(i);
  end;
  value := 0;
  sawDigit := False;
  while (i <= n) and (s[i] in ['0'..'9']) do
  begin
    value := value * 10 + (Ord(s[i]) - Ord('0'));
    sawDigit := True;
    Inc(i);
  end;
  if (i <= n) and (s[i] = '.') then
  begin
    Inc(i);
    scale := 0.1;
    while (i <= n) and (s[i] in ['0'..'9']) do
    begin
      value := value + (Ord(s[i]) - Ord('0')) * scale;
      scale := scale / 10;
      sawDigit := True;
      Inc(i);
    end;
  end;
  if not sawDigit then
    Exit;
  if (i <= n) and (s[i] in ['e', 'E']) then
  begin
    startDigits := i;
    Inc(i);
    expSign := 1;
    if (i <= n) and (s[i] in ['+', '-']) then
    begin
      if s[i] = '-' then
        expSign := -1;
      Inc(i);
    end;
    expo := 0;
    if (i <= n) and (s[i] in ['0'..'9']) then
    begin
      while (i <= n) and (s[i] in ['0'..'9']) and (expo < 400) do
      begin
        expo := expo * 10 + (Ord(s[i]) - Ord('0'));
        Inc(i);
      end;
      if expo > 38 then
        expo := 38;
      while expo > 0 do
      begin
        if expSign > 0 then
          value := value * 10
        else
          value := value / 10;
        Dec(expo);
      end;
    end
    else
      i := startDigits; { "1e" -> just 1 }
  end;
  if value > 3.4e38 then
    value := 3.4e38;
  Result := sign * value;
end;

function Cvar_InfoValidate(const s: AnsiString): Boolean;
begin
  Result := (Pos('\', s) = 0) and (Pos('"', s) = 0) and (Pos(';', s) = 0);
end;

function Cvar_Find(const name: AnsiString): PCvar;
var
  v: PCvar;
begin
  v := CvarVars;
  while v <> nil do
  begin
    if SameNameCI(v^.Name, name) then
      Exit(v);
    v := v^.Next;
  end;
  Result := nil;
end;

procedure AssignValue(v: PCvar; const value: AnsiString);
begin
  v^.Value := value;
  v^.NumValue := RealmsAtof(value);
  v^.Modified := True;
  if (v^.Flags and REALMS_CVAR_USERINFO) <> 0 then
    UserinfoModified := True;
end;

function ValidName(const name: AnsiString): Boolean;
var
  i: Integer;
begin
  Result := (Length(name) > 0) and (Length(name) < 64);
  if not Result then
    Exit;
  for i := 1 to Length(name) do
    if name[i] in [#0..' ', '"', ';', '\', '$'] then
      Exit(False);
end;

function Cvar_Get(const name, defaultValue: AnsiString; flags: LongInt): PCvar;
var
  v, tail: PCvar;
begin
  Result := nil;
  if not ValidName(name) then
  begin
    Com_Warning('invalid cvar name "' + name + '"');
    Exit;
  end;
  if (flags and (REALMS_CVAR_USERINFO or REALMS_CVAR_SERVERINFO)) <> 0 then
    if not Cvar_InfoValidate(defaultValue) then
    begin
      Com_Warning('invalid info cvar value for ' + name);
      Exit;
    end;

  v := Cvar_Find(name);
  if v <> nil then
  begin
    v^.Flags := v^.Flags or flags;
    v^.DefaultValue := defaultValue;
    Exit(v);
  end;

  New(v);
  v^.Name := name;
  v^.DefaultValue := defaultValue;
  v^.LatchedValue := '';
  v^.HasLatched := False;
  v^.Flags := flags;
  v^.Next := nil;
  AssignValue(v, defaultValue);

  { Append so cvarlist/writeconfig keep registration order. }
  if CvarVars = nil then
    CvarVars := v
  else
  begin
    tail := CvarVars;
    while tail^.Next <> nil do
      tail := tail^.Next;
    tail^.Next := v;
  end;
  Result := v;
end;

function Cvar_Set2(const name, value: AnsiString; force: Boolean): LongInt;
var
  v: PCvar;
begin
  v := Cvar_Find(name);
  if v = nil then
  begin
    { Like Quake 2: setting an unknown variable creates it. }
    if Cvar_Get(name, value, 0) <> nil then
      Exit(REALMS_CVAR_SET_OK);
    Exit(REALMS_CVAR_SET_UNKNOWN);
  end;

  if (v^.Flags and (REALMS_CVAR_USERINFO or REALMS_CVAR_SERVERINFO)) <> 0 then
    if not Cvar_InfoValidate(value) then
    begin
      Com_Printf('invalid info cvar value');
      Exit(REALMS_CVAR_SET_READONLY);
    end;

  if not force then
  begin
    if (v^.Flags and REALMS_CVAR_NOSET) <> 0 then
    begin
      Com_Printf(name + ' is write protected.');
      Exit(REALMS_CVAR_SET_READONLY);
    end;

    if ((v^.Flags and REALMS_CVAR_CHEAT) <> 0) and (value <> v^.Value) then
      if (not SameNameCI(name, 'sv_cheats')) and
         (Cvar_VariableValue('sv_cheats') = 0) then
      begin
        Com_Printf(name + ' is cheat protected.');
        Exit(REALMS_CVAR_SET_CHEAT);
      end;

    if (v^.Flags and REALMS_CVAR_LATCH) <> 0 then
    begin
      if v^.HasLatched then
      begin
        if value = v^.LatchedValue then
          Exit(REALMS_CVAR_SET_LATCHED);
      end
      else if value = v^.Value then
        Exit(REALMS_CVAR_SET_OK);
      v^.LatchedValue := value;
      v^.HasLatched := True;
      Com_Printf(name + ' will be changed for next game.');
      Exit(REALMS_CVAR_SET_LATCHED);
    end;
  end
  else if v^.HasLatched then
  begin
    v^.HasLatched := False;
    v^.LatchedValue := '';
  end;

  if value = v^.Value then
    Exit(REALMS_CVAR_SET_OK);
  AssignValue(v, value);
  Result := REALMS_CVAR_SET_OK;
end;

function Cvar_Set(const name, value: AnsiString): LongInt;
begin
  Result := Cvar_Set2(name, value, False);
end;

function Cvar_ForceSet(const name, value: AnsiString): LongInt;
begin
  Result := Cvar_Set2(name, value, True);
end;

function Cvar_FullSet(const name, value: AnsiString; flags: LongInt): PCvar;
var
  v: PCvar;
begin
  v := Cvar_Find(name);
  if v = nil then
    Exit(Cvar_Get(name, value, flags));
  if value <> v^.Value then
    AssignValue(v, value);
  v^.Flags := flags;
  Result := v;
end;

function Cvar_VariableValue(const name: AnsiString): Single;
var
  v: PCvar;
begin
  v := Cvar_Find(name);
  if v = nil then
    Exit(0);
  Result := v^.NumValue;
end;

function Cvar_VariableString(const name: AnsiString): AnsiString;
var
  v: PCvar;
begin
  v := Cvar_Find(name);
  if v = nil then
    Exit('');
  Result := v^.Value;
end;

procedure Cvar_GetLatchedVars;
var
  v: PCvar;
begin
  v := CvarVars;
  while v <> nil do
  begin
    if v^.HasLatched then
    begin
      v^.HasLatched := False;
      AssignValue(v, v^.LatchedValue);
      v^.LatchedValue := '';
    end;
    v := v^.Next;
  end;
end;

procedure Cvar_ClearAll;
var
  v, n: PCvar;
begin
  v := CvarVars;
  while v <> nil do
  begin
    n := v^.Next;
    Dispose(v);
    v := n;
  end;
  CvarVars := nil;
  UserinfoModified := False;
end;

function Cvar_Count: LongInt;
var
  v: PCvar;
begin
  Result := 0;
  v := CvarVars;
  while v <> nil do
  begin
    Inc(Result);
    v := v^.Next;
  end;
end;

function Cvar_ArchiveText: AnsiString;
var
  v: PCvar;
begin
  { Cvar_WriteVariables: archived cvars as executable config lines. }
  Result := '// generated by Realms Engine (Pascal core), do not modify' + LineEnding;
  v := CvarVars;
  while v <> nil do
  begin
    if (v^.Flags and REALMS_CVAR_ARCHIVE) <> 0 then
    begin
      if v^.HasLatched then
        Result := Result + 'seta ' + v^.Name + ' "' + v^.LatchedValue + '"' + LineEnding
      else
        Result := Result + 'seta ' + v^.Name + ' "' + v^.Value + '"' + LineEnding;
    end;
    v := v^.Next;
  end;
end;

end.
