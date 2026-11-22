{ librealms_core (.so, .dylib, or .a for iOS). Exports realms_pascal_get_api,
  which returns the RealmsCoreApi table from realms_bridge.h. }
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
