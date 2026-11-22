{ Command buffer, from qcommon/cmd.c, plus the cvar commands from cvar.c.
  A line runs as: built-in or host command, alias, cvar, else "Unknown
  command". Keep in step with realms/src/NativeCore.cpp. }
unit rlm_cmd;

{$mode objfpc}{$H+}

interface

const
  MAX_CBUF_TEXT = 65536;
  MAX_STRING_TOKENS = 80;
  ALIAS_LOOP_COUNT = 16;
  MACRO_LOOP_LIMIT = 100;

type
  TCmdProc = procedure;
  TArgArray = array of AnsiString;
  TCmdKind = (ckBuiltin, ckHost);

procedure Cmd_Init;
procedure Cmd_Shutdown;

procedure Cbuf_AddText(const text: AnsiString);
procedure Cbuf_InsertText(const text: AnsiString);
procedure Cbuf_Execute;

procedure Cmd_TokenizeString(const text: AnsiString; expandMacros: Boolean);
function Cmd_Argc: LongInt;
function Cmd_Argv(i: LongInt): AnsiString;
function Cmd_Args: AnsiString;
procedure Cmd_ExecuteString(const text: AnsiString);

function Cmd_AddCommand(const name: AnsiString; proc: TCmdProc; kind: TCmdKind): Boolean;
procedure Cmd_RemoveCommand(const name: AnsiString);
function Cmd_Exists(const name: AnsiString): Boolean;

implementation

uses
  SysUtils, rlm_bridge, rlm_console, rlm_cvar, rlm_files;

type
  PCmd = ^TCmd;
  TCmd = record
    Name: AnsiString;
    Kind: TCmdKind;
    Proc: TCmdProc;
    Next: PCmd;
  end;

  PAlias = ^TAlias;
  TAlias = record
    Name: AnsiString;
    Value: AnsiString;
    Next: PAlias;
  end;

var
  CmdText: AnsiString = '';
  CmdWait: Boolean = False;
  AliasCount: LongInt = 0;
  Commands: PCmd = nil;
  Aliases: PAlias = nil;
  CmdArgv: TArgArray;
  CmdArgsText: AnsiString = '';

{ ---------------------------------------------------------------- Cbuf --- }

procedure Cbuf_AddText(const text: AnsiString);
begin
  if Length(CmdText) + Length(text) > MAX_CBUF_TEXT then
  begin
    Com_Warning('Cbuf_AddText: overflow');
    Exit;
  end;
  CmdText := CmdText + text;
end;

procedure Cbuf_InsertText(const text: AnsiString);
begin
  { Inserted text runs before the rest of the buffer (exec, alias). }
  if Length(CmdText) + Length(text) + 1 > MAX_CBUF_TEXT then
  begin
    Com_Warning('Cbuf_InsertText: overflow');
    Exit;
  end;
  if (Length(text) > 0) and (text[Length(text)] <> #10) then
    CmdText := text + #10 + CmdText
  else
    CmdText := text + CmdText;
end;

procedure Cbuf_Execute;
var
  i, quotes: Integer;
  line: AnsiString;
begin
  AliasCount := 0;
  while Length(CmdText) > 0 do
  begin
    quotes := 0;
    i := 1;
    while i <= Length(CmdText) do
    begin
      if CmdText[i] = '"' then
        Inc(quotes);
      if ((quotes and 1) = 0) and (CmdText[i] = ';') then
        Break;
      if CmdText[i] = #10 then
        Break;
      Inc(i);
    end;
    line := Copy(CmdText, 1, i - 1);
    Delete(CmdText, 1, i); { also removes the ';' or newline }

    Cmd_ExecuteString(line);

    if CmdWait then
    begin
      { Skip out while text still remains in buffer, leaving it for the
        next frame (Quake 2 "wait"). }
      CmdWait := False;
      Break;
    end;
  end;
end;

{ ----------------------------------------------------------- Tokenizer --- }

function IsNameChar(c: Char): Boolean;
begin
  Result := c in ['a'..'z', 'A'..'Z', '0'..'9', '_', '.'];
end;

function MacroExpand(const text: AnsiString): AnsiString;
var
  i, j, expansions: Integer;
  inQuote: Boolean;
  name: AnsiString;
begin
  Result := '';
  inQuote := False;
  expansions := 0;
  i := 1;
  while i <= Length(text) do
  begin
    if text[i] = '"' then
      inQuote := not inQuote;
    if (not inQuote) and (text[i] = '$') and (i < Length(text)) and IsNameChar(text[i + 1]) then
    begin
      j := i + 1;
      while (j <= Length(text)) and IsNameChar(text[j]) do
        Inc(j);
      name := Copy(text, i + 1, j - i - 1);
      Inc(expansions);
      if expansions > MACRO_LOOP_LIMIT then
      begin
        Com_Warning('Macro expansion loop, discarded.');
        Exit('');
      end;
      Result := Result + Cvar_VariableString(name);
      i := j;
      Continue;
    end;
    Result := Result + text[i];
    Inc(i);
  end;
  if inQuote then
    Com_Printf('Line has unmatched quote, discarded.');
  if inQuote then
    Result := '';
end;

procedure Cmd_TokenizeString(const text: AnsiString; expandMacros: Boolean);
var
  s, token: AnsiString;
  i, n, argStart: Integer;
begin
  SetLength(CmdArgv, 0);
  CmdArgsText := '';
  if expandMacros then
    s := MacroExpand(text)
  else
    s := text;
  n := Length(s);
  i := 1;
  while True do
  begin
    { skip whitespace up to a newline }
    while (i <= n) and (s[i] <= ' ') and (s[i] <> #10) do
      Inc(i);
    if (i > n) or (s[i] = #10) then
      Break;
    { // comments end the line }
    if (s[i] = '/') and (i < n) and (s[i + 1] = '/') then
      Break;

    if Length(CmdArgv) = 1 then
    begin
      { Cmd_Args: everything after the command name, trailing blanks cut. }
      argStart := i;
      CmdArgsText := Copy(s, argStart, n - argStart + 1);
      while (Length(CmdArgsText) > 0) and (CmdArgsText[Length(CmdArgsText)] <= ' ') do
        SetLength(CmdArgsText, Length(CmdArgsText) - 1);
    end;

    token := '';
    if s[i] = '"' then
    begin
      Inc(i);
      while (i <= n) and (s[i] <> '"') do
      begin
        token := token + s[i];
        Inc(i);
      end;
      if i <= n then
        Inc(i); { closing quote }
    end
    else
    begin
      while (i <= n) and (s[i] > ' ') do
      begin
        token := token + s[i];
        Inc(i);
      end;
    end;

    if Length(CmdArgv) < MAX_STRING_TOKENS then
    begin
      SetLength(CmdArgv, Length(CmdArgv) + 1);
      CmdArgv[High(CmdArgv)] := token;
    end;
  end;
end;

function Cmd_Argc: LongInt;
begin
  Result := Length(CmdArgv);
end;

function Cmd_Argv(i: LongInt): AnsiString;
begin
  if (i < 0) or (i >= Length(CmdArgv)) then
    Exit('');
  Result := CmdArgv[i];
end;

function Cmd_Args: AnsiString;
begin
  Result := CmdArgsText;
end;

{ ------------------------------------------------------------ Registry --- }

function FindCmd(const name: AnsiString): PCmd;
var
  c: PCmd;
begin
  c := Commands;
  while c <> nil do
  begin
    if SameNameCI(c^.Name, name) then
      Exit(c);
    c := c^.Next;
  end;
  Result := nil;
end;

function FindAlias(const name: AnsiString): PAlias;
var
  a: PAlias;
begin
  a := Aliases;
  while a <> nil do
  begin
    if SameNameCI(a^.Name, name) then
      Exit(a);
    a := a^.Next;
  end;
  Result := nil;
end;

function Cmd_AddCommand(const name: AnsiString; proc: TCmdProc; kind: TCmdKind): Boolean;
var
  c: PCmd;
begin
  Result := False;
  if name = '' then
    Exit;
  if Cvar_Find(name) <> nil then
  begin
    Com_Printf('Cmd_AddCommand: ' + name + ' already defined as a var');
    Exit;
  end;
  if FindCmd(name) <> nil then
  begin
    Com_Printf('Cmd_AddCommand: ' + name + ' already defined');
    Exit;
  end;
  New(c);
  c^.Name := name;
  c^.Kind := kind;
  c^.Proc := proc;
  c^.Next := Commands;
  Commands := c;
  Result := True;
end;

procedure Cmd_RemoveCommand(const name: AnsiString);
var
  c, prev: PCmd;
begin
  prev := nil;
  c := Commands;
  while c <> nil do
  begin
    if SameNameCI(c^.Name, name) then
    begin
      if prev = nil then
        Commands := c^.Next
      else
        prev^.Next := c^.Next;
      Dispose(c);
      Exit;
    end;
    prev := c;
    c := c^.Next;
  end;
end;

function Cmd_Exists(const name: AnsiString): Boolean;
begin
  Result := FindCmd(name) <> nil;
end;

{ ------------------------------------------------------- cvar commands --- }

function Cvar_Command: Boolean;
var
  v: PCvar;
begin
  v := Cvar_Find(Cmd_Argv(0));
  if v = nil then
    Exit(False);
  if Cmd_Argc = 1 then
    Com_Printf('"' + v^.Name + '" is "' + v^.Value + '"')
  else
    Cvar_Set(v^.Name, Cmd_Argv(1));
  Result := True;
end;

procedure Cvar_Set_f;
var
  flags: LongInt;
begin
  if (Cmd_Argc <> 3) and (Cmd_Argc <> 4) then
  begin
    Com_Printf('usage: set <variable> <value> [u / s]');
    Exit;
  end;
  if Cmd_Argc = 4 then
  begin
    if Cmd_Argv(3) = 'u' then
      flags := REALMS_CVAR_USERINFO
    else if Cmd_Argv(3) = 's' then
      flags := REALMS_CVAR_SERVERINFO
    else
    begin
      Com_Printf('flags can only be ''u'' or ''s''');
      Exit;
    end;
    Cvar_FullSet(Cmd_Argv(1), Cmd_Argv(2), flags);
  end
  else
    Cvar_Set(Cmd_Argv(1), Cmd_Argv(2));
end;

procedure Cvar_SetA_f;
var
  v: PCvar;
begin
  if Cmd_Argc <> 3 then
  begin
    Com_Printf('usage: seta <variable> <value>');
    Exit;
  end;
  Cvar_Set(Cmd_Argv(1), Cmd_Argv(2));
  v := Cvar_Find(Cmd_Argv(1));
  if v <> nil then
    v^.Flags := v^.Flags or REALMS_CVAR_ARCHIVE;
end;

procedure Cvar_Toggle_f;
var
  v: PCvar;
begin
  if Cmd_Argc <> 2 then
  begin
    Com_Printf('usage: toggle <variable>');
    Exit;
  end;
  v := Cvar_Find(Cmd_Argv(1));
  if v = nil then
  begin
    Com_Printf('toggle: unknown variable ' + Cmd_Argv(1));
    Exit;
  end;
  if v^.NumValue <> 0 then
    Cvar_Set(v^.Name, '0')
  else
    Cvar_Set(v^.Name, '1');
end;

procedure Cvar_Reset_f;
var
  v: PCvar;
begin
  if Cmd_Argc <> 2 then
  begin
    Com_Printf('usage: reset <variable>');
    Exit;
  end;
  v := Cvar_Find(Cmd_Argv(1));
  if v <> nil then
    Cvar_Set(v^.Name, v^.DefaultValue);
end;

procedure Cvar_List_f;
var
  v: PCvar;
  line: AnsiString;
  count: LongInt;
begin
  count := 0;
  v := CvarVars;
  while v <> nil do
  begin
    line := '';
    if (v^.Flags and REALMS_CVAR_ARCHIVE) <> 0 then line := line + '*' else line := line + ' ';
    if (v^.Flags and REALMS_CVAR_USERINFO) <> 0 then line := line + 'U' else line := line + ' ';
    if (v^.Flags and REALMS_CVAR_SERVERINFO) <> 0 then line := line + 'S' else line := line + ' ';
    if (v^.Flags and REALMS_CVAR_NOSET) <> 0 then line := line + '-'
    else if (v^.Flags and REALMS_CVAR_LATCH) <> 0 then line := line + 'L'
    else line := line + ' ';
    if (v^.Flags and REALMS_CVAR_CHEAT) <> 0 then line := line + 'C' else line := line + ' ';
    Com_Printf(line + ' ' + v^.Name + ' "' + v^.Value + '"');
    Inc(count);
    v := v^.Next;
  end;
  Com_Printf(IntToStr(count) + ' cvars');
end;

procedure Cvar_WriteConfig_f;
var
  name: AnsiString;
begin
  if Cmd_Argc > 1 then
    name := Cmd_Argv(1)
  else
    name := 'config.cfg';
  if HostWriteAll(name, Cvar_ArchiveText) then
    Com_Printf('Wrote ' + name)
  else
    Com_Warning('Couldn''t write ' + name);
end;

{ ------------------------------------------------------ core commands --- }

procedure Cmd_Exec_f;
var
  text: AnsiString;
begin
  if Cmd_Argc <> 2 then
  begin
    Com_Printf('exec <filename> : execute a script file');
    Exit;
  end;
  if not FS_LoadText(Cmd_Argv(1), text) then
  begin
    Com_Printf('couldn''t exec ' + Cmd_Argv(1));
    Exit;
  end;
  Com_Printf('execing ' + Cmd_Argv(1));
  Cbuf_InsertText(text);
end;

procedure Cmd_Echo_f;
var
  i: Integer;
  line: AnsiString;
begin
  line := '';
  for i := 1 to Cmd_Argc - 1 do
  begin
    if i > 1 then
      line := line + ' ';
    line := line + Cmd_Argv(i);
  end;
  Com_Printf(line);
end;

procedure Cmd_Alias_f;
var
  a: PAlias;
  name, value: AnsiString;
  i: Integer;
begin
  if Cmd_Argc = 1 then
  begin
    Com_Printf('Current alias commands:');
    a := Aliases;
    while a <> nil do
    begin
      Com_Printf(a^.Name + ' : ' + TrimRight(a^.Value));
      a := a^.Next;
    end;
    Exit;
  end;
  name := Cmd_Argv(1);
  if Length(name) >= 32 then
  begin
    Com_Printf('Alias name is too long');
    Exit;
  end;
  value := '';
  for i := 2 to Cmd_Argc - 1 do
  begin
    if i > 2 then
      value := value + ' ';
    value := value + Cmd_Argv(i);
  end;
  value := value + #10;

  a := FindAlias(name);
  if a = nil then
  begin
    New(a);
    a^.Name := name;
    a^.Next := Aliases;
    Aliases := a;
  end;
  a^.Value := value;
end;

procedure Cmd_Unalias_f;
var
  a, prev: PAlias;
begin
  if Cmd_Argc <> 2 then
  begin
    Com_Printf('unalias <name> : delete an alias');
    Exit;
  end;
  prev := nil;
  a := Aliases;
  while a <> nil do
  begin
    if SameNameCI(a^.Name, Cmd_Argv(1)) then
    begin
      if prev = nil then
        Aliases := a^.Next
      else
        prev^.Next := a^.Next;
      Dispose(a);
      Exit;
    end;
    prev := a;
    a := a^.Next;
  end;
end;

procedure Cmd_Wait_f;
begin
  CmdWait := True;
end;

procedure Cmd_List_f;
var
  c: PCmd;
  count: LongInt;
begin
  count := 0;
  c := Commands;
  while c <> nil do
  begin
    Com_Printf(c^.Name);
    Inc(count);
    c := c^.Next;
  end;
  Com_Printf(IntToStr(count) + ' commands');
end;

{ ----------------------------------------------------------- Execution --- }

procedure Cmd_ExecuteString(const text: AnsiString);
var
  c: PCmd;
  a: PAlias;
  args: TArgArray;
begin
  Cmd_TokenizeString(text, True);
  if Cmd_Argc = 0 then
    Exit;

  c := FindCmd(Cmd_Argv(0));
  if c <> nil then
  begin
    case c^.Kind of
      ckBuiltin:
        if Assigned(c^.Proc) then
          c^.Proc();
      ckHost:
        begin
          { Copy so the host may re-enter the core from its handler. }
          args := Copy(CmdArgv, 0, Length(CmdArgv));
          HostCommand(args);
        end;
    end;
    Exit;
  end;

  a := FindAlias(Cmd_Argv(0));
  if a <> nil then
  begin
    Inc(AliasCount);
    if AliasCount >= ALIAS_LOOP_COUNT then
    begin
      Com_Printf('ALIAS_LOOP_COUNT');
      Exit;
    end;
    Cbuf_InsertText(a^.Value);
    Exit;
  end;

  if Cvar_Command then
    Exit;

  Com_Printf('Unknown command "' + Cmd_Argv(0) + '"');
end;

procedure Cmd_Init;
begin
  Cmd_AddCommand('cmdlist', @Cmd_List_f, ckBuiltin);
  Cmd_AddCommand('exec', @Cmd_Exec_f, ckBuiltin);
  Cmd_AddCommand('echo', @Cmd_Echo_f, ckBuiltin);
  Cmd_AddCommand('alias', @Cmd_Alias_f, ckBuiltin);
  Cmd_AddCommand('unalias', @Cmd_Unalias_f, ckBuiltin);
  Cmd_AddCommand('wait', @Cmd_Wait_f, ckBuiltin);
  Cmd_AddCommand('set', @Cvar_Set_f, ckBuiltin);
  Cmd_AddCommand('seta', @Cvar_SetA_f, ckBuiltin);
  Cmd_AddCommand('toggle', @Cvar_Toggle_f, ckBuiltin);
  Cmd_AddCommand('reset', @Cvar_Reset_f, ckBuiltin);
  Cmd_AddCommand('cvarlist', @Cvar_List_f, ckBuiltin);
  Cmd_AddCommand('writeconfig', @Cvar_WriteConfig_f, ckBuiltin);
end;

procedure Cmd_Shutdown;
var
  c, cn: PCmd;
  a, an: PAlias;
begin
  c := Commands;
  while c <> nil do
  begin
    cn := c^.Next;
    Dispose(c);
    c := cn;
  end;
  Commands := nil;
  a := Aliases;
  while a <> nil do
  begin
    an := a^.Next;
    Dispose(a);
    a := an;
  end;
  Aliases := nil;
  CmdText := '';
  CmdWait := False;
  AliasCount := 0;
  SetLength(CmdArgv, 0);
  CmdArgsText := '';
end;

end.
