{ Collision model, from qcommon/cmodel.c via game/qcommon/CModel.pas: IBSP v38
  maps, traces, contents, leafs, PVS/PHS, area portals, and glTF meshes as
  slab brushes in a kd-tree. Bad maps are rejected and leave no map loaded.
  Keep in step with realms/src/NativeCModel.cpp. }
unit rlm_cmodel;

{$mode objfpc}{$H+}

interface

uses
  ctypes, rlm_bridge;

{ data/size is the whole .bsp file; name is reported by CM_MapInfo. }
function CM_LoadMap(const name: AnsiString; data: PByte; size: SizeInt;
  out error: AnsiString): Boolean;
procedure CM_UnloadMap;
function CM_Loaded: Boolean;

function CM_MapInfo(info: PRealmsMapInfo): cint32;
function CM_Entities(buffer: PChar; bufferSize: cint32): cint32;
function CM_Model(index: cint32; mins, maxs, origin: pcfloat; headnode: pcint32): cint32;
function CM_HeadnodeForBox(mins, maxs: pcfloat): cint32;
function CM_PointContents(p: pcfloat; headnode: cint32; origin, angles: pcfloat): cint32;
procedure CM_BoxTrace(start, end_, mins, maxs: pcfloat; headnode, brushmask: cint32;
  origin, angles: pcfloat; out_trace: PRealmsTrace);
function CM_PointLeaf(p: pcfloat): cint32;
function CM_LeafInfo(leaf: cint32; contents, cluster, area: pcint32): cint32;

{ Bridge version 3. }
function CM_LoadMesh(desc: PRealmsMeshDesc; out error: AnsiString): Boolean;
function CM_ClusterPVS(cluster: cint32; buffer: PByte; size: cint32): cint32;
function CM_ClusterPHS(cluster: cint32; buffer: PByte; size: cint32): cint32;
procedure CM_SetAreaPortal(portal, open: cint32);
function CM_AreaPortalOpen(portal: cint32): cint32;
function CM_AreasConnected(area1, area2: cint32): cint32;
function CM_WriteAreaBits(area: cint32; buffer: PByte; size: cint32): cint32;
function CM_BoxLeafs(mins, maxs: pcfloat; list: pcint32; listSize, headnode: cint32;
  topnode: pcint32): cint32;
function CM_HeadnodeVisible(headnode: cint32; visbits: PByte; size: cint32): cint32;

{ CRC-32 (IEEE 802.3, the zlib polynomial). }
function CRC32_Block(data: PByte; size: SizeInt): cuint32;

implementation

uses
  SysUtils;

