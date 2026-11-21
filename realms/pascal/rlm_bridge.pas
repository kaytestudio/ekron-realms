{ ----------------------------------------------------------------------------
  Realms Engine - Pascal mirror of realms/include/realms/realms_bridge.h

  This unit must stay binary compatible with the C header: same field order,
  C calling convention (cdecl), C integer sizes and packed-as-C alignment.
  ---------------------------------------------------------------------------- }
unit rlm_bridge;

{$mode objfpc}{$H+}
{$PACKRECORDS C}

interface

uses
  ctypes;

const
  REALMS_BRIDGE_VERSION = 1;

  REALMS_PRINT_INFO = 0;
  REALMS_PRINT_WARNING = 1;
  REALMS_PRINT_ERROR = 2;
  REALMS_PRINT_DEVELOPER = 3;

  { Quake 2 q_shared.h CVAR_* flags plus the Realms CHEAT extension. }
  REALMS_CVAR_ARCHIVE = 1;
  REALMS_CVAR_USERINFO = 2;
  REALMS_CVAR_SERVERINFO = 4;
  REALMS_CVAR_NOSET = 8;
  REALMS_CVAR_LATCH = 16;
  REALMS_CVAR_CHEAT = 32;

  REALMS_CVAR_SET_OK = 1;
  REALMS_CVAR_SET_UNKNOWN = 0;
  REALMS_CVAR_SET_READONLY = -1;
  REALMS_CVAR_SET_LATCHED = -2;
  REALMS_CVAR_SET_CHEAT = -3;

type
  THostPrint = procedure(user: Pointer; level: cint32; text: PChar); cdecl;
  THostReadFile = function(user: Pointer; path: PChar; out_data: PPointer;
    out_size: pcint32): cint32; cdecl;
  THostFreeFile = procedure(user: Pointer; data: Pointer); cdecl;
  THostReadFileRange = function(user: Pointer; path: PChar; offset: cint64;
    size: cint32; out_buffer: Pointer): cint32; cdecl;
  THostWriteFile = function(user: Pointer; path: PChar; data: Pointer;
    size: cint32): cint32; cdecl;
  THostCommand = procedure(user: Pointer; argc: cint32; argv: PPChar); cdecl;
  THostMilliseconds = function(user: Pointer): cdouble; cdecl;

  PRealmsHostApi = ^TRealmsHostApi;
  TRealmsHostApi = record
    struct_size: cuint32;
    version: cuint32;
    user: Pointer;
    print: THostPrint;
    read_file: THostReadFile;
    free_file: THostFreeFile;
    read_file_range: THostReadFileRange;
    write_file: THostWriteFile;
    command: THostCommand;
    milliseconds: THostMilliseconds;
  end;

  TRealmsCvarVisitor = procedure(user: Pointer; name, value: PChar;
    flags: cint32); cdecl;

  TCoreBackendName = function: PChar; cdecl;
  TCoreInit = function(host: PRealmsHostApi): cint32; cdecl;
  TCoreShutdown = procedure; cdecl;
  TCoreFrame = procedure(msec: cint32); cdecl;
  TCoreCvarRegister = function(name, default_value: PChar; flags: cint32): cint32; cdecl;
  TCoreCvarSet = function(name, value: PChar): cint32; cdecl;
  TCoreCvarGet = function(name: PChar; buffer: PChar; buffer_size: cint32): cint32; cdecl;
  TCoreCvarValue = function(name: PChar): cfloat; cdecl;
  TCoreCvarFlags = function(name: PChar): cint32; cdecl;
  TCoreCvarModified = function(name: PChar; clear: cint32): cint32; cdecl;
  TCoreCvarForeach = procedure(visitor: TRealmsCvarVisitor; user: Pointer); cdecl;
  TCoreName = function(name: PChar): cint32; cdecl;
  TCoreNameProc = procedure(name: PChar); cdecl;
  TCoreText = procedure(text: PChar); cdecl;
  TCoreVoid = procedure; cdecl;
  TCoreFsAddPak = function(path: PChar): cint32; cdecl;
  TCoreFsLoad = function(path: PChar; out_data: PPointer; out_size: pcint32): cint32; cdecl;
  TCoreFsFree = procedure(data: Pointer); cdecl;
  TCoreCount = function: cint32; cdecl;

  PRealmsCoreApi = ^TRealmsCoreApi;
  TRealmsCoreApi = record
    struct_size: cuint32;
    version: cuint32;

    backend_name: TCoreBackendName;

    init: TCoreInit;
    shutdown: TCoreShutdown;
    frame: TCoreFrame;

    cvar_register: TCoreCvarRegister;
    cvar_set: TCoreCvarSet;
    cvar_force_set: TCoreCvarSet;
    cvar_get: TCoreCvarGet;
    cvar_value: TCoreCvarValue;
    cvar_flags: TCoreCvarFlags;
    cvar_modified: TCoreCvarModified;
    cvar_foreach: TCoreCvarForeach;

    cmd_register: TCoreName;
    cmd_unregister: TCoreNameProc;
    cmd_exists: TCoreName;
    cbuf_add_text: TCoreText;
    cbuf_insert_text: TCoreText;
    cbuf_execute: TCoreVoid;
    cmd_execute_string: TCoreText;

    fs_add_pak: TCoreFsAddPak;
    fs_load: TCoreFsLoad;
    fs_free: TCoreFsFree;
    fs_pak_count: TCoreCount;
  end;

implementation

end.
