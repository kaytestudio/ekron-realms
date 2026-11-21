{ ----------------------------------------------------------------------------
  Realms Engine - the Free Pascal core's RealmsCoreApi table.

  Shared by the two ways the core ships:
    - realms_core.lpr   library: librealms_core.so/.dylib/.a, loaded or linked
                        by realms::Engine (desktop, Android, iOS)
    - Projects/realms.lpr  the `realms-pascal` program: a Pascal executable
                        that links the C++ sandbox (libekron_sandbox) and
                        hands it this table directly
  The including file must use: ctypes, SysUtils, Math, rlm_bridge,
  rlm_console, rlm_cvar, rlm_cmd, rlm_files, rlm_cmodel.
  ---------------------------------------------------------------------------- }

const
  BACKEND_NAME: PChar = 'pascal-quake2';

var
  Api: TRealmsCoreApi;
  Initialized: Boolean = False;
  CoreTimeMs: Int64 = 0;

procedure ReportException(const where: AnsiString; E: Exception);
begin
  try
    Com_Error('[realms_core] ' + where + ': ' + E.ClassName + ': ' + E.Message);
  except
    { never let reporting fail across the ABI }
  end;
end;

procedure MaskFloatingPointExceptions;
begin
  { C/C++ hosts and GPU drivers expect IEEE masked exceptions; FPC's default
    unmasks some of them. Do this on every entry from the host. }
  SetExceptionMask([exInvalidOp, exDenormalized, exZeroDivide, exOverflow,
                    exUnderflow, exPrecision]);
end;

function StrOf(p: PChar): AnsiString;
begin
  if p = nil then
    Result := ''
  else
    Result := AnsiString(p);
end;

{ -------------------------------------------------------------- lifecycle }

function core_backend_name: PChar; cdecl;
begin
  Result := BACKEND_NAME;
end;

function core_init(host: PRealmsHostApi): cint32; cdecl;
begin
  Result := 0;
  try
    MaskFloatingPointExceptions;
    if Initialized then
    begin
      CM_UnloadMap;
      Cmd_Shutdown;
      FS_Shutdown;
      Cvar_ClearAll;
    end;
    BindHost(host);
    CoreTimeMs := 0;
    Cmd_Init;
    Cvar_Get('sv_cheats', '0', REALMS_CVAR_SERVERINFO or REALMS_CVAR_LATCH);
    Cvar_Get('developer', '0', 0);
    Cvar_Get('realms_core', 'pascal-quake2', REALMS_CVAR_NOSET);
    Initialized := True;
    Com_Printf('Realms core: Free Pascal Quake 2 stack online (FPC ' +
      {$I %FPCVERSION%} + ', ' + {$I %FPCTARGETOS%} + '-' + {$I %FPCTARGETCPU%} + ')');
    Result := 1;
  except
    on E: Exception do
      ReportException('init', E);
  end;
end;

procedure core_shutdown; cdecl;
begin
  try
    CM_UnloadMap;
    Cmd_Shutdown;
    FS_Shutdown;
    Cvar_ClearAll;
    Initialized := False;
    UnbindHost;
  except
    on E: Exception do
      ReportException('shutdown', E);
  end;
end;

procedure core_frame(msec: cint32); cdecl;
begin
  if not Initialized then
    Exit;
  try
    MaskFloatingPointExceptions;
    if msec > 0 then
      Inc(CoreTimeMs, msec);
    Cbuf_Execute;
  except
    on E: Exception do
      ReportException('frame', E);
  end;
end;

{ ------------------------------------------------------------------- cvars }

function core_cvar_register(name, default_value: PChar; flags: cint32): cint32; cdecl;
begin
  Result := 0;
  try
    if Cvar_Get(StrOf(name), StrOf(default_value), flags) <> nil then
      Result := 1;
  except
    on E: Exception do
      ReportException('cvar_register', E);
  end;
end;

function core_cvar_set(name, value: PChar): cint32; cdecl;
begin
  Result := REALMS_CVAR_SET_UNKNOWN;
  try
    Result := Cvar_Set(StrOf(name), StrOf(value));
  except
    on E: Exception do
      ReportException('cvar_set', E);
  end;
end;

function core_cvar_force_set(name, value: PChar): cint32; cdecl;
begin
  Result := REALMS_CVAR_SET_UNKNOWN;
  try
    Result := Cvar_ForceSet(StrOf(name), StrOf(value));
  except
    on E: Exception do
      ReportException('cvar_force_set', E);
  end;
end;

function core_cvar_get(name: PChar; buffer: PChar; buffer_size: cint32): cint32; cdecl;
var
  v: PCvar;
  n: Integer;
