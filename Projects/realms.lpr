{ realms-pascal: the Pascal core compiled in, running the C++ desktop sandbox
  (libekron_sandbox) with it. Takes the same options as ekron-realms.

  Built by cmake -DENABLE_FPC_CLIENT=ON. By hand from Projects/, with
  libekron_sandbox in <build>:
    fpc -Mobjfpc -Sh -O2 -Fu../realms/pascal -Fi../realms/pascal -Fl<build> realms.lpr }
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

{ sys/desktop/ekron_sandbox.h. On Darwin FPC ignores the library named by
  `external`, so link it explicitly. }
{$linklib ekron_sandbox}
function ekron_sandbox_run(argc: cint; argv: PPChar; pascalCore: TRealmsGetApi): cint; cdecl;
  external 'ekron_sandbox' name 'ekron_sandbox_run';

begin
  FillApi;
  { SDL, the GL driver and the C++ sandbox expect IEEE masked exceptions. }
  MaskFloatingPointExceptions;
  ExitCode := ekron_sandbox_run(argc, argv, @realms_pascal_get_api);
end.
