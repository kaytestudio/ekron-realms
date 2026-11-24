unit input;

{$mode objfpc}{$H+}

interface

uses
  SDL2,
  SysUtils,
  Common,   // Com_Printf
  cvar,     // Cvar_Get
  keys;     // Key_ClearStates

// in_*.c: the platform input layer. Only the cvars, focus handling and
// Key_ClearStates are implemented; mouse and joystick input are not.
procedure IN_Init;
procedure IN_Shutdown;
procedure IN_Frame;
procedure IN_Activate(active: Boolean);
procedure IN_MouseMove(dx, dy: Integer);
procedure IN_MouseEvent(button, state: Integer);
procedure IN_JoyMove(axis, value: Integer);
procedure IN_JoyButtons(button, state: Integer);

var
  in_mouse: cvar_p;
  in_joystick: cvar_p;

implementation

var
  InputInitialized: Boolean = False;


procedure IN_Init;
begin
  if InputInitialized then Exit;

  Com_Printf(PRINT_ALL, '------- Input Initialization -------'#10);


  in_mouse := Cvar_Get('in_mouse', '1', CVAR_ARCHIVE);
  in_joystick := Cvar_Get('in_joystick', '0', CVAR_ARCHIVE);

  InputInitialized := True;
  Com_Printf(PRINT_ALL, '------------------------------------'#10);
end;

procedure IN_Shutdown;
begin
  if not InputInitialized then Exit;

  Com_Printf(PRINT_ALL, '------- Input Shutdown -------'#10, []);
  InputInitialized := False;
  Com_Printf(PRINT_ALL, '------------------------------------'#10, []);
end;

procedure IN_Frame;
// Not implemented.
begin
end;

procedure IN_Activate(active: Boolean);
begin
  if active then
  begin
    Common.Com_Printf('Input Activated.'#10);
  end
  else
  begin
    Common.Com_Printf('Input Deactivated.'#10);
    // no stuck keys after losing focus
    keys.Key_ClearStates;
  end;
end;

// Mouse and joystick input: not implemented, the events are ignored.
procedure IN_MouseMove(dx, dy: Integer);
begin
end;

procedure IN_MouseEvent(button, state: Integer);
begin
end;

procedure IN_JoyMove(axis, value: Integer);
begin
end;

procedure IN_JoyButtons(button, state: Integer);
begin
end;

initialization
  // nothing: IN_Init / IN_Shutdown do the work
finalization
end.