begin
  Result := -1;
  try
    if (buffer <> nil) and (buffer_size > 0) then
      buffer^ := #0;
    v := Cvar_Find(StrOf(name));
    if v = nil then
      Exit;
    Result := Length(v^.Value);
    if (buffer <> nil) and (buffer_size > 0) then
    begin
      n := Min(Length(v^.Value), buffer_size - 1);
      if n > 0 then
        Move(v^.Value[1], buffer^, n);
      buffer[n] := #0;
    end;
  except
    on E: Exception do
      ReportException('cvar_get', E);
  end;
end;

function core_cvar_value(name: PChar): cfloat; cdecl;
begin
  Result := 0;
  try
    Result := Cvar_VariableValue(StrOf(name));
  except
    on E: Exception do
      ReportException('cvar_value', E);
  end;
end;

function core_cvar_flags(name: PChar): cint32; cdecl;
var
  v: PCvar;
begin
  Result := -1;
  try
    v := Cvar_Find(StrOf(name));
    if v <> nil then
      Result := v^.Flags;
  except
    on E: Exception do
      ReportException('cvar_flags', E);
  end;
end;

function core_cvar_modified(name: PChar; clear: cint32): cint32; cdecl;
var
  v: PCvar;
begin
  Result := 0;
  try
    v := Cvar_Find(StrOf(name));
    if v = nil then
      Exit;
    if v^.Modified then
      Result := 1;
    if clear <> 0 then
      v^.Modified := False;
  except
    on E: Exception do
      ReportException('cvar_modified', E);
  end;
end;

procedure core_cvar_foreach(visitor: TRealmsCvarVisitor; user: Pointer); cdecl;
var
  v: PCvar;
begin
  if not Assigned(visitor) then
    Exit;
  try
    v := CvarVars;
    while v <> nil do
    begin
      visitor(user, PChar(v^.Name), PChar(v^.Value), v^.Flags);
      v := v^.Next;
    end;
  except
    on E: Exception do
      ReportException('cvar_foreach', E);
  end;
end;

{ ---------------------------------------------------------------- commands }

function core_cmd_register(name: PChar): cint32; cdecl;
begin
  Result := 0;
  try
    if Cmd_AddCommand(StrOf(name), nil, ckHost) then
      Result := 1;
  except
    on E: Exception do
      ReportException('cmd_register', E);
  end;
end;

procedure core_cmd_unregister(name: PChar); cdecl;
begin
  try
    Cmd_RemoveCommand(StrOf(name));
  except
    on E: Exception do
      ReportException('cmd_unregister', E);
  end;
end;

function core_cmd_exists(name: PChar): cint32; cdecl;
begin
  Result := 0;
  try
    if Cmd_Exists(StrOf(name)) then
      Result := 1;
  except
    on E: Exception do
      ReportException('cmd_exists', E);
  end;
end;

procedure core_cbuf_add_text(text: PChar); cdecl;
begin
  try
    Cbuf_AddText(StrOf(text));
  except
    on E: Exception do
      ReportException('cbuf_add_text', E);
  end;
end;

procedure core_cbuf_insert_text(text: PChar); cdecl;
begin
  try
    Cbuf_InsertText(StrOf(text));
  except
    on E: Exception do
      ReportException('cbuf_insert_text', E);
  end;
end;

procedure core_cbuf_execute; cdecl;
begin
  try
    MaskFloatingPointExceptions;
    Cbuf_Execute;
  except
    on E: Exception do
      ReportException('cbuf_execute', E);
  end;
end;

procedure core_cmd_execute_string(text: PChar); cdecl;
begin
  try
    MaskFloatingPointExceptions;
    Cmd_ExecuteString(StrOf(text));
  except
    on E: Exception do
      ReportException('cmd_execute_string', E);
  end;
end;

{ -------------------------------------------------------------- filesystem }

function core_fs_add_pak(path: PChar): cint32; cdecl;
begin
  Result := -1;
  try
    Result := FS_AddPak(StrOf(path));
  except
    on E: Exception do
      ReportException('fs_add_pak', E);
  end;
end;

function core_fs_load(path: PChar; out_data: PPointer; out_size: pcint32): cint32; cdecl;
var
  data: Pointer;
  size: LongInt;
begin
  Result := 0;
  try
    if out_data <> nil then
      out_data^ := nil;
    if out_size <> nil then
      out_size^ := 0;
    if FS_LoadFile(StrOf(path), data, size) then
    begin
      if out_data <> nil then
        out_data^ := data
      else
        FreeMem(data);
      if out_size <> nil then
        out_size^ := size;
      Result := 1;
    end;
  except
    on E: Exception do
      ReportException('fs_load', E);
  end;
