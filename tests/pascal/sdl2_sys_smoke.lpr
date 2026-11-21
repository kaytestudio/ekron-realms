{ SDL2 bindings smoke test for the Free Pascal sys layer (sys/mac/sys_mac.pas).

  Uses SDL2 exactly the way sys_mac.pas does: the same "in" path (resolved
  from Projects/, the directory realms.lpr / realms.lpi compile from) and the
  same calls, so a missing tools/SDL2-for-Pascal submodule or an API mismatch
  fails here. Run from Projects/:

    git submodule update --init tools/SDL2-for-Pascal
    cd Projects && fpc -Mobjfpc -FU/tmp/sdl2u -FE/tmp/sdl2u ../tests/pascal/sdl2_sys_smoke.lpr
    /tmp/sdl2u/sdl2_sys_smoke }
program sdl2_sys_smoke;

{$mode objfpc}{$H+}

uses
  SysUtils,
  SDL2 in '../tools/SDL2-for-Pascal/units/sdl2.pas';

var
  Version: TSDL_Version;
  StartTicks: UInt32;
  Text: PAnsiChar;
begin
  SDL_GetVersion(@Version);
  WriteLn(Format('SDL %d.%d.%d', [Version.major, Version.minor, Version.patch]));
  // TSysMac.Create
  if SDL_Init(SDL_INIT_TIMER or SDL_INIT_EVENTS) <> 0 then
  begin
    WriteLn('SDL_Init failed: ', SDL_GetError);
    Halt(1);
  end;
  // Sys_Milliseconds / Sys_Sleep
  StartTicks := SDL_GetTicks;
  SDL_Delay(20);
  if SDL_GetTicks - StartTicks < 15 then
  begin
    WriteLn('SDL_Delay / SDL_GetTicks did not advance');
    Halt(1);
  end;
  // TSysMac.GetClipboardData (needs the video subsystem; nil is fine headless)
  if SDL_HasClipboardText = SDL_TRUE then
  begin
    Text := SDL_GetClipboardText;
    if Assigned(Text) then
      SDL_free(Text);
  end;
  // TSysMac.Error would call SDL_ShowSimpleMessageBox; only check it links.
  if not Assigned(@SDL_ShowSimpleMessageBox) then
    Halt(1);
  SDL_Quit;
  WriteLn('sdl2_sys_smoke: OK');
end.
