{ ----------------------------------------------------------------------------
  Realms Engine - collision model (Pascal core)

  Quake 2 qcommon/cmodel.c for the Realms bridge (version 2): loads IBSP v38
  maps from the virtual filesystem, validates every index before use and
  answers point contents / box trace / leaf queries. The algorithms follow
  game/qcommon/CModel.pas (the original Delphi port) with its 64-bit pointer
  casts, fixed-size tables and Com_Error exits replaced by dynamic arrays and
  a clean rejection path: a malformed map leaves no map loaded.

  Keep in lock-step with realms/src/NativeCModel.cpp - the conformance suite
  (tests/realms/realms_map_suite.h) runs the same checks against both cores.
  ---------------------------------------------------------------------------- }
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

function LoadInto(var m: TMapState): Boolean;
var
  n, i, j, numSurfaces, num, first, portal, other, areaLimit, entLen, used: LongInt;
  p, ent: PByte;
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
  for i := 0 to m.numAreas - 1 do
  begin
    p := LumpAt(LUMP_AREAS, i, 8);
    num := RD32(p);
    first := RD32(p + 4);
    if (num < 0) or (first < 0) or (Int64(first) + num > m.numAreaPortals) then
      Exit(Fail('bad area portals'));
  end;
  for i := 0 to m.numAreaPortals - 1 do
  begin
    p := LumpAt(LUMP_AREAPORTALS, i, 8);
    portal := RD32(p);
    other := RD32(p + 4);
    if (portal < 0) or (portal >= MAX_MAP_AREAPORTALS) or (other < 0) or (other >= m.numAreas) then
      Exit(Fail('bad area portal'));
  end;
  if m.numAreas > 0 then
    areaLimit := m.numAreas
  else
    areaLimit := 1;
  for i := 0 to m.numLeafs - 1 do
    if (m.leafs[i].area < 0) or (m.leafs[i].area >= areaLimit) then
      Exit(Fail('bad leaf area'));

  { Visibility (kept for the PVS; only the size is checked here). }
  if Lumps[LUMP_VISIBILITY].len > MAX_MAP_VISIBILITY then
    Exit(Fail('visibility lump is too large'));

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
  LeafList: array[0..MAX_BOX_LEAFS - 1] of LongInt;
  LeafCount: LongInt;

procedure BoxLeafnums(num: LongInt);
var
  s: Integer;
begin
  while True do
  begin
    if num < 0 then
    begin
      if LeafCount < MAX_BOX_LEAFS then
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
    begin
      BoxLeafnums(Map.nodes[num].children[0]);
      num := Map.nodes[num].children[1];
    end;
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
    LeafCount := 0;
    BoxLeafnums(headnode);
    for i := 0 to LeafCount - 1 do
    begin
      ForLeafBrushes(LeafList[i], @TestBoxInBrush);
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
  Result := Map.leafs[PointLeafnum(local, headnode)].contents;
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

end.
