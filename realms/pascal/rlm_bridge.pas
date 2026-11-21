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
  REALMS_BRIDGE_VERSION = 2;

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

  { Quake 2 q_shared.h CONTENTS_* / MASK_* (bridge version 2). }
  REALMS_CONTENTS_SOLID = $1;
  REALMS_CONTENTS_WINDOW = $2;
  REALMS_CONTENTS_AUX = $4;
  REALMS_CONTENTS_LAVA = $8;
  REALMS_CONTENTS_SLIME = $10;
  REALMS_CONTENTS_WATER = $20;
  REALMS_CONTENTS_MIST = $40;
  REALMS_CONTENTS_AREAPORTAL = $8000;
  REALMS_CONTENTS_PLAYERCLIP = $10000;
  REALMS_CONTENTS_MONSTERCLIP = $20000;
  REALMS_CONTENTS_ORIGIN = $1000000;
  REALMS_CONTENTS_MONSTER = $2000000;
  REALMS_CONTENTS_DEADMONSTER = $4000000;
  REALMS_CONTENTS_DETAIL = $8000000;
  REALMS_CONTENTS_TRANSLUCENT = $10000000;
  REALMS_CONTENTS_LADDER = $20000000;

  REALMS_MASK_ALL = -1;
  REALMS_MASK_SOLID = REALMS_CONTENTS_SOLID or REALMS_CONTENTS_WINDOW;
  REALMS_MASK_PLAYERSOLID = REALMS_CONTENTS_SOLID or REALMS_CONTENTS_PLAYERCLIP or
    REALMS_CONTENTS_WINDOW or REALMS_CONTENTS_MONSTER;
  REALMS_MASK_WATER = REALMS_CONTENTS_WATER or REALMS_CONTENTS_LAVA or REALMS_CONTENTS_SLIME;

  { Quake 2 surface flags (SURF_*). }
  REALMS_SURF_LIGHT = $1;
  REALMS_SURF_SLICK = $2;
  REALMS_SURF_SKY = $4;
  REALMS_SURF_WARP = $8;
  REALMS_SURF_TRANS33 = $10;
  REALMS_SURF_TRANS66 = $20;
  REALMS_SURF_FLOWING = $40;
  REALMS_SURF_NODRAW = $80;

type
  TRealmsVec3 = array[0..2] of cfloat;

  { trace_t (RealmsTrace, 84 bytes). }
  PRealmsTrace = ^TRealmsTrace;
  TRealmsTrace = record
    allsolid: cint32;
    startsolid: cint32;
    fraction: cfloat;
    endpos: TRealmsVec3;
    plane_normal: TRealmsVec3;
    plane_dist: cfloat;
    contents: cint32;
    surface_flags: cint32;
    surface_value: cint32;
    surface_name: array[0..31] of AnsiChar;
  end;

  { RealmsMapInfo (144 bytes). }
  PRealmsMapInfo = ^TRealmsMapInfo;
  TRealmsMapInfo = record
    struct_size: cuint32;
    checksum: cuint32;
    num_planes: cint32;
    num_nodes: cint32;
    num_leafs: cint32;
    num_leaf_brushes: cint32;
    num_brushes: cint32;
    num_brush_sides: cint32;
    num_texinfo: cint32;
    num_models: cint32;
    num_clusters: cint32;
    num_areas: cint32;
    num_area_portals: cint32;
    entity_length: cint32;
    mins: TRealmsVec3;
    maxs: TRealmsVec3;
    name: array[0..63] of AnsiChar;
  end;

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
  TCoreMapLoad = function(path: PChar): cint32; cdecl;
  TCoreMapInfo = function(out_info: PRealmsMapInfo): cint32; cdecl;
  TCoreMapEntities = function(buffer: PChar; buffer_size: cint32): cint32; cdecl;
  TCoreMapModel = function(index: cint32; mins, maxs, origin: pcfloat;
    headnode: pcint32): cint32; cdecl;
  TCoreMapHeadnodeForBox = function(mins, maxs: pcfloat): cint32; cdecl;
  TCoreMapPointContents = function(point: pcfloat; headnode: cint32;
    origin, angles: pcfloat): cint32; cdecl;
  TCoreMapBoxTrace = procedure(start, end_, mins, maxs: pcfloat;
    headnode, brushmask: cint32; origin, angles: pcfloat;
    out_trace: PRealmsTrace); cdecl;
  TCoreMapPointLeaf = function(point: pcfloat): cint32; cdecl;
  TCoreMapLeafInfo = function(leaf: cint32; contents, cluster, area: pcint32): cint32; cdecl;

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

    { Collision model, bridge version 2. }
    map_load: TCoreMapLoad;
    map_info: TCoreMapInfo;
    map_entities: TCoreMapEntities;
    map_model: TCoreMapModel;
    map_headnode_for_box: TCoreMapHeadnodeForBox;
    map_point_contents: TCoreMapPointContents;
    map_box_trace: TCoreMapBoxTrace;
    map_point_leaf: TCoreMapPointLeaf;
    map_leaf_info: TCoreMapLeafInfo;
  end;

implementation

end.
