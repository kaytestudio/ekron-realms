{ ----------------------------------------------------------------------------
  Ekron Realms - Free Pascal client (`realms-pascal`)

  One native program, two languages:
    - the Realms game core is Free Pascal (realms/pascal: Quake 2 cvars,
      command buffer, PAK filesystem, BSP and mesh collision) and is compiled
      straight into this program;
    - window, input, OpenGL renderer, audio and the sandbox world are the C++
      desktop sandbox (sys/desktop), linked as libekron_sandbox. SDL2 is used
      from C++, so this program needs no Pascal SDL bindings.
  The sandbox gets the core through realms_pascal_get_api (origin "linked"),
  exactly the table librealms_core exports, so the game behaves the same as
  `ekron-realms` on the Pascal core.

  Options are the sandbox's: --version, --engine-info, --headless-smoke,
  --assets <dir>, --lang <code>, +set <cvar> <value>, ...

  Build: cmake -DENABLE_FPC_CLIENT=ON builds it as <build>/realms-pascal (and
  EkronRealms.app/Contents/MacOS/realms-pascal). By hand, from Projects/, once
  libekron_sandbox is built in <build>:
    fpc -Mobjfpc -Sh -O2 -Fu../realms/pascal -Fi../realms/pascal -Fl<build> realms.lpr

  The old, unported Quake 2 client graph is kept in realms_q2legacy.lpr.
  ---------------------------------------------------------------------------- }
program realms;

{$mode objfpc}{$H+}

uses
  {$IFDEF UNIX}
  cthreads,
  {$ENDIF}
  ctypes, SysUtils, Math,
  rlm_bridge, rlm_console, rlm_cvar, rlm_cmd, rlm_files, rlm_cmodel;

{ The Pascal core: realms_pascal_get_api, FillApi, MaskFloatingPointExceptions
  (shared with the librealms_core library, realms/pascal/realms_core.lpr). }
{$I rlm_core_api.inc}

type
  TRealmsGetApi = function(requested_version: cuint32): PRealmsCoreApi; cdecl;

{ sys/desktop/ekron_sandbox.h }
function ekron_sandbox_run(argc: cint; argv: PPChar; pascalCore: TRealmsGetApi): cint; cdecl;
  external 'ekron_sandbox' name 'ekron_sandbox_run';

begin
  FillApi;
  { SDL, the GL driver and the C++ sandbox expect IEEE masked exceptions. }
  MaskFloatingPointExceptions;
  ExitCode := ekron_sandbox_run(argc, argv, @realms_pascal_get_api);
end.