end;

procedure core_fs_free(data: Pointer); cdecl;
begin
  try
    if data <> nil then
      FreeMem(data);
  except
    on E: Exception do
      ReportException('fs_free', E);
  end;
end;

function core_fs_pak_count: cint32; cdecl;
begin
  Result := FS_PakCount;
end;

{ --------------------------------------------------------- collision model }

function core_map_load(path: PChar): cint32; cdecl;
var
  name, err: AnsiString;
  data: Pointer;
  size: LongInt;
  ok: Boolean;
begin
  Result := 0;
  try
    MaskFloatingPointExceptions;
    name := FS_NormalizePath(StrOf(path));
    CM_UnloadMap;
    if name = '' then
      Exit(1);
    if not FS_LoadFile(name, data, size) then
    begin
      Com_Warning('map_load: couldn''t load ' + name);
      Exit;
    end;
    try
      ok := CM_LoadMap(name, PByte(data), size, err);
    finally
      FreeMem(data);
    end;
    if not ok then
    begin
      Com_Warning('map_load: ' + name + ': ' + err);
      Exit;
    end;
    Result := 1;
  except
    on E: Exception do
    begin
      CM_UnloadMap;
      ReportException('map_load', E);
    end;
  end;
end;

function core_map_info(out_info: PRealmsMapInfo): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_MapInfo(out_info);
  except
    on E: Exception do
      ReportException('map_info', E);
  end;
end;

function core_map_entities(buffer: PChar; buffer_size: cint32): cint32; cdecl;
begin
  Result := -1;
  try
    Result := CM_Entities(buffer, buffer_size);
  except
    on E: Exception do
      ReportException('map_entities', E);
  end;
end;

