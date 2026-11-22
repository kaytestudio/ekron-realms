{ ----------------------------------------------------------------------------
  Realms Engine - Free Pascal core library

  Builds the Quake 2 derived core (cvars, command buffer, PAK filesystem,
  BSP collision model) as
    - librealms_core.so     (Linux desktop, Android jniLibs)
    - librealms_core.dylib  (macOS .app bundle)
    - librealms_core.a      (iOS, static: see realms/pascal/build_core.sh)
  and exports a single C entry point, realms_pascal_get_api, that returns the
  RealmsCoreApi function table defined in realms/include/realms/realms_bridge.h.

  No Pascal exception may unwind into C++: every exported function is wrapped.
  ---------------------------------------------------------------------------- }
library realms_core;

{$mode objfpc}{$H+}

uses
  {$IFDEF UNIX}
  cthreads,
  {$ENDIF}
  ctypes, SysUtils, Math,
  rlm_bridge, rlm_console, rlm_cvar, rlm_cmd, rlm_files, rlm_cmodel;

{$I rlm_core_api.inc}

exports
  realms_pascal_get_api;

begin
  FillApi;
  MaskFloatingPointExceptions;
end.