const
  BSP_IDENT = Ord('I') or (Ord('B') shl 8) or (Ord('S') shl 16) or (Ord('P') shl 24);
  BSP_VERSION = 38;
  NUM_LUMPS = 19;
  HEADER_SIZE = 8 + NUM_LUMPS * 8;

  LUMP_ENTITIES = 0;
  LUMP_PLANES = 1;
  LUMP_VISIBILITY = 3;
  LUMP_NODES = 4;
  LUMP_TEXINFO = 5;
  LUMP_LEAFS = 8;
  LUMP_LEAFBRUSHES = 10;
  LUMP_MODELS = 13;
  LUMP_BRUSHES = 14;
  LUMP_BRUSHSIDES = 15;
  LUMP_AREAS = 17;
  LUMP_AREAPORTALS = 18;

  { qfiles.h MAX_MAP_* limits }
  MAX_MAP_MODELS = 1024;
  MAX_MAP_BRUSHES = 8192;
  MAX_MAP_ENTSTRING = $40000;
  MAX_MAP_TEXINFO = 8192;
  MAX_MAP_AREAS = 256;
  MAX_MAP_AREAPORTALS = 1024;
  MAX_MAP_PLANES = 65536;
  MAX_MAP_NODES = 65536;
  MAX_MAP_BRUSHSIDES = 65536;
  MAX_MAP_LEAFS = 65536;
  MAX_MAP_LEAFBRUSHES = 65536;
  MAX_MAP_VISIBILITY = $100000;
  { keeps the recursive traces' stack use bounded for hostile files }
  MAX_TREE_DEPTH = 1024;
  MAX_BOX_LEAFS = 1024;

  DIST_EPSILON: Single = 0.03125;
  ONE: Single = 1.0;

type
  TVec3 = array[0..2] of Single;

  TPlane = record
    normal: TVec3;
    dist: Single;
    ptype: LongInt;
  end;

  TNode = record
    plane: LongInt;
    children: array[0..1] of LongInt;
  end;

  TSurface = record
    name: array[0..31] of AnsiChar;
    flags: LongInt;
    value: LongInt;
  end;

  TLeaf = record
    contents, cluster, area, firstLeafBrush, numLeafBrushes: LongInt;
  end;

  PBrush = ^TBrush;
  TBrush = record
    contents, firstSide, numSides: LongInt;
    checkcount: LongInt;
  end;

  TBrushSide = record
    plane: LongInt;
    surface: LongInt; { -1: null surface }
  end;

  TModel = record
    mins, maxs, origin: TVec3;
    headnode: LongInt;
  end;

  TArea = record
    numPortals, firstPortal: LongInt;
    floodnum, floodvalid: LongInt;
  end;

  TAreaPortal = record
    portalnum, otherArea: LongInt;
  end;

  TByteArr = array of Byte;
  TIntArr = array of LongInt;

  TMapState = record
    loaded: Boolean;
    name: AnsiString;
    checksum: cuint32;
    planes: array of TPlane;
    nodes: array of TNode;
    surfaces: array of TSurface;
    leafs: array of TLeaf;
    leafBrushes: array of LongInt;
    brushes: array of TBrush;
    sides: array of TBrushSide;
    models: array of TModel;
    entities: AnsiString;
    visibility: TByteArr;     { compressed PVS / PHS rows (dvis_t) }
    visOffsets: TIntArr;      { bitofs[cluster][2], flattened }
    areas: array of TArea;
    areaPortals: array of TAreaPortal;
    portalOpen: array[0..MAX_MAP_AREAPORTALS - 1] of Byte;
    floodValid: LongInt;
    meshMap: Boolean;         { triangle mesh map: point contents test leaf brushes }
    { counts as stored in the file (the arrays also hold the box hull) }
    numPlanes, numNodes, numLeafs, numLeafBrushes, numBrushes, numSides: LongInt;
    numClusters, numAreas, numAreaPortals: LongInt;
    emptyLeaf: LongInt;
    boxHeadnode, boxPlanes: LongInt;
    checkcount: LongInt;
  end;

  TLumpView = record
    data: PByte;
    len: LongInt;
  end;

  TBrushProc = procedure(var brush: TBrush);

var
  Map: TMapState;

procedure ClearMap(var m: TMapState);
begin
  m.loaded := False;
  m.name := '';
  m.checksum := 0;
  SetLength(m.planes, 0);
  SetLength(m.nodes, 0);
  SetLength(m.surfaces, 0);
  SetLength(m.leafs, 0);
  SetLength(m.leafBrushes, 0);
  SetLength(m.brushes, 0);
  SetLength(m.sides, 0);
  SetLength(m.models, 0);
  m.entities := '';
  SetLength(m.visibility, 0);
  SetLength(m.visOffsets, 0);
  SetLength(m.areas, 0);
  SetLength(m.areaPortals, 0);
  FillChar(m.portalOpen, SizeOf(m.portalOpen), 0);
  m.floodValid := 0;
  m.meshMap := False;
  m.numPlanes := 0;
  m.numNodes := 0;
  m.numLeafs := 0;
  m.numLeafBrushes := 0;
  m.numBrushes := 0;
  m.numSides := 0;
  m.numClusters := 0;
  m.numAreas := 0;
  m.numAreaPortals := 0;
  m.emptyLeaf := 0;
  m.boxHeadnode := 0;
  m.boxPlanes := 0;
  m.checkcount := 0;
end;

{ ---------------------------------------------------------------- helpers }

function RD32(p: PByte): LongInt;
begin
  Move(p^, Result, 4);
end;

function RD16(p: PByte): LongInt;
var
  v: SmallInt;
begin
  Move(p^, v, 2);
  Result := v;
end;

function RDU16(p: PByte): LongInt;
var
  v: Word;
begin
  Move(p^, v, 2);
  Result := v;
end;

function RDF(p: PByte): Single;
begin
  Move(p^, Result, 4);
end;

function Dot(const a, b: TVec3): Single;
begin
  Result := a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
end;

procedure AngleVectors(angles: pcfloat; out forward, right, up: TVec3);
var
  k, a, sy, cy, sp, cp, sr, cr: Single;
begin
  k := Pi * 2.0 / 360.0;
  a := angles[1] * k;
  sy := Sin(a);
  cy := Cos(a);
  a := angles[0] * k;
  sp := Sin(a);
  cp := Cos(a);
  a := angles[2] * k;
  sr := Sin(a);
  cr := Cos(a);
  forward[0] := cp * cy;
  forward[1] := cp * sy;
  forward[2] := -sp;
  right[0] := -sr * sp * cy + cr * sy;
  right[1] := -sr * sp * sy - cr * cy;
  right[2] := -sr * cp;
  up[0] := cr * sp * cy + sr * sy;
  up[1] := cr * sp * sy - sr * cy;
  up[2] := cr * cp;
end;

procedure RotateInto(var v: TVec3; const f, r, u: TVec3);
var
  t: TVec3;
begin
  t := v;
  v[0] := Dot(t, f);
  v[1] := -Dot(t, r);
  v[2] := Dot(t, u);
end;

function AnyNonZero(v: pcfloat): Boolean;
begin
  Result := (v <> nil) and ((v[0] <> 0) or (v[1] <> 0) or (v[2] <> 0));
end;

function Vec(p: pcfloat): TVec3;
begin
  Result[0] := p[0];
  Result[1] := p[1];
  Result[2] := p[2];
end;

{ ---------------------------------------------------------------- loading }

var
  LData: PByte;
  LSize: SizeInt;
  LError: AnsiString;
  Lumps: array[0..NUM_LUMPS - 1] of TLumpView;

function Fail(const msg: AnsiString): Boolean;
begin
  LError := msg;
  Result := False;
end;

function ReadHeader: Boolean;
var
  i: Integer;
  version, ofs, len: LongInt;
begin
  if (LData = nil) or (LSize < HEADER_SIZE) then
    Exit(Fail('file is too small to be a BSP'));
  if RD32(LData) <> BSP_IDENT then
    Exit(Fail('not an IBSP file'));
  version := RD32(LData + 4);
  if version <> BSP_VERSION then
    Exit(Fail('wrong version number (' + IntToStr(version) + ' should be 38)'));
  for i := 0 to NUM_LUMPS - 1 do
  begin
    ofs := RD32(LData + 8 + i * 8);
    len := RD32(LData + 12 + i * 8);
    if (ofs < 0) or (len < 0) or (Int64(ofs) + Int64(len) > Int64(LSize)) then
      Exit(Fail('lump ' + IntToStr(i) + ' is out of range'));
    Lumps[i].data := LData + ofs;
    Lumps[i].len := len;
  end;
  Result := True;
end;

{ Element count of a lump, enforcing whole records and the Quake 2 limit. }
function CountLump(lump, elemSize, maxCount, minCount: LongInt; const what: AnsiString;
  out n: LongInt): Boolean;
var
  len: LongInt;
begin
  n := 0;
  len := Lumps[lump].len;
  if len mod elemSize <> 0 then
    Exit(Fail('funny lump size (' + what + ')'));
  n := len div elemSize;
  if n > maxCount then
    Exit(Fail('too many ' + what));
  if n < minCount then
    Exit(Fail('map has no ' + what));
  Result := True;
end;

function LumpAt(lump, index, elemSize: LongInt): PByte;
begin
  Result := Lumps[lump].data + SizeInt(index) * SizeInt(elemSize);
end;

function ValidChild(const m: TMapState; c: LongInt): Boolean;
begin
  if c >= 0 then
    Result := c < m.numNodes
  else
    Result := -1 - Int64(c) < Int64(m.numLeafs);
end;

{ Rejects node graphs with cycles (they would never terminate) or deeper than
  MAX_TREE_DEPTH. Iterative DFS; leafs have depth 0. }
function CheckTree(const m: TMapState): Boolean;
var
  state: array of Byte; { 0 new, 1 on stack, 2 done }
  depth: array of LongInt;
  stackNode, stackNext: array of LongInt;
  sp, root, node, c, d, k: LongInt;
begin
  SetLength(state, m.numNodes);
  SetLength(depth, m.numNodes);
  SetLength(stackNode, m.numNodes);
  SetLength(stackNext, m.numNodes);
  for k := 0 to m.numNodes - 1 do
  begin
    state[k] := 0;
    depth[k] := 0;
  end;
  for root := 0 to m.numNodes - 1 do
  begin
    if state[root] <> 0 then
      Continue;
    state[root] := 1;
    sp := 0;
    stackNode[0] := root;
    stackNext[0] := 0;
    while sp >= 0 do
    begin
      node := stackNode[sp];
      if stackNext[sp] < 2 then
      begin
        c := m.nodes[node].children[stackNext[sp]];
        Inc(stackNext[sp]);
        if c < 0 then
          Continue;
        if state[c] = 1 then
          Exit(Fail('BSP node graph has a cycle'));
        if state[c] = 0 then
        begin
          state[c] := 1;
          Inc(sp);
          stackNode[sp] := c;
          stackNext[sp] := 0;
        end;
        Continue;
      end;
      d := 0;
      for k := 0 to 1 do
      begin
        c := m.nodes[node].children[k];
        if (c >= 0) and (depth[c] > d) then
          d := depth[c];
      end;
      depth[node] := d + 1;
      if depth[node] > MAX_TREE_DEPTH then
        Exit(Fail('BSP tree is too deep'));
      state[node] := 2;
      Dec(sp);
    end;
  end;
  Result := True;
end;

{ CM_InitBoxHull: six nodes/planes and one brush/leaf after the map's own. }
procedure InitBoxHull(var m: TMapState);
var
  i, side: LongInt;
begin
  m.boxHeadnode := m.numNodes;
  m.boxPlanes := m.numPlanes;

  SetLength(m.brushes, m.numBrushes + 1);
  m.brushes[m.numBrushes].contents := REALMS_CONTENTS_MONSTER;
  m.brushes[m.numBrushes].firstSide := m.numSides;
  m.brushes[m.numBrushes].numSides := 6;
  m.brushes[m.numBrushes].checkcount := 0;

  SetLength(m.leafs, m.numLeafs + 1);
  m.leafs[m.numLeafs].contents := REALMS_CONTENTS_MONSTER;
  m.leafs[m.numLeafs].cluster := 0;
  m.leafs[m.numLeafs].area := 0;
  m.leafs[m.numLeafs].firstLeafBrush := m.numLeafBrushes;
  m.leafs[m.numLeafs].numLeafBrushes := 1;

  SetLength(m.leafBrushes, m.numLeafBrushes + 1);
  m.leafBrushes[m.numLeafBrushes] := m.numBrushes;

  SetLength(m.planes, m.numPlanes + 12);
  SetLength(m.sides, m.numSides + 6);
  SetLength(m.nodes, m.numNodes + 6);
  for i := 0 to 5 do
  begin
    side := i and 1;
    m.sides[m.numSides + i].plane := m.numPlanes + i * 2 + side;
    m.sides[m.numSides + i].surface := -1;

    m.nodes[m.numNodes + i].plane := m.numPlanes + i * 2;
    m.nodes[m.numNodes + i].children[side] := -1 - m.emptyLeaf;
    if i <> 5 then
      m.nodes[m.numNodes + i].children[side xor 1] := m.boxHeadnode + i + 1
    else
      m.nodes[m.numNodes + i].children[side xor 1] := -1 - m.numLeafs;

    with m.planes[m.numPlanes + i * 2] do
    begin
      normal[0] := 0;
      normal[1] := 0;
      normal[2] := 0;
      normal[i shr 1] := 1;
      dist := 0;
      ptype := i shr 1;
    end;
    with m.planes[m.numPlanes + i * 2 + 1] do
    begin
      normal[0] := 0;
      normal[1] := 0;
      normal[2] := 0;
      normal[i shr 1] := -1;
      dist := 0;
      ptype := 3 + (i shr 1);
    end;
  end;
end;

{ FloodAreaConnections. Area 0 is the solid / unused area. }
procedure FloodArea(var m: TMapState; area, floodnum: LongInt);
var
  i, pidx: LongInt;
begin
  if m.areas[area].floodvalid = m.floodValid then
    Exit; { already flooded (Quake 2 errors on "reflooded") }
  m.areas[area].floodnum := floodnum;
  m.areas[area].floodvalid := m.floodValid;
  for i := 0 to m.areas[area].numPortals - 1 do
  begin
    pidx := m.areas[area].firstPortal + i;
    if m.portalOpen[m.areaPortals[pidx].portalnum] <> 0 then
      FloodArea(m, m.areaPortals[pidx].otherArea, floodnum);
  end;
end;

procedure FloodAreaConnections(var m: TMapState);
var
  i, floodnum: LongInt;
begin
  Inc(m.floodValid);
  floodnum := 0;
  for i := 1 to m.numAreas - 1 do
  begin
    if m.areas[i].floodvalid = m.floodValid then
      Continue;
    Inc(floodnum);
    FloodArea(m, i, floodnum);
  end;
end;

function LoadInto(var m: TMapState): Boolean;
var
  n, i, j, numSurfaces, num, first, portal, other, areaLimit, entLen, used: LongInt;
  visLen, visClusters, ofs: LongInt;
  p, ent, vis: PByte;
begin
  Result := False;
  if not ReadHeader then
    Exit;

  { Surfaces (texinfo). }
  if not CountLump(LUMP_TEXINFO, 76, MAX_MAP_TEXINFO, 1, 'surfaces', n) then
    Exit;
  SetLength(m.surfaces, n);
  for i := 0 to n - 1 do
  begin
    p := LumpAt(LUMP_TEXINFO, i, 76);
    FillChar(m.surfaces[i], SizeOf(TSurface), 0);
    m.surfaces[i].flags := RD32(p + 32);
    m.surfaces[i].value := RD32(p + 36);
    j := 0;
    while (j < 31) and (p[40 + j] <> 0) do
    begin
      m.surfaces[i].name[j] := AnsiChar(p[40 + j]);
      Inc(j);
    end;
  end;

  { Leafs. }
  if not CountLump(LUMP_LEAFS, 28, MAX_MAP_LEAFS, 1, 'leafs', m.numLeafs) then
    Exit;
  SetLength(m.leafs, m.numLeafs);
  m.numClusters := 0;
  for i := 0 to m.numLeafs - 1 do
  begin
    p := LumpAt(LUMP_LEAFS, i, 28);
    m.leafs[i].contents := RD32(p);
    m.leafs[i].cluster := RD16(p + 4);
    m.leafs[i].area := RD16(p + 6);
    m.leafs[i].firstLeafBrush := RDU16(p + 24);
    m.leafs[i].numLeafBrushes := RDU16(p + 26);
    if m.leafs[i].cluster < -1 then
      Exit(Fail('bad leaf cluster'));
    if m.leafs[i].cluster >= m.numClusters then
      m.numClusters := m.leafs[i].cluster + 1;
  end;
  if m.leafs[0].contents <> REALMS_CONTENTS_SOLID then
    Exit(Fail('leaf 0 is not CONTENTS_SOLID'));
  m.emptyLeaf := -1;
  for i := 1 to m.numLeafs - 1 do
    if m.leafs[i].contents = 0 then
    begin
      m.emptyLeaf := i;
      Break;
    end;
  if m.emptyLeaf < 0 then
    Exit(Fail('map does not have an empty leaf'));

  { Leaf brushes. }
  if not CountLump(LUMP_LEAFBRUSHES, 2, MAX_MAP_LEAFBRUSHES, 1, 'leaf brushes', m.numLeafBrushes) then
    Exit;
  SetLength(m.leafBrushes, m.numLeafBrushes);
  for i := 0 to m.numLeafBrushes - 1 do
    m.leafBrushes[i] := RDU16(LumpAt(LUMP_LEAFBRUSHES, i, 2));

  { Planes. }
  if not CountLump(LUMP_PLANES, 20, MAX_MAP_PLANES, 1, 'planes', m.numPlanes) then
    Exit;
  SetLength(m.planes, m.numPlanes);
  for i := 0 to m.numPlanes - 1 do
  begin
    p := LumpAt(LUMP_PLANES, i, 20);
    for j := 0 to 2 do
      m.planes[i].normal[j] := RDF(p + 4 * j);
    m.planes[i].dist := RDF(p + 12);
    m.planes[i].ptype := RD32(p + 16);
    if (m.planes[i].ptype < 0) or (m.planes[i].ptype > 5) then
      Exit(Fail('bad plane type'));
  end;

  { Brushes. }
  if not CountLump(LUMP_BRUSHES, 12, MAX_MAP_BRUSHES, 0, 'brushes', m.numBrushes) then
    Exit;
  SetLength(m.brushes, m.numBrushes);
  for i := 0 to m.numBrushes - 1 do
  begin
    p := LumpAt(LUMP_BRUSHES, i, 12);
    m.brushes[i].firstSide := RD32(p);
    m.brushes[i].numSides := RD32(p + 4);
    m.brushes[i].contents := RD32(p + 8);
    m.brushes[i].checkcount := 0;
  end;

  { Brush sides. }
  if not CountLump(LUMP_BRUSHSIDES, 4, MAX_MAP_BRUSHSIDES, 0, 'brush sides', m.numSides) then
    Exit;
  SetLength(m.sides, m.numSides);
  numSurfaces := Length(m.surfaces);
  for i := 0 to m.numSides - 1 do
  begin
    p := LumpAt(LUMP_BRUSHSIDES, i, 4);
    m.sides[i].plane := RDU16(p);
    m.sides[i].surface := RD16(p + 2);
    if m.sides[i].plane >= m.numPlanes then
      Exit(Fail('bad brush side plane'));
    if (m.sides[i].surface < -1) or (m.sides[i].surface >= numSurfaces) then
      Exit(Fail('bad brush side texinfo'));
  end;
  for i := 0 to m.numBrushes - 1 do
    with m.brushes[i] do
      if (firstSide < 0) or (numSides < 0) or (Int64(firstSide) + numSides > m.numSides) then
        Exit(Fail('bad brush sides'));
  for i := 0 to m.numLeafBrushes - 1 do
    if m.leafBrushes[i] >= m.numBrushes then
      Exit(Fail('bad leaf brush'));
  for i := 0 to m.numLeafs - 1 do
    if m.leafs[i].firstLeafBrush + m.leafs[i].numLeafBrushes > m.numLeafBrushes then
      Exit(Fail('bad leaf brush range'));

  { Inline models. }
  if not CountLump(LUMP_MODELS, 48, MAX_MAP_MODELS, 1, 'models', n) then
    Exit;
  SetLength(m.models, n);
  for i := 0 to n - 1 do
  begin
    p := LumpAt(LUMP_MODELS, i, 48);
    for j := 0 to 2 do
    begin
      { spread the mins / maxs by a pixel }
      m.models[i].mins[j] := RDF(p + 4 * j) - 1;
      m.models[i].maxs[j] := RDF(p + 12 + 4 * j) + 1;
      m.models[i].origin[j] := RDF(p + 24 + 4 * j);
    end;
    m.models[i].headnode := RD32(p + 36);
  end;

  { Nodes. }
  if not CountLump(LUMP_NODES, 28, MAX_MAP_NODES, 1, 'nodes', m.numNodes) then
    Exit;
  SetLength(m.nodes, m.numNodes);
  for i := 0 to m.numNodes - 1 do
  begin
    p := LumpAt(LUMP_NODES, i, 28);
    m.nodes[i].plane := RD32(p);
    m.nodes[i].children[0] := RD32(p + 4);
    m.nodes[i].children[1] := RD32(p + 8);
    if (m.nodes[i].plane < 0) or (m.nodes[i].plane >= m.numPlanes) then
      Exit(Fail('bad node plane'));
  end;
  for i := 0 to m.numNodes - 1 do
    if not ValidChild(m, m.nodes[i].children[0]) or not ValidChild(m, m.nodes[i].children[1]) then
      Exit(Fail('bad node child'));
  for i := 0 to Length(m.models) - 1 do
    if not ValidChild(m, m.models[i].headnode) then
      Exit(Fail('bad model headnode'));
  if not CheckTree(m) then
    Exit;

  { Areas and area portals. }
  if not CountLump(LUMP_AREAS, 8, MAX_MAP_AREAS, 0, 'areas', m.numAreas) then
    Exit;
  if not CountLump(LUMP_AREAPORTALS, 8, MAX_MAP_AREAPORTALS, 0, 'area portals', m.numAreaPortals) then
    Exit;
  SetLength(m.areas, m.numAreas);
  for i := 0 to m.numAreas - 1 do
  begin
    p := LumpAt(LUMP_AREAS, i, 8);
    num := RD32(p);
    first := RD32(p + 4);
    if (num < 0) or (first < 0) or (Int64(first) + num > m.numAreaPortals) then
      Exit(Fail('bad area portals'));
    m.areas[i].numPortals := num;
    m.areas[i].firstPortal := first;
    m.areas[i].floodnum := 0;
    m.areas[i].floodvalid := 0;
  end;
  SetLength(m.areaPortals, m.numAreaPortals);
  for i := 0 to m.numAreaPortals - 1 do
  begin
    p := LumpAt(LUMP_AREAPORTALS, i, 8);
    portal := RD32(p);
    other := RD32(p + 4);
    if (portal < 0) or (portal >= MAX_MAP_AREAPORTALS) or (other < 0) or (other >= m.numAreas) then
      Exit(Fail('bad area portal'));
    m.areaPortals[i].portalnum := portal;
    m.areaPortals[i].otherArea := other;
  end;
  if m.numAreas > 0 then
    areaLimit := m.numAreas
  else
    areaLimit := 1;
  for i := 0 to m.numLeafs - 1 do
    if (m.leafs[i].area < 0) or (m.leafs[i].area >= areaLimit) then
      Exit(Fail('bad leaf area'));

  { Visibility: dvis_t header (numclusters, bitofs[numclusters][2]) and the
    run-length compressed rows. Rows are decompressed with bounds checks. }
  visLen := Lumps[LUMP_VISIBILITY].len;
  if visLen > MAX_MAP_VISIBILITY then
    Exit(Fail('visibility lump is too large'));
  if visLen > 0 then
  begin
    vis := Lumps[LUMP_VISIBILITY].data;
    if visLen < 4 then
      Exit(Fail('visibility lump is truncated'));
    visClusters := RD32(vis);
    if (visClusters < 0) or (visClusters > MAX_MAP_LEAFS) or (4 + Int64(visClusters) * 8 > visLen) then
      Exit(Fail('bad visibility cluster count'));
    SetLength(m.visOffsets, visClusters * 2);
    for i := 0 to visClusters * 2 - 1 do
    begin
      ofs := RD32(vis + 4 + 4 * i);
      if (ofs < 0) or (ofs > visLen) then
        Exit(Fail('bad visibility offset'));
      m.visOffsets[i] := ofs;
    end;
    if m.numClusters > visClusters then
      Exit(Fail('leaf cluster out of range for visibility'));
    m.numClusters := visClusters;
    SetLength(m.visibility, visLen);
    Move(vis^, m.visibility[0], visLen);
  end;

  { Entity string, up to the first NUL. }
  entLen := Lumps[LUMP_ENTITIES].len;
  if entLen > MAX_MAP_ENTSTRING then
    Exit(Fail('entity lump is too large'));
  ent := Lumps[LUMP_ENTITIES].data;
  used := 0;
  while (used < entLen) and (ent[used] <> 0) do
    Inc(used);
  SetString(m.entities, PAnsiChar(ent), used);

  InitBoxHull(m);
  FloodAreaConnections(m);
  Result := True;
end;

{ -------------------------------------------------------- triangle meshes }

const
  MAX_MESH_VERTICES = 1 shl 24;
  MAX_MESH_TRIANGLES = 1 shl 20;
  MAX_MESH_SURFACES = 65536;
  MESH_LEAF_BRUSHES = 8;
  MESH_MAX_DEPTH = 40;
  MESH_EDGES: array[0..8, 0..1] of Integer =
    ((0, 1), (1, 2), (2, 0), (3, 4), (4, 5), (5, 3), (0, 3), (1, 4), (2, 5));

  MESH_MAX_COORD: Single = 1e6;
  MESH_NORM_EPS: Single = 1e-6;
  MESH_DUP_DOT: Single = 0.99999;
  MESH_DUP_DIST: Single = 0.01;
  MESH_BEHIND_EPS: Single = 0.01;
  MESH_SPLIT_EPS: Single = 0.001;
  MESH_HALF: Single = 0.5;
  MESH_BIG: Single = 1e30;

type
  TMeshBrush = record
    mins, maxs, center: TVec3;
  end;

var
  { builder state: element counts of the growing map arrays }
  MPlanes, MNodes, MLeafs, MLeafBrushes, MBrushes, MSides: LongInt;
  MBounds: array of TMeshBrush;
  MBoundsCount: LongInt;

function FiniteF(x: Single): Boolean;
var
  bits: LongWord;
begin
  Move(x, bits, 4);
  Result := (bits and $7F800000) <> $7F800000;
end;

function VSub(const a, b: TVec3): TVec3;
begin
  Result[0] := a[0] - b[0];
  Result[1] := a[1] - b[1];
  Result[2] := a[2] - b[2];
end;

function VCross(const a, b: TVec3): TVec3;
begin
  Result[0] := a[1] * b[2] - a[2] * b[1];
  Result[1] := a[2] * b[0] - a[0] * b[2];
  Result[2] := a[0] * b[1] - a[1] * b[0];
end;

function VNormalize(var a: TVec3): Boolean;
var
  len: Single;
begin
  len := Sqrt(Dot(a, a));
  if not (len > MESH_NORM_EPS) then
    Exit(False);
  a[0] := a[0] / len;
  a[1] := a[1] / len;
  a[2] := a[2] / len;
  Result := True;
end;

{ Plane type as Quake 2 stores it: exact positive axes are axial (0..2) and
  everything else is 3 + the dominant axis. }
function MeshPlaneType(const n: TVec3): LongInt;
var
  a, major: Integer;
begin
  for a := 0 to 2 do
    if (n[a] = 1) and (n[(a + 1) mod 3] = 0) and (n[(a + 2) mod 3] = 0) then
      Exit(a);
  major := 0;
  for a := 1 to 2 do
    if Abs(n[a]) > Abs(n[major]) then
      major := a;
  Result := 3 + major;
end;

function MeshAddPlane(var m: TMapState; const n: TVec3; dist: Single): LongInt;
begin
  if MPlanes = Length(m.planes) then
    SetLength(m.planes, MPlanes * 2 + 256);
  m.planes[MPlanes].normal := n;
  m.planes[MPlanes].dist := dist;
  m.planes[MPlanes].ptype := MeshPlaneType(n);
  Result := MPlanes;
  Inc(MPlanes);
end;

{ Triangle (a, b, c), counter-clockwise seen from the front, becomes a slab
  brush: front face, back face thick units behind, three edge planes, plus
  Quake 2 axial and edge bevels so box traces stay tight. }
function MeshAddTriangle(var m: TMapState; const a, b, c: TVec3; thick: Single;
  surface, contents: LongInt): Boolean;
var
  n, back, en, dir, ax, bn, pos, neg: TVec3;
  pts: array[0..5] of TVec3;
  normals: array[0..71] of TVec3;
  dists: array[0..71] of Single;
  count, i, j, e, axis, sign, k: Integer;
  d: Single;
  behind: Boolean;
  mb: TMeshBrush;

  procedure AddSide(const pn: TVec3; dd: Single);
  var
    q: Integer;
  begin
    for q := 0 to count - 1 do { skip duplicates }
      if (Dot(normals[q], pn) > MESH_DUP_DOT) and (Abs(dists[q] - dd) < MESH_DUP_DIST) then
        Exit;
    normals[count] := pn;
    dists[count] := dd;
    Inc(count);
  end;

begin
  Result := False;
  count := 0;
  n := VCross(VSub(b, a), VSub(c, a));
  if not VNormalize(n) then
    Exit; { degenerate }
  pts[0] := a;
  pts[1] := b;
  pts[2] := c;
  for k := 0 to 2 do
    for j := 0 to 2 do
      pts[3 + k][j] := pts[k][j] - n[j] * thick;
  AddSide(n, Dot(n, a));
  for j := 0 to 2 do
    back[j] := -n[j];
  AddSide(back, Dot(back, pts[3]));
  for e := 0 to 2 do
  begin
    en := VCross(VSub(pts[(e + 1) mod 3], pts[e]), n);
    if not VNormalize(en) then
      Exit;
    AddSide(en, Dot(en, pts[e]));
  end;
  { axial bevels: the brush bounds }
  for i := 0 to 2 do
  begin
    mb.mins[i] := pts[0][i];
    mb.maxs[i] := pts[0][i];
    for k := 0 to 5 do
    begin
      if pts[k][i] < mb.mins[i] then
        mb.mins[i] := pts[k][i];
      if pts[k][i] > mb.maxs[i] then
        mb.maxs[i] := pts[k][i];
    end;
    mb.center[i] := (mb.mins[i] + mb.maxs[i]) * MESH_HALF;
  end;
  for i := 0 to 2 do
  begin
    for j := 0 to 2 do
    begin
      pos[j] := 0;
      neg[j] := 0;
    end;
    pos[i] := 1;
    neg[i] := -1;
    AddSide(pos, mb.maxs[i]);
    AddSide(neg, -mb.mins[i]);
  end;
  { edge bevels: edge direction x axis, kept when every vertex is behind }
  for e := 0 to 8 do
  begin
    dir := VSub(pts[MESH_EDGES[e, 1]], pts[MESH_EDGES[e, 0]]);
    for axis := 0 to 2 do
    begin
      sign := -1;
      while sign <= 1 do
      begin
        for j := 0 to 2 do
          ax[j] := 0;
        ax[axis] := sign;
        bn := VCross(dir, ax);
        if VNormalize(bn) then
        begin
          d := Dot(bn, pts[MESH_EDGES[e, 0]]);
          behind := True;
          for k := 0 to 5 do
            if Dot(bn, pts[k]) - d > MESH_BEHIND_EPS then
              behind := False;
          if behind then
            AddSide(bn, d);
        end;
        Inc(sign, 2);
      end;
    end;
  end;

  if MBrushes = Length(m.brushes) then
    SetLength(m.brushes, MBrushes * 2 + 256);
  m.brushes[MBrushes].contents := contents;
  m.brushes[MBrushes].firstSide := MSides;
  m.brushes[MBrushes].numSides := count;
  m.brushes[MBrushes].checkcount := 0;
  Inc(MBrushes);
  for i := 0 to count - 1 do
  begin
    k := MeshAddPlane(m, normals[i], dists[i]);
    if MSides = Length(m.sides) then
      SetLength(m.sides, MSides * 2 + 1024);
    m.sides[MSides].plane := k;
    m.sides[MSides].surface := surface;
    Inc(MSides);
  end;
  if MBoundsCount = Length(MBounds) then
    SetLength(MBounds, MBoundsCount * 2 + 256);
  MBounds[MBoundsCount] := mb;
  Inc(MBoundsCount);
  Result := True;
end;

{ Axial kd-tree; leafs list every brush whose bounds touch them. }
function MeshBuild(var m: TMapState; var ids: TIntArr; depth: Integer): LongInt;
var
  cmin, cmax, nrm: TVec3;
  i, k, n, axis, nf, nb, index, f, b, id: LongInt;
  split: Single;
  front, back: TIntArr;
begin
  n := Length(ids);
  front := nil;
  back := nil;
  for i := 0 to 2 do
  begin
    cmin[i] := MESH_BIG;
    cmax[i] := -MESH_BIG;
  end;
  for k := 0 to n - 1 do
    for i := 0 to 2 do
    begin
      if MBounds[ids[k]].center[i] < cmin[i] then
        cmin[i] := MBounds[ids[k]].center[i];
      if MBounds[ids[k]].center[i] > cmax[i] then
        cmax[i] := MBounds[ids[k]].center[i];
    end;
  axis := 0;
  for i := 1 to 2 do
    if cmax[i] - cmin[i] > cmax[axis] - cmin[axis] then
      axis := i;
  split := (cmin[axis] + cmax[axis]) * MESH_HALF;
  if (n > MESH_LEAF_BRUSHES) and (depth < MESH_MAX_DEPTH) and (cmax[axis] - cmin[axis] > MESH_SPLIT_EPS) then
  begin
    SetLength(front, n);
    SetLength(back, n);
    nf := 0;
    nb := 0;
    for k := 0 to n - 1 do
    begin
      id := ids[k];
      if MBounds[id].maxs[axis] >= split then
      begin
        front[nf] := id;
        Inc(nf);
      end;
      if MBounds[id].mins[axis] <= split then
      begin
        back[nb] := id;
        Inc(nb);
      end;
    end;
    { splits that duplicate too many brushes stop here, and a node budget
      bounds the whole tree }
    if (nf < n) and (nb < n) and (nf + nb <= n + n div 2) and
       (Int64(MNodes) < 4 * Int64(MBoundsCount) + 64) then
    begin
      SetLength(front, nf);
      SetLength(back, nb);
      SetLength(ids, 0);
      if MNodes = Length(m.nodes) then
        SetLength(m.nodes, MNodes * 2 + 256);
      index := MNodes;
      Inc(MNodes);
      for i := 0 to 2 do
        nrm[i] := 0;
      nrm[axis] := 1;
      m.nodes[index].plane := MeshAddPlane(m, nrm, split);
      f := MeshBuild(m, front, depth + 1);
      b := MeshBuild(m, back, depth + 1);
      m.nodes[index].children[0] := f;
      m.nodes[index].children[1] := b;
      Exit(index);
    end;
    SetLength(front, 0);
    SetLength(back, 0);
  end;
  if MLeafs = Length(m.leafs) then
    SetLength(m.leafs, MLeafs * 2 + 256);
  m.leafs[MLeafs].contents := 0;
  m.leafs[MLeafs].cluster := 0;
  m.leafs[MLeafs].area := 1;
  m.leafs[MLeafs].firstLeafBrush := MLeafBrushes;
  m.leafs[MLeafs].numLeafBrushes := n;
  for k := 0 to n - 1 do
  begin
    if MLeafBrushes = Length(m.leafBrushes) then
      SetLength(m.leafBrushes, MLeafBrushes * 2 + 1024);
    m.leafBrushes[MLeafBrushes] := ids[k];
    Inc(MLeafBrushes);
    m.leafs[MLeafs].contents := m.leafs[MLeafs].contents or m.brushes[ids[k]].contents;
  end;
  Result := -1 - MLeafs;
  Inc(MLeafs);
end;

function LoadMeshInto(var m: TMapState; const d: TRealmsMeshDesc): Boolean;
var
  i, j, k, t, surface, contents, nIds, numModels, model, n: LongInt;
  thick: Single;
  v: array[0..2] of TVec3;
  ids: TIntArr;
  mins, maxs, nrm: TVec3;
  world, inl: TModel;
  triModels: pcint32;
  haveWorld, any: Boolean;
  { brush ids per model; inline model bounds from all of its triangles }
  modelIds: array of TIntArr;
  modelCounts: array of LongInt;
  modelMins, modelMaxs: array of TVec3;
begin
  Result := False;
  if (d.num_vertices < 0) or (d.num_vertices > MAX_MESH_VERTICES) then
    Exit(Fail('bad mesh vertex count'));
  if (d.num_triangles < 0) or (d.num_triangles > MAX_MESH_TRIANGLES) then
    Exit(Fail('too many mesh triangles'));
  if (d.num_surfaces < 1) or (d.num_surfaces > MAX_MESH_SURFACES) or (d.surfaces = nil) then
    Exit(Fail('bad mesh surfaces'));
  if ((d.num_vertices > 0) and (d.vertices = nil)) or ((d.num_triangles > 0) and (d.indices = nil)) then
    Exit(Fail('missing mesh data'));
  for i := 0 to d.num_vertices * 3 - 1 do
    if not FiniteF(d.vertices[i]) or (Abs(d.vertices[i]) > MESH_MAX_COORD) then
      Exit(Fail('bad mesh vertex'));
  for i := 0 to d.num_triangles * 3 - 1 do
    if (d.indices[i] < 0) or (d.indices[i] >= d.num_vertices) then
      Exit(Fail('bad mesh index'));
  if d.triangle_surfaces <> nil then
    for i := 0 to d.num_triangles - 1 do
      if (d.triangle_surfaces[i] < 0) or (d.triangle_surfaces[i] >= d.num_surfaces) then
        Exit(Fail('bad mesh triangle surface'));
  if (d.entities <> nil) and (StrLen(d.entities) > MAX_MAP_ENTSTRING) then
    Exit(Fail('entity lump is too large'));
  { inline models: only hosts that pass the longer description }
  numModels := 1;
  triModels := nil;
  if (d.struct_size >= SizeOf(TRealmsMeshDesc)) and (d.num_models > 1) then
  begin
    numModels := d.num_models;
    triModels := d.triangle_models;
  end;
  if (numModels > MAX_MAP_MODELS) or ((numModels > 1) and (triModels = nil) and (d.num_triangles > 0)) then
    Exit(Fail('bad mesh model count'));
  if triModels <> nil then
    for i := 0 to d.num_triangles - 1 do
      if (triModels[i] < 0) or (triModels[i] >= numModels) then
        Exit(Fail('bad mesh triangle model'));
  if FiniteF(d.thickness) and (d.thickness > 0) then
    thick := d.thickness
  else
    thick := 2;

  SetLength(m.surfaces, d.num_surfaces);
  for i := 0 to d.num_surfaces - 1 do
  begin
    FillChar(m.surfaces[i], SizeOf(TSurface), 0);
    j := 0;
    while (j < 31) and (d.surfaces[i].name[j] <> #0) do
    begin
      m.surfaces[i].name[j] := d.surfaces[i].name[j];
      Inc(j);
    end;
    m.surfaces[i].flags := d.surfaces[i].flags;
    m.surfaces[i].value := d.surfaces[i].value;
  end;

  MPlanes := 0;
  MNodes := 0;
  MLeafBrushes := 0;
  MBrushes := 0;
  MSides := 0;
  MBoundsCount := 0;
  { leaf 0: the solid dummy leaf; leaf 1: the empty leaf the box hull uses }
  SetLength(m.leafs, 256);
  m.leafs[0].contents := REALMS_CONTENTS_SOLID;
  m.leafs[0].cluster := -1;
  m.leafs[0].area := 0;
  m.leafs[0].firstLeafBrush := 0;
  m.leafs[0].numLeafBrushes := 0;
  m.leafs[1].contents := 0;
  m.leafs[1].cluster := 0;
  m.leafs[1].area := 1;
  m.leafs[1].firstLeafBrush := 0;
  m.leafs[1].numLeafBrushes := 0;
  MLeafs := 2;
  m.emptyLeaf := 1;

  modelIds := nil;
  modelCounts := nil;
  modelMins := nil;
  modelMaxs := nil;
  SetLength(modelIds, numModels);
  SetLength(modelCounts, numModels);
  SetLength(modelMins, numModels);
  SetLength(modelMaxs, numModels);
  for n := 0 to numModels - 1 do
  begin
    modelIds[n] := nil;
    modelCounts[n] := 0;
    for j := 0 to 2 do
    begin
      modelMins[n][j] := 1e30;
      modelMaxs[n][j] := -1e30;
    end;
  end;
  SetLength(modelIds[0], d.num_triangles);
  for j := 0 to 2 do
  begin
    mins[j] := 0;
    maxs[j] := 0;
  end;
  haveWorld := False;
  for t := 0 to d.num_triangles - 1 do
  begin
    if d.triangle_surfaces <> nil then
      surface := d.triangle_surfaces[t]
    else
      surface := 0;
    contents := d.surfaces[surface].contents;
    if triModels <> nil then
      model := triModels[t]
    else
      model := 0;
    for k := 0 to 2 do
      for j := 0 to 2 do
        v[k][j] := d.vertices[SizeInt(d.indices[t * 3 + k]) * 3 + j];
    if model > 0 then
      for k := 0 to 2 do
        for j := 0 to 2 do
        begin
          if v[k][j] < modelMins[model][j] then
            modelMins[model][j] := v[k][j];
          if v[k][j] > modelMaxs[model][j] then
            modelMaxs[model][j] := v[k][j];
        end;
    if (contents = 0) or not MeshAddTriangle(m, v[0], v[1], v[2], thick, surface, contents) then
      Continue;
    if modelCounts[model] = Length(modelIds[model]) then
      SetLength(modelIds[model], modelCounts[model] * 2 + 64);
    modelIds[model][modelCounts[model]] := MBoundsCount - 1;
    Inc(modelCounts[model]);
    if model <> 0 then
      Continue;
    for j := 0 to 2 do
      if not haveWorld then
      begin
        mins[j] := MBounds[MBoundsCount - 1].mins[j];
        maxs[j] := MBounds[MBoundsCount - 1].maxs[j];
      end
      else
      begin
        if MBounds[MBoundsCount - 1].mins[j] < mins[j] then
          mins[j] := MBounds[MBoundsCount - 1].mins[j];
        if MBounds[MBoundsCount - 1].maxs[j] > maxs[j] then
          maxs[j] := MBounds[MBoundsCount - 1].maxs[j];
      end;
    haveWorld := True;
  end;
  ids := modelIds[0];
  nIds := modelCounts[0];
  SetLength(ids, nIds);

  if nIds = 0 then
    world.headnode := -1 - m.emptyLeaf
  else
    world.headnode := MeshBuild(m, ids, 0);
  if world.headnode < 0 then
  begin
    { headnode 0 must be the world (CM_PointContents(p, 0)), so tiny meshes
      still get a root node; both sides lead to the same leaf }
    for j := 0 to 2 do
      nrm[j] := 0;
    nrm[2] := 1;
    if MNodes = Length(m.nodes) then
      SetLength(m.nodes, MNodes * 2 + 16);
    m.nodes[MNodes].plane := MeshAddPlane(m, nrm, 0);
    m.nodes[MNodes].children[0] := world.headnode;
    m.nodes[MNodes].children[1] := world.headnode;
    Inc(MNodes);
    world.headnode := 0;
  end;
  for j := 0 to 2 do
  begin
    world.mins[j] := mins[j] - 1;
    world.maxs[j] := maxs[j] + 1;
    world.origin[j] := 0;
  end;
  SetLength(m.models, numModels);
  m.models[0] := world;
  { inline models after the world (headnode 0 stays the world's root) }
  for n := 1 to numModels - 1 do
  begin
    SetLength(modelIds[n], modelCounts[n]);
    if modelCounts[n] = 0 then
      inl.headnode := -1 - m.emptyLeaf
    else
      inl.headnode := MeshBuild(m, modelIds[n], 0);
    if inl.headnode < 0 then
    begin
      { a node, as qbsp writes for every model (servers take a negative
        headnode for "not a BSP model"); both sides lead to the leaf }
      for j := 0 to 2 do
        nrm[j] := 0;
      nrm[2] := 1;
      k := MeshAddPlane(m, nrm, 0);
      if MNodes = Length(m.nodes) then
        SetLength(m.nodes, MNodes * 2 + 16);
      m.nodes[MNodes].plane := k;
      m.nodes[MNodes].children[0] := inl.headnode;
      m.nodes[MNodes].children[1] := inl.headnode;
      inl.headnode := MNodes;
      Inc(MNodes);
    end;
    for j := 0 to 2 do
    begin
      any := modelMins[n][j] <= modelMaxs[n][j];
      if any then
      begin
        inl.mins[j] := modelMins[n][j] - 1;
        inl.maxs[j] := modelMaxs[n][j] + 1;
      end
      else
      begin
        inl.mins[j] := 0;
        inl.maxs[j] := 0;
      end;
      inl.origin[j] := 0;
    end;
    m.models[n] := inl;
  end;

  SetLength(m.planes, MPlanes);
  SetLength(m.nodes, MNodes);
  SetLength(m.leafs, MLeafs);
  SetLength(m.leafBrushes, MLeafBrushes);
  SetLength(m.brushes, MBrushes);
  SetLength(m.sides, MSides);
  m.numPlanes := MPlanes;
  m.numNodes := MNodes;
  m.numLeafs := MLeafs;
  m.numLeafBrushes := MLeafBrushes;
  m.numBrushes := MBrushes;
  m.numSides := MSides;
  m.numClusters := 1;
  m.numAreas := 2;
  m.numAreaPortals := 0;
  SetLength(m.areas, 2);
  for i := 0 to 1 do
  begin
    m.areas[i].numPortals := 0;
    m.areas[i].firstPortal := 0;
    m.areas[i].floodnum := 0;
    m.areas[i].floodvalid := 0;
  end;
  if d.entities <> nil then
    m.entities := StrPas(d.entities)
  else
    m.entities := '';
  m.meshMap := True;
  InitBoxHull(m);
  FloodAreaConnections(m);
  Result := True;
end;

{ ---------------------------------------------------------------- queries }

function ValidHeadnode(h: LongInt): Boolean;
begin
  if not Map.loaded then
    Exit(False);
  if h >= 0 then
    Result := h < Map.numNodes + 6
  else
    Result := -1 - Int64(h) < Int64(Map.numLeafs) + 1;
end;

function PointLeafnum(const p: TVec3; num: LongInt): LongInt;
var
  d: Single;
begin
  while num >= 0 do
  begin
    with Map.planes[Map.nodes[num].plane] do
      if ptype < 3 then
        d := p[ptype] - dist
      else
        d := Dot(normal, p) - dist;
    if d < 0 then
      num := Map.nodes[num].children[1]
    else
      num := Map.nodes[num].children[0];
  end;
  Result := -1 - num;
end;

{ BOX_ON_PLANE_SIDE: 1 front, 2 back, 3 both. }
function BoxOnPlaneSide(const mins, maxs: TVec3; const p: TPlane): Integer;
var
  d1, d2: Single;
  j: Integer;
begin
  if p.ptype < 3 then
  begin
    if p.dist <= mins[p.ptype] then
      Exit(1);
    if p.dist >= maxs[p.ptype] then
      Exit(2);
    Exit(3);
  end;
  d1 := 0;
  d2 := 0;
  for j := 0 to 2 do
    if p.normal[j] < 0 then
    begin
      d1 := d1 + p.normal[j] * mins[j];
      d2 := d2 + p.normal[j] * maxs[j];
    end
    else
    begin
      d1 := d1 + p.normal[j] * maxs[j];
      d2 := d2 + p.normal[j] * mins[j];
    end;
  Result := 0;
  if d1 >= p.dist then
    Result := 1;
  if d2 < p.dist then
    Result := Result or 2;
end;

var
  LeafMins, LeafMaxs: TVec3;
  LeafBuf: array[0..MAX_BOX_LEAFS - 1] of LongInt;
  LeafList: pcint32;  { output of BoxLeafnums }
  LeafMax, LeafCount, LeafTop: LongInt;

{ CM_BoxLeafnums_r }
procedure BoxLeafnums(num: LongInt);
var
  s: Integer;
begin
  while True do
  begin
    if num < 0 then
    begin
      if LeafCount < LeafMax then
      begin
        LeafList[LeafCount] := -1 - num;
        Inc(LeafCount);
      end;
      Exit;
    end;
    s := BoxOnPlaneSide(LeafMins, LeafMaxs, Map.planes[Map.nodes[num].plane]);
    if s = 1 then
      num := Map.nodes[num].children[0]
    else if s = 2 then
      num := Map.nodes[num].children[1]
    else
    begin { go down both }
      if LeafTop = -1 then
        LeafTop := num;
      BoxLeafnums(Map.nodes[num].children[0]);
      num := Map.nodes[num].children[1];
    end;
  end;
end;

{ CM_DecompressVis with bounds checks on the compressed input. }
procedure DecompressVis(cluster, which: LongInt; var row: TByteArr);
var
  rowBytes, inp, len, outp, c: LongInt;
  b: Byte;
begin
  rowBytes := (Map.numClusters + 7) shr 3;
  SetLength(row, rowBytes);
  if rowBytes = 0 then
    Exit;
  FillChar(row[0], rowBytes, 0);
  if Length(Map.visibility) = 0 then
  begin { no vis info, so make all visible }
    FillChar(row[0], rowBytes, $FF);
    Exit;
  end;
  if (cluster < 0) or (cluster >= Map.numClusters) then
    Exit;
  inp := Map.visOffsets[cluster * 2 + which];
  len := Length(Map.visibility);
  outp := 0;
  while outp < rowBytes do
  begin
    if inp >= len then
      Break; { truncated row: the rest stays invisible }
    b := Map.visibility[inp];
    if b <> 0 then
    begin
      row[outp] := b;
      Inc(outp);
      Inc(inp);
      Continue;
    end;
    if inp + 1 >= len then
      Break;
    c := Map.visibility[inp + 1];
    Inc(inp, 2);
    if c > rowBytes - outp then
      c := rowBytes - outp; { "Vis decompression overrun" }
    Inc(outp, c);
  end;
end;

function HeadnodeVisible(num: LongInt; visbits: PByte; size: LongInt): Boolean;
var
  cluster: LongInt;
begin
  if num < 0 then
  begin
    cluster := Map.leafs[-1 - num].cluster;
    if (cluster < 0) or ((cluster shr 3) >= size) then
      Exit(False);
    Exit((visbits[cluster shr 3] and (1 shl (cluster and 7))) <> 0);
  end;
  Result := HeadnodeVisible(Map.nodes[num].children[0], visbits, size) or
    HeadnodeVisible(Map.nodes[num].children[1], visbits, size);
end;

{ Point contents for mesh maps: their kd-tree leafs are not convex cells, so
  the brushes listed in the leaf decide. }
function LeafPointContents(leafnum: LongInt; const p: TVec3): LongInt;
var
  k, i, b, pl: LongInt;
  inside: Boolean;
begin
  if not Map.meshMap or (leafnum < 2) or (leafnum >= Map.numLeafs) then
    Exit(Map.leafs[leafnum].contents);
  Result := 0;
  for k := 0 to Map.leafs[leafnum].numLeafBrushes - 1 do
  begin
    b := Map.leafBrushes[Map.leafs[leafnum].firstLeafBrush + k];
    inside := True;
    i := 0;
    while (i < Map.brushes[b].numSides) and inside do
    begin
      pl := Map.sides[Map.brushes[b].firstSide + i].plane;
      if Dot(Map.planes[pl].normal, p) - Map.planes[pl].dist > 0 then
        inside := False;
      Inc(i);
    end;
    if inside then
      Result := Result or Map.brushes[b].contents;
  end;
end;

type
  TTraceWork = record
    start, end_, mins, maxs, extents: TVec3;
    contents: LongInt;
    isPoint: Boolean;
    { trace_t }
    allsolid, startsolid: Boolean;
    fraction: Single;
    plane: LongInt;
    surface: LongInt;
    hitContents: LongInt;
  end;

var
  T: TTraceWork;

procedure ResetTrace;
var
  i: Integer;
begin
  for i := 0 to 2 do
  begin
    T.start[i] := 0;
    T.end_[i] := 0;
    T.mins[i] := 0;
    T.maxs[i] := 0;
    T.extents[i] := 0;
  end;
  T.contents := 0;
  T.isPoint := False;
  T.allsolid := False;
  T.startsolid := False;
  T.fraction := 1;
  T.plane := -1;
  T.surface := -1;
  T.hitContents := 0;
end;

procedure ClipBoxToBrush(var brush: TBrush);
var
  enterfrac, leavefrac, dist, d1, d2, f: Single;
  clipplane, leadside, i, j, sideIndex, pl: LongInt;
  getout, startout: Boolean;
  ofs: TVec3;
begin
  if brush.numSides = 0 then
    Exit;
  enterfrac := -1;
  leavefrac := 1;
  clipplane := -1;
  leadside := -1;
  getout := False;
  startout := False;
  for i := 0 to brush.numSides - 1 do
  begin
    sideIndex := brush.firstSide + i;
    pl := Map.sides[sideIndex].plane;
    if not T.isPoint then
    begin
      for j := 0 to 2 do
        if Map.planes[pl].normal[j] < 0 then
          ofs[j] := T.maxs[j]
        else
          ofs[j] := T.mins[j];
      dist := Map.planes[pl].dist - Dot(ofs, Map.planes[pl].normal);
    end
    else
      dist := Map.planes[pl].dist;
    d1 := Dot(T.start, Map.planes[pl].normal) - dist;
    d2 := Dot(T.end_, Map.planes[pl].normal) - dist;
    if d2 > 0 then
      getout := True; { endpoint is not in solid }
    if d1 > 0 then
      startout := True;
    { if completely in front of face, no intersection }
    if (d1 > 0) and (d2 >= d1) then
      Exit;
    if (d1 <= 0) and (d2 <= 0) then
      Continue;
    if d1 > d2 then
    begin { enter }
      f := (d1 - DIST_EPSILON) / (d1 - d2);
      if f > enterfrac then
      begin
        enterfrac := f;
        clipplane := Map.sides[sideIndex].plane;
        leadside := sideIndex;
      end;
    end
    else
    begin { leave }
      f := (d1 + DIST_EPSILON) / (d1 - d2);
      if f < leavefrac then
        leavefrac := f;
    end;
  end;
  if not startout then
  begin { original point was inside brush }
    T.startsolid := True;
    if not getout then
      T.allsolid := True;
    Exit;
  end;
  if (enterfrac < leavefrac) and (enterfrac > -1) and (enterfrac < T.fraction) then
  begin
    if enterfrac < 0 then
      enterfrac := 0;
    T.fraction := enterfrac;
    T.plane := clipplane;
    T.surface := Map.sides[leadside].surface;
    T.hitContents := brush.contents;
  end;
end;

procedure TestBoxInBrush(var brush: TBrush);
var
  i, j, pl: LongInt;
  ofs: TVec3;
  dist, d1: Single;
begin
  if brush.numSides = 0 then
    Exit;
  for i := 0 to brush.numSides - 1 do
  begin
    pl := Map.sides[brush.firstSide + i].plane;
    for j := 0 to 2 do
      if Map.planes[pl].normal[j] < 0 then
        ofs[j] := T.maxs[j]
      else
        ofs[j] := T.mins[j];
    dist := Map.planes[pl].dist - Dot(ofs, Map.planes[pl].normal);
    d1 := Dot(T.start, Map.planes[pl].normal) - dist;
    { if completely in front of face, no intersection }
    if d1 > 0 then
      Exit;
  end;
  T.startsolid := True;
  T.allsolid := True;
  T.fraction := 0;
  T.hitContents := brush.contents;
end;

procedure ForLeafBrushes(leafnum: LongInt; fn: TBrushProc);
var
  k, b: LongInt;
begin
  if (Map.leafs[leafnum].contents and T.contents) = 0 then
    Exit;
  for k := 0 to Map.leafs[leafnum].numLeafBrushes - 1 do
  begin
    b := Map.leafBrushes[Map.leafs[leafnum].firstLeafBrush + k];
    { already checked this brush in another leaf }
    if Map.brushes[b].checkcount = Map.checkcount then
      Continue;
    Map.brushes[b].checkcount := Map.checkcount;
    if (Map.brushes[b].contents and T.contents) = 0 then
      Continue;
    fn(Map.brushes[b]);
    if T.fraction = 0 then
      Exit;
  end;
end;

procedure RecursiveHullCheck(num: LongInt; p1f, p2f: Single; const p1, p2: TVec3);
var
  t1, t2, offset, idist, frac, frac2, midf: Single;
  side, i: Integer;
  mid: TVec3;
  child0, child1: LongInt;
begin
  if T.fraction <= p1f then
    Exit; { already hit something nearer }
  if num < 0 then
  begin
    ForLeafBrushes(-1 - num, @ClipBoxToBrush);
    Exit;
  end;
  child0 := Map.nodes[num].children[0];
  child1 := Map.nodes[num].children[1];
  with Map.planes[Map.nodes[num].plane] do
    if ptype < 3 then
    begin
      t1 := p1[ptype] - dist;
      t2 := p2[ptype] - dist;
      offset := T.extents[ptype];
    end
    else
    begin
      t1 := Dot(normal, p1) - dist;
      t2 := Dot(normal, p2) - dist;
      if T.isPoint then
        offset := 0
      else
        offset := Abs(T.extents[0] * normal[0]) + Abs(T.extents[1] * normal[1]) +
          Abs(T.extents[2] * normal[2]);
    end;

  if (t1 >= offset) and (t2 >= offset) then
  begin
    RecursiveHullCheck(child0, p1f, p2f, p1, p2);
    Exit;
  end;
  if (t1 < -offset) and (t2 < -offset) then
  begin
    RecursiveHullCheck(child1, p1f, p2f, p1, p2);
    Exit;
  end;

  { put the crosspoint DIST_EPSILON pixels on the near side }
  if t1 < t2 then
  begin
    idist := ONE / (t1 - t2);
    side := 1;
    frac2 := (t1 + offset + DIST_EPSILON) * idist;
    frac := (t1 - offset + DIST_EPSILON) * idist;
  end
  else if t1 > t2 then
  begin
    idist := ONE / (t1 - t2);
    side := 0;
    frac2 := (t1 - offset - DIST_EPSILON) * idist;
    frac := (t1 + offset + DIST_EPSILON) * idist;
  end
  else
  begin
    side := 0;
    frac := 1;
    frac2 := 0;
  end;

  { move up to the node }
  if frac < 0 then
    frac := 0;
  if frac > 1 then
    frac := 1;
  midf := p1f + (p2f - p1f) * frac;
  for i := 0 to 2 do
    mid[i] := p1[i] + frac * (p2[i] - p1[i]);
  if side = 0 then
    RecursiveHullCheck(child0, p1f, midf, p1, mid)
  else
    RecursiveHullCheck(child1, p1f, midf, p1, mid);

  { go past the node }
  if frac2 < 0 then
    frac2 := 0;
  if frac2 > 1 then
    frac2 := 1;
  midf := p1f + (p2f - p1f) * frac2;
  for i := 0 to 2 do
    mid[i] := p1[i] + frac2 * (p2[i] - p1[i]);
  if side = 0 then
    RecursiveHullCheck(child1, midf, p2f, mid, p2)
  else
    RecursiveHullCheck(child0, midf, p2f, mid, p2);
end;

{ CM_BoxTrace in the model's local space. Returns the end position. }
procedure BoxTrace(const start, end_, mins, maxs: TVec3; headnode, brushmask: LongInt;
  out endpos: TVec3);
var
  i: Integer;
begin
  Inc(Map.checkcount); { for multi-check avoidance }
  ResetTrace;
  T.contents := brushmask;
  T.start := start;
  T.end_ := end_;
  T.mins := mins;
  T.maxs := maxs;

  { check for position test special case }
  if (start[0] = end_[0]) and (start[1] = end_[1]) and (start[2] = end_[2]) then
  begin
    for i := 0 to 2 do
    begin
      LeafMins[i] := start[i] + mins[i] - 1;
      LeafMaxs[i] := start[i] + maxs[i] + 1;
    end;
    LeafList := @LeafBuf[0];
    LeafMax := MAX_BOX_LEAFS;
    LeafCount := 0;
    LeafTop := -1;
    BoxLeafnums(headnode);
    for i := 0 to LeafCount - 1 do
    begin
      ForLeafBrushes(LeafBuf[i], @TestBoxInBrush);
      if T.allsolid then
        Break;
    end;
    endpos := start;
    Exit;
  end;

  { check for point special case }
  if (mins[0] = 0) and (mins[1] = 0) and (mins[2] = 0) and
     (maxs[0] = 0) and (maxs[1] = 0) and (maxs[2] = 0) then
  begin
    T.isPoint := True;
    T.extents[0] := 0;
    T.extents[1] := 0;
    T.extents[2] := 0;
  end
  else
  begin
    T.isPoint := False;
    for i := 0 to 2 do
      if -mins[i] > maxs[i] then
        T.extents[i] := -mins[i]
      else
        T.extents[i] := maxs[i];
  end;

  RecursiveHullCheck(headnode, 0, 1, start, end_);

  if T.fraction = 1 then
    endpos := end_
  else
    for i := 0 to 2 do
      endpos[i] := start[i] + T.fraction * (end_[i] - start[i]);
end;

{ -------------------------------------------------------------------- API }

var
  CrcTable: array[0..255] of cuint32;
  CrcReady: Boolean = False;

function CRC32_Block(data: PByte; size: SizeInt): cuint32;
var
  i: SizeInt;
  c: cuint32;
  n, k: Integer;
begin
  if not CrcReady then
  begin
    for n := 0 to 255 do
    begin
      c := cuint32(n);
      for k := 0 to 7 do
        if (c and 1) <> 0 then
          c := cuint32($EDB88320) xor (c shr 1)
        else
          c := c shr 1;
      CrcTable[n] := c;
    end;
    CrcReady := True;
  end;
  c := $FFFFFFFF;
  for i := 0 to size - 1 do
    c := CrcTable[(c xor data[i]) and $FF] xor (c shr 8);
  Result := c xor $FFFFFFFF;
end;

function CM_LoadMap(const name: AnsiString; data: PByte; size: SizeInt;
  out error: AnsiString): Boolean;
var
  m: TMapState;
begin
  error := '';
  CM_UnloadMap;
  ClearMap(m);
  LData := data;
  LSize := size;
  LError := '';
  if not LoadInto(m) then
  begin
    error := LError;
    LData := nil;
    Exit(False);
  end;
  LData := nil;
  m.loaded := True;
  m.name := name;
  m.checksum := CRC32_Block(data, size);
  Map := m;
  Result := True;
end;

procedure CM_UnloadMap;
begin
  ClearMap(Map);
end;

function CM_Loaded: Boolean;
begin
  Result := Map.loaded;
end;

function CM_MapInfo(info: PRealmsMapInfo): cint32;
var
  structSize: cuint32;
  n, j: Integer;
begin
  if (info = nil) or (info^.struct_size < SizeOf(TRealmsMapInfo)) then
    Exit(0);
  structSize := info^.struct_size;
  FillChar(info^, SizeOf(TRealmsMapInfo), 0);
  info^.struct_size := structSize;
  if not Map.loaded then
    Exit(0);
  info^.checksum := Map.checksum;
  info^.num_planes := Map.numPlanes;
  info^.num_nodes := Map.numNodes;
  info^.num_leafs := Map.numLeafs;
  info^.num_leaf_brushes := Map.numLeafBrushes;
  info^.num_brushes := Map.numBrushes;
  info^.num_brush_sides := Map.numSides;
  info^.num_texinfo := Length(Map.surfaces);
  info^.num_models := Length(Map.models);
  info^.num_clusters := Map.numClusters;
  info^.num_areas := Map.numAreas;
  info^.num_area_portals := Map.numAreaPortals;
  info^.entity_length := Length(Map.entities);
  for j := 0 to 2 do
  begin
    info^.mins[j] := Map.models[0].mins[j];
    info^.maxs[j] := Map.models[0].maxs[j];
  end;
  n := Length(Map.name);
  if n > High(info^.name) then
    n := High(info^.name);
  if n > 0 then
    Move(Map.name[1], info^.name[0], n);
  info^.name[n] := #0;
  Result := 1;
end;

function CM_Entities(buffer: PChar; bufferSize: cint32): cint32;
var
  n: SizeInt;
begin
  if (buffer <> nil) and (bufferSize > 0) then
    buffer[0] := #0;
  if not Map.loaded then
    Exit(-1);
  if (buffer <> nil) and (bufferSize > 0) then
  begin
    n := Length(Map.entities);
    if n > bufferSize - 1 then
      n := bufferSize - 1;
    if n > 0 then
      Move(Map.entities[1], buffer^, n);
    buffer[n] := #0;
  end;
  Result := Length(Map.entities);
end;

function CM_Model(index: cint32; mins, maxs, origin: pcfloat; headnode: pcint32): cint32;
var
  j: Integer;
begin
  if not Map.loaded or (index < 0) or (index >= Length(Map.models)) then
    Exit(0);
  for j := 0 to 2 do
  begin
    if mins <> nil then
      mins[j] := Map.models[index].mins[j];
    if maxs <> nil then
      maxs[j] := Map.models[index].maxs[j];
    if origin <> nil then
      origin[j] := Map.models[index].origin[j];
  end;
  if headnode <> nil then
    headnode^ := Map.models[index].headnode;
  Result := 1;
end;

function CM_HeadnodeForBox(mins, maxs: pcfloat): cint32;
var
  a, b: Integer;
begin
  if not Map.loaded or (mins = nil) or (maxs = nil) then
    Exit(0);
  b := Map.boxPlanes;
  for a := 0 to 2 do
  begin
    Map.planes[b + a * 4 + 0].dist := maxs[a];
    Map.planes[b + a * 4 + 1].dist := -maxs[a];
    Map.planes[b + a * 4 + 2].dist := mins[a];
    Map.planes[b + a * 4 + 3].dist := -mins[a];
  end;
  Result := Map.boxHeadnode;
end;

function CM_PointContents(p: pcfloat; headnode: cint32; origin, angles: pcfloat): cint32;
var
  local, f, r, u: TVec3;
  i: Integer;
begin
  if (p = nil) or not ValidHeadnode(headnode) then
    Exit(0);
  local := Vec(p);
  if origin <> nil then
    for i := 0 to 2 do
      local[i] := local[i] - origin[i];
  if (headnode <> Map.boxHeadnode) and AnyNonZero(angles) then
  begin
    AngleVectors(angles, f, r, u);
    RotateInto(local, f, r, u);
  end;
  Result := LeafPointContents(PointLeafnum(local, headnode), local);
end;

procedure CM_BoxTrace(start, end_, mins, maxs: pcfloat; headnode, brushmask: cint32;
  origin, angles: pcfloat; out_trace: PRealmsTrace);
const
  ZERO: TVec3 = (0, 0, 0);
var
  transformed, rotated: Boolean;
  s, e, vmins, vmaxs, f, r, u, endpos, neg, normal: TVec3;
  i: Integer;
begin
  if out_trace = nil then
    Exit;
  FillChar(out_trace^, SizeOf(TRealmsTrace), 0);
  out_trace^.fraction := 1;
  if end_ <> nil then
    for i := 0 to 2 do
      out_trace^.endpos[i] := end_[i];
  if (start = nil) or (end_ = nil) or not ValidHeadnode(headnode) then
    Exit;
  if mins <> nil then
    vmins := Vec(mins)
  else
    vmins := ZERO;
  if maxs <> nil then
    vmaxs := Vec(maxs)
  else
    vmaxs := ZERO;

  transformed := (origin <> nil) or (angles <> nil);
  rotated := (headnode <> Map.boxHeadnode) and AnyNonZero(angles);
  s := Vec(start);
  e := Vec(end_);
  if origin <> nil then
    for i := 0 to 2 do
    begin
      s[i] := s[i] - origin[i];
      e[i] := e[i] - origin[i];
    end;
  if rotated then
  begin
    AngleVectors(angles, f, r, u);
    RotateInto(s, f, r, u);
    RotateInto(e, f, r, u);
  end;
  BoxTrace(s, e, vmins, vmaxs, headnode, brushmask, endpos);

  if T.allsolid then
    out_trace^.allsolid := 1;
  if T.startsolid then
    out_trace^.startsolid := 1;
  out_trace^.fraction := T.fraction;
  out_trace^.contents := T.hitContents;
  if T.plane >= 0 then
  begin
    for i := 0 to 2 do
      out_trace^.plane_normal[i] := Map.planes[T.plane].normal[i];
    out_trace^.plane_dist := Map.planes[T.plane].dist;
  end;
  if T.surface >= 0 then
  begin
    Move(Map.surfaces[T.surface].name, out_trace^.surface_name, SizeOf(out_trace^.surface_name));
    out_trace^.surface_flags := Map.surfaces[T.surface].flags;
    out_trace^.surface_value := Map.surfaces[T.surface].value;
  end;
  if rotated and (T.fraction <> 1) then
  begin
    for i := 0 to 2 do
      neg[i] := -angles[i];
    AngleVectors(@neg[0], f, r, u);
    for i := 0 to 2 do
      normal[i] := out_trace^.plane_normal[i];
    RotateInto(normal, f, r, u);
    for i := 0 to 2 do
      out_trace^.plane_normal[i] := normal[i];
  end;
  if transformed then
    for i := 0 to 2 do
      out_trace^.endpos[i] := start[i] + T.fraction * (end_[i] - start[i])
  else
    for i := 0 to 2 do
      out_trace^.endpos[i] := endpos[i];
end;

function CM_PointLeaf(p: pcfloat): cint32;
begin
  if not Map.loaded or (p = nil) then
    Exit(0);
  Result := PointLeafnum(Vec(p), Map.models[0].headnode);
end;

function CM_LeafInfo(leaf: cint32; contents, cluster, area: pcint32): cint32;
begin
  if not Map.loaded or (leaf < 0) or (leaf >= Map.numLeafs) then
    Exit(0);
  if contents <> nil then
    contents^ := Map.leafs[leaf].contents;
  if cluster <> nil then
    cluster^ := Map.leafs[leaf].cluster;
  if area <> nil then
    area^ := Map.leafs[leaf].area;
  Result := 1;
end;

{ ------------------------------------------------------- bridge version 3 }

function CM_LoadMesh(desc: PRealmsMeshDesc; out error: AnsiString): Boolean;
var
  m: TMapState;
  ok: Boolean;
begin
  error := '';
  CM_UnloadMap;
  { hosts built before triangle_models / num_models pass the shorter
    description (REALMS_MESH_DESC_V3_SIZE): world only }
  if (desc = nil) or (desc^.struct_size < PtrUInt(@PRealmsMeshDesc(nil)^.triangle_models)) then
  begin
    error := 'bad mesh description';
    Exit(False);
  end;
  ClearMap(m);
  LError := '';
  try
    ok := LoadMeshInto(m, desc^);
  finally
    SetLength(MBounds, 0);
    MBoundsCount := 0;
  end;
  if not ok then
  begin
    error := LError;
    Exit(False);
  end;
  m.loaded := True;
  if desc^.name <> nil then
    m.name := StrPas(desc^.name)
  else
    m.name := '';
  m.checksum := desc^.checksum;
  Map := m;
  Result := True;
end;

function CopyRow(const row: TByteArr; buffer: PByte; size: cint32): cint32;
var
  n: LongInt;
begin
  n := Length(row);
  if n > size then
    n := size;
  if (buffer <> nil) and (n > 0) then
    Move(row[0], buffer^, n);
  Result := Length(row);
end;

function CM_ClusterPVS(cluster: cint32; buffer: PByte; size: cint32): cint32;
var
  row: TByteArr;
begin
  if not Map.loaded then
    Exit(0);
  row := nil;
  DecompressVis(cluster, 0, row);
  Result := CopyRow(row, buffer, size);
end;

function CM_ClusterPHS(cluster: cint32; buffer: PByte; size: cint32): cint32;
var
  row: TByteArr;
begin
  if not Map.loaded then
    Exit(0);
  row := nil;
  DecompressVis(cluster, 1, row);
  Result := CopyRow(row, buffer, size);
end;

procedure CM_SetAreaPortal(portal, open: cint32);
begin
  if not Map.loaded or (portal < 0) or (portal >= MAX_MAP_AREAPORTALS) then
    Exit;
  if open <> 0 then
    Map.portalOpen[portal] := 1
  else
    Map.portalOpen[portal] := 0;
  FloodAreaConnections(Map);
end;

function CM_AreaPortalOpen(portal: cint32): cint32;
begin
  if not Map.loaded or (portal < 0) or (portal >= MAX_MAP_AREAPORTALS) then
    Exit(-1);
  Result := Map.portalOpen[portal];
end;

function CM_AreasConnected(area1, area2: cint32): cint32;
begin
  if not Map.loaded or (area1 < 0) or (area2 < 0) or (area1 >= Map.numAreas) or
     (area2 >= Map.numAreas) then
    Exit(0);
  if Map.areas[area1].floodnum = Map.areas[area2].floodnum then
    Result := 1
  else
    Result := 0;
end;

function CM_WriteAreaBits(area: cint32; buffer: PByte; size: cint32): cint32;
var
  row: TByteArr;
  i, floodnum: LongInt;
  all: Boolean;
begin
  if not Map.loaded then
    Exit(0);
  row := nil;
  SetLength(row, (Map.numAreas + 7) shr 3);
  if Length(row) > 0 then
    FillChar(row[0], Length(row), 0);
  all := (area <= 0) or (area >= Map.numAreas);
  if all then
    floodnum := 0
  else
    floodnum := Map.areas[area].floodnum;
  for i := 0 to Map.numAreas - 1 do
    if all or (Map.areas[i].floodnum = floodnum) then
      row[i shr 3] := row[i shr 3] or Byte(1 shl (i and 7));
  Result := CopyRow(row, buffer, size);
end;

function CM_BoxLeafs(mins, maxs: pcfloat; list: pcint32; listSize, headnode: cint32;
  topnode: pcint32): cint32;
begin
  if topnode <> nil then
    topnode^ := -1;
  if (mins = nil) or (maxs = nil) or not ValidHeadnode(headnode) then
    Exit(0);
  LeafMins := Vec(mins);
  LeafMaxs := Vec(maxs);
  LeafList := list;
  if (list = nil) or (listSize < 0) then
    LeafMax := 0
  else
    LeafMax := listSize;
  LeafCount := 0;
  LeafTop := -1;
  BoxLeafnums(headnode);
  LeafList := nil;
  if topnode <> nil then
    topnode^ := LeafTop;
  Result := LeafCount;
end;

function CM_HeadnodeVisible(headnode: cint32; visbits: PByte; size: cint32): cint32;
begin
  if (visbits = nil) or (size <= 0) or not ValidHeadnode(headnode) then
    Exit(0);
  if HeadnodeVisible(headnode, visbits, size) then
    Result := 1
  else
    Result := 0;
end;

end.