function core_map_model(index: cint32; mins, maxs, origin: pcfloat;
  headnode: pcint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_Model(index, mins, maxs, origin, headnode);
  except
    on E: Exception do
      ReportException('map_model', E);
  end;
end;

function core_map_headnode_for_box(mins, maxs: pcfloat): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_HeadnodeForBox(mins, maxs);
  except
    on E: Exception do
      ReportException('map_headnode_for_box', E);
  end;
end;

function core_map_point_contents(point: pcfloat; headnode: cint32;
  origin, angles: pcfloat): cint32; cdecl;
begin
  Result := 0;
  try
    MaskFloatingPointExceptions;
    Result := CM_PointContents(point, headnode, origin, angles);
  except
    on E: Exception do
      ReportException('map_point_contents', E);
  end;
end;

procedure core_map_box_trace(start, end_, mins, maxs: pcfloat;
  headnode, brushmask: cint32; origin, angles: pcfloat;
  out_trace: PRealmsTrace); cdecl;
begin
  try
    MaskFloatingPointExceptions;
    CM_BoxTrace(start, end_, mins, maxs, headnode, brushmask, origin, angles, out_trace);
  except
    on E: Exception do
      ReportException('map_box_trace', E);
  end;
end;

function core_map_point_leaf(point: pcfloat): cint32; cdecl;
begin
  Result := 0;
  try
    MaskFloatingPointExceptions;
    Result := CM_PointLeaf(point);
  except
    on E: Exception do
      ReportException('map_point_leaf', E);
  end;
end;

function core_map_leaf_info(leaf: cint32; contents, cluster, area: pcint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_LeafInfo(leaf, contents, cluster, area);
  except
    on E: Exception do
      ReportException('map_leaf_info', E);
  end;
end;

function core_map_cluster_pvs(cluster: cint32; buffer: PByte; buffer_size: cint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_ClusterPVS(cluster, buffer, buffer_size);
  except
    on E: Exception do
      ReportException('map_cluster_pvs', E);
  end;
end;

function core_map_cluster_phs(cluster: cint32; buffer: PByte; buffer_size: cint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_ClusterPHS(cluster, buffer, buffer_size);
  except
    on E: Exception do
      ReportException('map_cluster_phs', E);
  end;
end;

procedure core_map_set_area_portal(portal, open: cint32); cdecl;
begin
  try
    CM_SetAreaPortal(portal, open);
  except
    on E: Exception do
      ReportException('map_set_area_portal', E);
  end;
end;

function core_map_area_portal_open(portal: cint32): cint32; cdecl;
begin
  Result := -1;
  try
    Result := CM_AreaPortalOpen(portal);
  except
    on E: Exception do
      ReportException('map_area_portal_open', E);
  end;
end;

function core_map_areas_connected(area1, area2: cint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_AreasConnected(area1, area2);
  except
    on E: Exception do
      ReportException('map_areas_connected', E);
  end;
end;

function core_map_write_area_bits(area: cint32; buffer: PByte; buffer_size: cint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_WriteAreaBits(area, buffer, buffer_size);
  except
    on E: Exception do
      ReportException('map_write_area_bits', E);
  end;
end;

function core_map_box_leafs(mins, maxs: pcfloat; list: pcint32; list_size, headnode: cint32;
  topnode: pcint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_BoxLeafs(mins, maxs, list, list_size, headnode, topnode);
  except
    on E: Exception do
      ReportException('map_box_leafs', E);
  end;
end;

function core_map_headnode_visible(headnode: cint32; visbits: PByte; visbits_size: cint32): cint32; cdecl;
begin
  Result := 0;
  try
    Result := CM_HeadnodeVisible(headnode, visbits, visbits_size);
  except
    on E: Exception do
      ReportException('map_headnode_visible', E);
  end;
end;

function core_map_load_mesh(desc: PRealmsMeshDesc): cint32; cdecl;
var
  name, err: AnsiString;
begin
  Result := 0;
  try
    MaskFloatingPointExceptions;
    if CM_LoadMesh(desc, err) then
      Exit(1);
    name := '?';
    if (desc <> nil) and (desc^.name <> nil) then
      name := StrOf(desc^.name);
    Com_Warning('map_load_mesh: ' + name + ': ' + err);
  except
    on E: Exception do
    begin
      CM_UnloadMap;
      ReportException('map_load_mesh', E);
    end;
  end;
end;

{ ------------------------------------------------------------ entry point }

procedure FillApi;
begin
  if Api.struct_size <> 0 then
    Exit;
  FillChar(Api, SizeOf(Api), 0);
  Api.struct_size := SizeOf(Api);
  Api.version := REALMS_BRIDGE_VERSION;
  Api.backend_name := @core_backend_name;
  Api.init := @core_init;
  Api.shutdown := @core_shutdown;
  Api.frame := @core_frame;
  Api.cvar_register := @core_cvar_register;
  Api.cvar_set := @core_cvar_set;
  Api.cvar_force_set := @core_cvar_force_set;
  Api.cvar_get := @core_cvar_get;
  Api.cvar_value := @core_cvar_value;
  Api.cvar_flags := @core_cvar_flags;
  Api.cvar_modified := @core_cvar_modified;
  Api.cvar_foreach := @core_cvar_foreach;
  Api.cmd_register := @core_cmd_register;
  Api.cmd_unregister := @core_cmd_unregister;
  Api.cmd_exists := @core_cmd_exists;
  Api.cbuf_add_text := @core_cbuf_add_text;
  Api.cbuf_insert_text := @core_cbuf_insert_text;
  Api.cbuf_execute := @core_cbuf_execute;
  Api.cmd_execute_string := @core_cmd_execute_string;
  Api.fs_add_pak := @core_fs_add_pak;
  Api.fs_load := @core_fs_load;
  Api.fs_free := @core_fs_free;
  Api.fs_pak_count := @core_fs_pak_count;
  Api.map_load := @core_map_load;
  Api.map_info := @core_map_info;
  Api.map_entities := @core_map_entities;
  Api.map_model := @core_map_model;
  Api.map_headnode_for_box := @core_map_headnode_for_box;
  Api.map_point_contents := @core_map_point_contents;
  Api.map_box_trace := @core_map_box_trace;
  Api.map_point_leaf := @core_map_point_leaf;
  Api.map_leaf_info := @core_map_leaf_info;
  Api.map_cluster_pvs := @core_map_cluster_pvs;
  Api.map_cluster_phs := @core_map_cluster_phs;
  Api.map_set_area_portal := @core_map_set_area_portal;
  Api.map_area_portal_open := @core_map_area_portal_open;
  Api.map_areas_connected := @core_map_areas_connected;
  Api.map_write_area_bits := @core_map_write_area_bits;
  Api.map_box_leafs := @core_map_box_leafs;
  Api.map_headnode_visible := @core_map_headnode_visible;
  Api.map_load_mesh := @core_map_load_mesh;
end;

function realms_pascal_get_api(requested_version: cuint32): PRealmsCoreApi; cdecl;
begin
  { The table is filled lazily so static (iOS) links don't depend on the
    library's main block having run. }
  if requested_version <> REALMS_BRIDGE_VERSION then
    Exit(nil);
  FillApi;
  Result := @Api;
end;
