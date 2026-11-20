/**
 * Процедурный генератор огромного разрушенного города (Three.js).
 * Не зависит от DOM: принимает материалы снаружи, возвращает THREE.Group + данные для игры
 * (коллайдеры зданий, точки спавна, очаги пожаров, воронки).
 *
 * Система координат: Y — вверх, единицы — метры.
 * Вся геометрия слита в чанки (~200 м) по трём материалам => мало draw call'ов для Android.
 */
import * as THREE from "three";
import { mergeGeometries } from "three/examples/jsm/utils/BufferGeometryUtils.js";

export interface CityOptions {
  seed: number;
  /** количество кварталов по каждой оси (N x N) */
  blocks: number;
  /** уровень разрушений 0..1 */
  destruction: number;
}

export interface CityMaterials {
  solid: THREE.Material; // стены с текстурой окон
  plain: THREE.Material; // всё остальное (вертексные цвета)
  fire: THREE.Material; // эмиссивные языки огня
}

export interface Collider {
  x: number;
  z: number;
  hw: number;
  hd: number;
  h: number;
}

export interface BlockInfo {
  x0: number;
  z0: number;
  x1: number;
  z1: number;
  kind: "city" | "park" | "crater" | "rubble";
}

export interface CityData {
  group: THREE.Group;
  options: CityOptions;
  colliders: Collider[];
  fires: THREE.Vector3[];
  craters: { x: number; z: number; r: number }[];
  spawns: { x: number; z: number; yaw: number }[];
  blocks: BlockInfo[];
  half: number; // половина стороны города, м
  stats: { buildings: number; triangles: number; vertices: number; props: number; craters: number; fires: number };
}

type Kind = "solid" | "plain" | "fire";

export const FL = 3.2; // высота этажа
const TW = 6; // ширина текстурной плитки (2 окна) в метрах
const TH = 6.4; // высота плитки (2 этажа)
const BLOCK = 64;
const CHUNK = 200;
const MAX_FIRES = 90;

const roadWidth = (i: number) => (i % 4 === 0 ? 26 : 14);

export function cityExtent(blocks: number): number {
  let t = 0;
  for (let i = 0; i <= blocks; i++) {
    t += roadWidth(i);
    if (i < blocks) t += BLOCK;
  }
  return t;
}

/* ------------------------------ утилиты ------------------------------ */

function mulberry32(a: number) {
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
type Rnd = () => number;
const pick = <T>(r: Rnd, a: T[]): T => a[Math.floor(r() * a.length)];
const ri = (r: Rnd, a: number, b: number) => a + Math.floor(r() * (b - a + 1));
const smooth = (t: number) => {
  const x = Math.min(1, Math.max(0, t));
  return x * x * (3 - 2 * x);
};

const CONCRETE = [0x8d8a84, 0x9a958b, 0x7a7e80, 0xa59d8f, 0x6f6b66, 0x8a7f75, 0x93897a, 0x7d7469];
const BRICK = [0x8a5848, 0x7a4c3d, 0x956552];
const RUBBLE = [0x75716b, 0x5f5b56, 0x857f76, 0x4c4843, 0x6b5f52];
const RUST = [0x6b3d24, 0x5a3a28, 0x4a3426];
const CAR_COLORS = [0x6d3a2a, 0x59605f, 0x7a6f4f, 0x4a5a6b, 0x8a8478, 0x5c2b2b, 0x3e4b3a];

const _m = new THREE.Matrix4();
const _q = new THREE.Quaternion();
const _e = new THREE.Euler();
const _p = new THREE.Vector3();
const _s = new THREE.Vector3(1, 1, 1);

class Ctx {
  batches = new Map<string, THREE.BufferGeometry[]>();
  base: THREE.Matrix4 | null = null;
  kx = 0;
  kz = 0;
  props = 0;
  constructor(public rnd: Rnd) {}

  at(x: number, z: number) {
    this.kx = Math.floor(x / CHUNK);
    this.kz = Math.floor(z / CHUNK);
  }

  jc(hex: number, amt = 0.22) {
    const c = new THREE.Color(hex);
    c.multiplyScalar(1 + (this.rnd() - 0.5) * amt);
    return c;
  }

  push(kind: Kind, g: THREE.BufferGeometry) {
    const key = `${kind}|${this.kx}|${this.kz}`;
    let arr = this.batches.get(key);
    if (!arr) {
      arr = [];
      this.batches.set(key, arr);
    }
    arr.push(g);
    if (arr.length > 1200) {
      const merged = mergeGeometries(arr, false);
      arr.forEach((x) => x.dispose());
      arr.length = 0;
      if (merged) arr.push(merged);
    }
  }

  /** применяет матрицу, красит вершины (с затемнением у земли) и кладёт в чанк */
  emit(kind: Kind, g: THREE.BufferGeometry, m: THREE.Matrix4, color: THREE.Color, ao: boolean) {
    if (this.base) m.premultiply(this.base);
    g.applyMatrix4(m);
    const pos = g.getAttribute("position");
    const n = pos.count;
    const col = new Float32Array(n * 3);
    for (let i = 0; i < n; i++) {
      const f = ao ? 0.45 + 0.55 * smooth(pos.getY(i) / 7) : 1;
      col[i * 3] = color.r * f;
      col[i * 3 + 1] = color.g * f;
      col[i * 3 + 2] = color.b * f;
    }
    g.setAttribute("color", new THREE.BufferAttribute(col, 3));
    this.push(kind, g);
  }
}

interface BoxOpt {
  rx?: number;
  ry?: number;
  rz?: number;
  wall?: boolean;
  ao?: boolean;
  uo?: number;
  /** оставить нижнюю грань (для висящих плит); по умолчанию вырезается — экономия 1/6 треугольников */
  bottom?: boolean;
}

/** бокс с началом координат в центре основания */
function box(
  ctx: Ctx,
  kind: Kind,
  w: number,
  h: number,
  d: number,
  x: number,
  y: number,
  z: number,
  color: number | THREE.Color,
  o: BoxOpt = {}
) {
  const g = new THREE.BoxGeometry(w, h, d);
  if (!o.bottom) {
    // вырезаем грань -Y (вершины 12..15) — её никто не видит
    const cut = (name: string, size: number) => {
      const a = Array.from(g.getAttribute(name).array as ArrayLike<number>);
      a.splice(12 * size, 4 * size);
      g.setAttribute(name, new THREE.Float32BufferAttribute(a, size));
    };
    cut("position", 3);
    cut("normal", 3);
    cut("uv", 2);
    const idx = Array.from(g.index!.array);
    idx.splice(18, 6);
    g.setIndex(idx.map((i) => (i >= 16 ? i - 4 : i)));
  }
  g.translate(0, h / 2, 0);
  const uv = g.getAttribute("uv");
  for (let f = 0; f < 6; f++) {
    if (f === 3 && !o.bottom) continue;
    for (let i = 0; i < 4; i++) {
      const k = (f > 3 && !o.bottom ? f - 1 : f) * 4 + i;
      if (!o.wall || (f >= 2 && f < 4)) {
        uv.setXY(k, 0.02, 0.02); // глухая стена / крыша
      } else {
        const len = f < 2 ? d : w;
        uv.setXY(k, (uv.getX(k) * len) / TW + (o.uo ?? 0), (uv.getY(k) * h) / TH);
      }
    }
  }
  _e.set(o.rx ?? 0, o.ry ?? 0, o.rz ?? 0);
  _q.setFromEuler(_e);
  _m.compose(_p.set(x, y, z), _q, _s);
  ctx.emit(kind, g, _m, typeof color === "number" ? new THREE.Color(color) : color, !!o.ao);
}

function flat(ctx: Ctx, w: number, d: number, x: number, y: number, z: number, ry: number, color: THREE.Color | number) {
  const g = new THREE.PlaneGeometry(w, d);
  g.rotateX(-Math.PI / 2);
  const uv = g.getAttribute("uv");
  for (let i = 0; i < uv.count; i++) uv.setXY(i, 0.02, 0.02);
  _e.set(0, ry, 0);
  _q.setFromEuler(_e);
  _m.compose(_p.set(x, y, z), _q, _s);
  ctx.emit("plain", g, _m, typeof color === "number" ? new THREE.Color(color) : color, false);
}

/* ------------------------------ объекты ------------------------------ */

function pile(ctx: Ctx, cx: number, cz: number, radius: number, count: number, maxSize: number, y0 = 0) {
  const rnd = ctx.rnd;
  for (let i = 0; i < count; i++) {
    const a = rnd() * Math.PI * 2;
    const r = radius * Math.sqrt(rnd());
    const s = 0.4 + rnd() * maxSize;
    const hgt = (1 - r / radius) * radius * 0.32;
    box(
      ctx,
      "plain",
      s * (0.7 + rnd()),
      s * (0.4 + rnd() * 0.6),
      s * (0.7 + rnd()),
      cx + Math.cos(a) * r,
      y0 + hgt - 0.1,
      cz + Math.sin(a) * r,
      ctx.jc(pick(rnd, RUBBLE), 0.3),
      { rx: rnd() * 3, ry: rnd() * 3, rz: rnd() * 3 }
    );
  }
  ctx.props++;
}

function rebar(ctx: Ctx, x: number, y: number, z: number, cluster = 1) {
  const rnd = ctx.rnd;
  for (let i = 0; i < cluster; i++) {
    const h = 1 + rnd() * 3.2;
    box(ctx, "plain", 0.08, h, 0.08, x + (rnd() - 0.5) * 2, y, z + (rnd() - 0.5) * 2, ctx.jc(pick(rnd, RUST), 0.3), {
      rx: (rnd() - 0.5) * 0.9,
      rz: (rnd() - 0.5) * 0.9,
    });
  }
}

function fire(ctx: Ctx, x: number, z: number, fires: THREE.Vector3[], y = 0.3) {
  if (fires.length >= MAX_FIRES) return;
  const rnd = ctx.rnd;
  ctx.at(x, z);
  ctx.base = null;
  for (let i = 0; i < 3; i++) {
    box(ctx, "plain", 1 + rnd() * 1.5, 0.5 + rnd() * 0.5, 1 + rnd() * 1.5, x + (rnd() - 0.5) * 2, y - 0.2, z + (rnd() - 0.5) * 2, 0x161413, {
      ry: rnd() * 3,
    });
  }
  fires.push(new THREE.Vector3(x, y, z));
  const cols = [0xff5a0a, 0xff8a14, 0xffc93a];
  for (let k = 0; k < 3; k++) {
    const h = 1.6 + rnd() * 2.8 - k * 0.3;
    const r = 0.5 + rnd() * 0.8 - k * 0.1;
    const g = new THREE.ConeGeometry(Math.max(0.2, r), h, 5, 1, true);
    g.translate(0, h / 2, 0);
    _e.set((rnd() - 0.5) * 0.3, rnd() * 3, (rnd() - 0.5) * 0.3);
    _q.setFromEuler(_e);
    _m.compose(_p.set(x + (rnd() - 0.5) * 1.2, y, z + (rnd() - 0.5) * 1.2), _q, _s);
    ctx.emit("fire", g, _m, new THREE.Color(cols[k]), false);
  }
}

function crater(ctx: Ctx, x: number, z: number, R: number, y0: number) {
  const rnd = ctx.rnd;
  ctx.at(x, z);
  ctx.base = null;
  const prof: [number, number][] = [
    [0, 0.05],
    [0.5, 0.05],
    [0.78, 0.45],
    [0.92, 0.8],
    [1.06, 0.55],
    [1.3, 0.14],
    [1.55, 0.02],
  ];
  const S = 20;
  const hs = Math.min(1, R / 6);
  const pos: number[] = [];
  const col: number[] = [];
  const idx: number[] = [];
  const cIn = new THREE.Color(0x1b1917);
  const cRim = new THREE.Color(0x4b4237);
  const cOut = new THREE.Color(0x35302a);
  const tmp = new THREE.Color();
  prof.forEach(([r, h], k) => {
    for (let s = 0; s < S; s++) {
      const a = (s / S) * Math.PI * 2;
      const jr = r * R * (1 + (r > 0.6 ? (rnd() - 0.5) * 0.12 : 0));
      pos.push(x + Math.cos(a) * jr, y0 + h * hs + (r > 0.6 ? rnd() * 0.15 : 0), z + Math.sin(a) * jr);
      if (r < 0.6) tmp.copy(cIn).lerp(cRim, r / 0.6 * 0.3);
      else if (r < 1.05) tmp.copy(cRim).multiplyScalar(0.8 + rnd() * 0.3);
      else tmp.copy(cRim).lerp(cOut, Math.min(1, (r - 1.05) / 0.5));
      col.push(tmp.r, tmp.g, tmp.b);
    }
    if (k < prof.length - 1) {
      for (let s = 0; s < S; s++) {
        const s2 = (s + 1) % S;
        const A = k * S + s;
        const B = (k + 1) * S + s;
        const C = k * S + s2;
        const D = (k + 1) * S + s2;
        idx.push(A, C, B, B, C, D);
      }
    }
  });
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute("color", new THREE.Float32BufferAttribute(col, 3));
  g.setAttribute("uv", new THREE.Float32BufferAttribute(new Array((pos.length / 3) * 2).fill(0.02), 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  ctx.push("plain", g);
  // обломки по краю
  const n = 5 + Math.floor(R);
  for (let i = 0; i < n; i++) {
    const a = rnd() * Math.PI * 2;
    const rr = R * (0.95 + rnd() * 0.5);
    const s = 0.4 + rnd() * 1.2;
    box(ctx, "plain", s, s * 0.6, s, x + Math.cos(a) * rr, y0 + 0.3, z + Math.sin(a) * rr, ctx.jc(pick(rnd, RUBBLE), 0.3), {
      rx: rnd() * 3,
      ry: rnd() * 3,
    });
  }
  ctx.props++;
}

function car(ctx: Ctx, x: number, z: number, yaw: number, y = 0.02) {
  const rnd = ctx.rnd;
  ctx.at(x, z);
  const burnt = rnd() < 0.35;
  const flipped = rnd() < 0.12;
  const body = burnt ? ctx.jc(0x1a1918, 0.4) : ctx.jc(pick(rnd, CAR_COLORS), 0.3);
  const old = ctx.base;
  const e = new THREE.Euler(flipped ? Math.PI : (rnd() - 0.5) * 0.08, yaw, (rnd() - 0.5) * 0.08, "YXZ");
  const m = new THREE.Matrix4().compose(
    new THREE.Vector3(x, flipped ? y + 1.6 : y, z),
    new THREE.Quaternion().setFromEuler(e),
    new THREE.Vector3(1, 1, 1)
  );
  ctx.base = m;
  box(ctx, "plain", 4.3, 0.7, 1.85, 0, 0.3, 0, body);
  box(ctx, "plain", 2.1, 0.58, 1.7, -0.2, 1.0, 0, burnt ? 0x0e0e0e : 0x1d252b);
  box(ctx, "plain", 1.95, 0.1, 1.62, -0.2, 1.58, 0, body);
  box(ctx, "plain", 0.75, 0.75, 2.0, 1.35, 0, 0, 0x101010);
  box(ctx, "plain", 0.75, 0.75, 2.0, -1.35, 0, 0, 0x101010);
  ctx.base = old;
  ctx.props++;
}

function lamp(ctx: Ctx, x: number, z: number, y: number) {
  const rnd = ctx.rnd;
  ctx.at(x, z);
  const fallen = rnd() < 0.15;
  const tilt = fallen ? Math.PI / 2 - 0.05 : (rnd() - 0.5) * 0.45;
  const ry = rnd() * Math.PI * 2;
  const m = new THREE.Matrix4().compose(
    new THREE.Vector3(x, y, z),
    new THREE.Quaternion().setFromEuler(new THREE.Euler(0, ry, tilt, "YXZ")),
    new THREE.Vector3(1, 1, 1)
  );
  const old = ctx.base;
  ctx.base = m;
  box(ctx, "plain", 0.2, 8, 0.2, 0, 0, 0, 0x3f4547);
  box(ctx, "plain", 2.2, 0.12, 0.15, 1.0, 7.9, 0, 0x3f4547);
  box(ctx, "plain", 0.6, 0.15, 0.3, 2.0, 7.75, 0, 0x2a2d2f);
  ctx.base = old;
  ctx.props++;
}

function deadTree(ctx: Ctx, x: number, z: number, y: number) {
  const rnd = ctx.rnd;
  ctx.at(x, z);
  const h = 3.5 + rnd() * 3;
  const trunk = 0x3a2f26;
  const old = ctx.base;
  ctx.base = new THREE.Matrix4().compose(
    new THREE.Vector3(x, y, z),
    new THREE.Quaternion().setFromEuler(new THREE.Euler((rnd() - 0.5) * 0.12, rnd() * 6, (rnd() - 0.5) * 0.12)),
    new THREE.Vector3(1, 1, 1)
  );
  box(ctx, "plain", 0.38, h, 0.38, 0, 0, 0, ctx.jc(trunk, 0.3));
  const nb = 3 + Math.floor(rnd() * 3);
  for (let i = 0; i < nb; i++) {
    const side = rnd() < 0.5 ? 1 : -1;
    box(ctx, "plain", 0.12, 1.6 + rnd() * 1.6, 0.12, 0, h * (0.4 + rnd() * 0.55), 0, ctx.jc(trunk, 0.3), {
      ry: rnd() * 6,
      rz: side * (0.6 + rnd() * 0.7),
    });
  }
  ctx.base = old;
  ctx.props++;
}

function barrier(ctx: Ctx, x: number, z: number, yaw: number) {
  const rnd = ctx.rnd;
  ctx.at(x, z);
  const n = ri(rnd, 2, 5);
  const cs = Math.cos(yaw);
  const sn = Math.sin(yaw);
  for (let i = 0; i < n; i++) {
    const t = (i - n / 2) * 2.8;
    box(ctx, "plain", 0.7, 0.95, 2.6, x + sn * t, 0, z + cs * t, ctx.jc(0x8d8b86, 0.3), {
      ry: yaw + (rnd() - 0.5) * 0.3,
      rz: rnd() < 0.15 ? 0.4 : 0,
    });
  }
  if (rnd() < 0.5) {
    for (let i = 0; i < 6; i++) {
      box(ctx, "plain", 0.9, 0.35, 0.45, x + (rnd() - 0.5) * 3, i < 4 ? 0 : 0.35, z + (rnd() - 0.5) * 3, ctx.jc(0x8a7a5a, 0.3), {
        ry: rnd() * 3,
      });
    }
  }
  ctx.props++;
}

/* ------------------------------ здания ------------------------------ */

function rubbleLot(ctx: Ctx, cx: number, cz: number, w: number, d: number, fires: THREE.Vector3[]) {
  const rnd = ctx.rnd;
  pile(ctx, cx, cz, Math.max(w, d) * 0.55, 16 + Math.floor((w * d) / 40), 2.4);
  const k = ri(rnd, 2, 4);
  for (let i = 0; i < k; i++) {
    const fw = 2 + rnd() * 5;
    const fh = FL * ri(rnd, 1, 2) * (0.6 + rnd() * 0.5);
    box(ctx, "solid", fw, fh, 0.4, cx + (rnd() - 0.5) * w * 0.8, 0, cz + (rnd() - 0.5) * d * 0.8, ctx.jc(pick(rnd, CONCRETE)), {
      wall: true,
      ao: true,
      ry: rnd() * 3,
      rz: (rnd() - 0.5) * 0.2,
    });
  }
  for (let i = 0; i < 4; i++) rebar(ctx, cx + (rnd() - 0.5) * w * 0.7, 0.5, cz + (rnd() - 0.5) * d * 0.7, 2);
  if (rnd() < 0.35) fire(ctx, cx + (rnd() - 0.5) * w * 0.4, cz + (rnd() - 0.5) * d * 0.4, fires, 0.9);
}

function building(
  ctx: Ctx,
  cx: number,
  cz: number,
  w: number,
  d: number,
  floors: number,
  D: number,
  colliders: Collider[],
  fires: THREE.Vector3[]
): void {
  const rnd = ctx.rnd;
  ctx.at(cx, cz);
  ctx.base = null;
  const r = rnd();
  const pRub = 0.06 + 0.2 * D;
  const pHalf = pRub + 0.16 + 0.34 * D;
  if (r < pRub) {
    rubbleLot(ctx, cx, cz, w, d, fires);
    return;
  }
  const half = r < pHalf;
  const baseHex = pick(rnd, rnd() < 0.25 ? BRICK : CONCRETE);
  const uo = rnd() < 0.5 ? 0 : 0.5;

  if (rnd() < 0.05 + 0.07 * D) {
    const e = new THREE.Euler((rnd() - 0.5) * 0.09, 0, (rnd() - 0.5) * 0.09);
    ctx.base = new THREE.Matrix4()
      .makeTranslation(cx, -0.3, cz)
      .multiply(new THREE.Matrix4().makeRotationFromEuler(e))
      .multiply(new THREE.Matrix4().makeTranslation(-cx, 0, -cz));
  }

  const ncx = Math.min(4, Math.max(1, Math.round(w / 10)));
  const ncz = Math.min(4, Math.max(1, Math.round(d / 10)));
  const cw = w / ncx;
  const cd = d / ncz;
  let maxTop = 0;

  for (let a = 0; a < ncx; a++) {
    for (let b = 0; b < ncz; b++) {
      const px = cx - w / 2 + cw * (a + 0.5);
      const pz = cz - d / 2 + cd * (b + 0.5);
      let top: number;
      if (!half) {
        top = floors;
        if (rnd() < 0.45) top = Math.max(2, Math.round(floors * (0.55 + rnd() * 0.45)));
      } else {
        top = Math.max(1, Math.round(floors * (0.12 + rnd() * 0.6)));
        if (rnd() < 0.15) top = 1;
      }
      let sk = rnd() < (half ? 0.5 : 0.4) ? 1 + Math.floor(rnd() * 3) : 0;
      sk = Math.min(sk, top - 1);
      const solid = top - sk;
      const color = ctx.jc(baseHex, 0.25);

      box(ctx, "solid", cw + 0.05, solid * FL, cd + 0.05, px, 0, pz, color, { wall: true, ao: true, uo });

      // пробоины в фасаде
      if (solid >= 3 && rnd() < 0.2 + 0.3 * D) {
        const faces: number[] = [];
        if (a === ncx - 1) faces.push(0);
        if (a === 0) faces.push(1);
        if (b === ncz - 1) faces.push(2);
        if (b === 0) faces.push(3);
        if (faces.length) {
          const f = pick(rnd, faces);
          const dim = f < 2 ? cd : cw;
          const hw = Math.min(2 + rnd() * 3, dim - 1);
          const hh = 2.5 + rnd() * 3;
          const room = solid * FL - hh - 2;
          if (room > 0.5 && hw > 1) {
            const y = 1.5 + rnd() * room;
            const o = 0.05;
            const sx = f === 0 ? 1 : -1;
            const sz = f === 2 ? 1 : -1;
            if (f < 2) {
              box(ctx, "plain", 0.1, hh + 1.4, hw + 1.4, px + sx * (cw / 2 + o), y - 0.7, pz, 0x2b2724);
              box(ctx, "plain", 0.1, hh, hw, px + sx * (cw / 2 + o * 2), y, pz, 0x0b0b0c);
            } else {
              box(ctx, "plain", hw + 1.4, hh + 1.4, 0.1, px, y - 0.7, pz + sz * (cd / 2 + o), 0x2b2724);
              box(ctx, "plain", hw, hh, 0.1, px, y, pz + sz * (cd / 2 + o * 2), 0x0b0b0c);
            }
          }
        }
      }

      // оголённый каркас
      const y0 = solid * FL;
      if (sk > 0) {
        for (let k = 0; k < sk; k++) {
          box(ctx, "plain", cw - 0.3, 0.35, cd - 0.3, px, y0 + k * FL, pz, ctx.jc(0x8d8a83, 0.2), {
            rx: rnd() < 0.12 ? (rnd() - 0.5) * 0.35 : 0,
            bottom: true,
          });
        }
        for (const sx of [-1, 1]) {
          for (const sz of [-1, 1]) {
            if (rnd() < 0.75) {
              box(ctx, "plain", 0.55, sk * FL, 0.55, px + sx * (cw / 2 - 0.5), y0, pz + sz * (cd / 2 - 0.5), ctx.jc(0x7c7973, 0.2));
            }
          }
        }
      }
      const topY = top * FL;
      maxTop = Math.max(maxTop, topY);

      const nr = rnd() < 0.7 ? 1 + Math.floor(rnd() * 3) : 0;
      for (let i = 0; i < nr; i++) rebar(ctx, px + (rnd() - 0.5) * cw * 0.7, topY, pz + (rnd() - 0.5) * cd * 0.7, 1);
      if (rnd() < 0.16) {
        box(ctx, "plain", cw * 0.8, 0.3, cd * 0.8, px, topY, pz + cd * 0.25, ctx.jc(0x8a867f, 0.2), {
          rx: 0.3 + rnd() * 0.5,
          ry: (rnd() - 0.5) * 0.6,
          bottom: true,
        });
      }
      if (sk === 0 && !half && rnd() < 0.3) {
        box(ctx, "plain", 1.6 + rnd() * 1.5, 1.2, 1.6 + rnd(), px + (rnd() - 0.5) * 2, topY, pz + (rnd() - 0.5) * 2, ctx.jc(0x6c7072, 0.3), {
          ry: rnd() * 3,
        });
      }
      if (rnd() < 0.4) {
        box(ctx, "plain", 0.8 + rnd(), 0.5, 0.8 + rnd(), px + (rnd() - 0.5) * cw * 0.6, topY, pz + (rnd() - 0.5) * cd * 0.6, ctx.jc(pick(rnd, RUBBLE), 0.3), {
          rx: rnd(),
          ry: rnd() * 3,
        });
      }
    }
  }

  ctx.base = null;

  // обломки вокруг основания
  const nRub = 3 + Math.floor(rnd() * 6 * (0.4 + D));
  for (let i = 0; i < nRub; i++) {
    let rx = 0;
    let rz = 0;
    for (let t = 0; t < 5; t++) {
      rx = (rnd() - 0.5) * (w + 9);
      rz = (rnd() - 0.5) * (d + 9);
      if (Math.abs(rx) > w / 2 || Math.abs(rz) > d / 2) break;
    }
    const s = 0.5 + rnd() * 2;
    box(ctx, "plain", s, s * 0.7, s, cx + rx, -0.1, cz + rz, ctx.jc(pick(rnd, RUBBLE), 0.3), {
      rx: rnd() * 3,
      ry: rnd() * 3,
      rz: rnd() * 3,
    });
  }
  if (half || rnd() < 0.3) {
    const sx = rnd() < 0.5 ? -1 : 1;
    pile(ctx, cx + sx * (w / 2 + 1.5), cz + (rnd() - 0.5) * d, 4.5, 10, 1.8);
  }
  if (rnd() < 0.05 + 0.1 * D) fire(ctx, cx + (w / 2 + 2) * (rnd() < 0.5 ? 1 : -1), cz + (rnd() - 0.5) * d * 0.6, fires);

  colliders.push({ x: cx, z: cz, hw: w / 2, hd: d / 2, h: maxTop });
}

/* ------------------------------ город ------------------------------ */

export function generateCity(opts: CityOptions, mats: CityMaterials): CityData {
  const rnd = mulberry32(Math.floor(opts.seed) * 9973 + 17);
  const ctx = new Ctx(rnd);
  const N = opts.blocks;
  const D = opts.destruction;

  const rS: number[] = [];
  const rE: number[] = [];
  const bS: number[] = [];
  let cur = 0;
  for (let i = 0; i <= N; i++) {
    rS[i] = cur;
    cur += roadWidth(i);
    rE[i] = cur;
    if (i < N) {
      bS[i] = cur;
      cur += BLOCK;
    }
  }
  const total = cur;
  const off = -total / 2;
  const half = total / 2;
  for (let i = 0; i <= N; i++) {
    rS[i] += off;
    rE[i] += off;
    if (i < N) bS[i] += off;
  }
  const roadC = (i: number) => (rS[i] + rE[i]) / 2;
  const inRoad = (v: number, margin = 0) => {
    for (let i = 0; i <= N; i++) if (v > rS[i] - margin && v < rE[i] + margin) return true;
    return false;
  };

  const colliders: Collider[] = [];
  const fires: THREE.Vector3[] = [];
  const craters: { x: number; z: number; r: number }[] = [];
  const blocks: BlockInfo[] = [];

  const roadPoint = () => {
    const vertical = rnd() < 0.5;
    const i = ri(rnd, 0, N);
    const c = roadC(i);
    const w = rE[i] - rS[i];
    const t = (rnd() - 0.5) * (total - 12);
    const lat = (rnd() - 0.5) * (w - 4);
    return vertical ? { x: c + lat, z: t, yaw: Math.PI / 2, w } : { x: t, z: c + lat, yaw: 0, w };
  };

  /* --- разметка дорог --- */
  for (let i = 0; i <= N; i++) {
    const avenue = i % 4 === 0;
    const c = roadC(i);
    const markCol = avenue ? 0xb8a24a : 0xcfcfcf;
    for (let t = -half + 2; t < half - 4; t += 8) {
      if (inRoad(t + 1.5, 1.5) || inRoad(t - 1.5, 1.5)) continue;
      if (rnd() < 0.22) continue;
      const col = ctx.jc(markCol, 0.5);
      if (avenue) {
        ctx.at(c, t);
        flat(ctx, 0.18, 3, c - 0.25, 0.12, t + 1.5, 0, col);
        flat(ctx, 0.18, 3, c + 0.25, 0.12, t + 1.5, 0, col);
        ctx.at(t, c);
        flat(ctx, 3, 0.18, t + 1.5, 0.12, c - 0.25, 0, col);
        flat(ctx, 3, 0.18, t + 1.5, 0.12, c + 0.25, 0, col);
      } else {
        ctx.at(c, t);
        flat(ctx, 0.18, 3, c, 0.12, t + 1.5, 0, col);
        ctx.at(t, c);
        flat(ctx, 3, 0.18, t + 1.5, 0.12, c, 0, col);
      }
    }
  }
  // пятна на асфальте (трещины, копоть, грязь)
  const patchCols = [0x222224, 0x3a3733, 0x4b4339, 0x1a1a1b, 0x55504a];
  for (let k = 0; k < N * N * 7; k++) {
    const p = roadPoint();
    ctx.at(p.x, p.z);
    flat(ctx, 1.5 + rnd() * 6, 1.5 + rnd() * 6, p.x, 0.04 + rnd() * 0.04, p.z, rnd() * 3, ctx.jc(pick(rnd, patchCols), 0.3));
  }

  /* --- кварталы --- */
  let buildings = 0;
  for (let ix = 0; ix < N; ix++) {
    for (let iz = 0; iz < N; iz++) {
      const x0 = bS[ix];
      const z0 = bS[iz];
      const cx = x0 + BLOCK / 2;
      const cz = z0 + BLOCK / 2;
      const dist = Math.hypot(cx, cz) / half;
      const kr = rnd();
      let kind: BlockInfo["kind"] = "city";
      if (dist > 0.12 && kr < 0.07) kind = "park";
      else if (kr < 0.12) kind = "crater";
      else if (kr < 0.12 + 0.07 * D) kind = "rubble";
      blocks.push({ x0, z0, x1: x0 + BLOCK, z1: z0 + BLOCK, kind });

      ctx.at(cx, cz);
      box(ctx, "plain", BLOCK, 0.18, BLOCK, cx, 0, cz, ctx.jc(0x6e6b66, 0.2));

      // фонари и деревья по периметру
      for (let i = 0; i < 2; i++) {
        const side = ri(rnd, 0, 3);
        const t = (rnd() - 0.5) * (BLOCK - 6);
        const lx = side === 0 ? x0 + 1 : side === 1 ? x0 + BLOCK - 1 : cx + t;
        const lz = side === 2 ? z0 + 1 : side === 3 ? z0 + BLOCK - 1 : cz + t;
        lamp(ctx, lx, lz, 0.18);
      }
      if (rnd() < 0.5) {
        const side = ri(rnd, 0, 3);
        const t = (rnd() - 0.5) * (BLOCK - 6);
        const tx = side === 0 ? x0 + 1.6 : side === 1 ? x0 + BLOCK - 1.6 : cx + t;
        const tz = side === 2 ? z0 + 1.6 : side === 3 ? z0 + BLOCK - 1.6 : cz + t;
        deadTree(ctx, tx, tz, 0.18);
      }

      if (kind === "park") {
        box(ctx, "plain", BLOCK - 8, 0.12, BLOCK - 8, cx, 0.18, cz, ctx.jc(0x5d5a3a, 0.2));
        for (let i = 0; i < 14; i++) deadTree(ctx, cx + (rnd() - 0.5) * (BLOCK - 12), cz + (rnd() - 0.5) * (BLOCK - 12), 0.3);
        for (let i = 0; i < 3; i++) pile(ctx, cx + (rnd() - 0.5) * 40, cz + (rnd() - 0.5) * 40, 3, 8, 1.2, 0.3);
        if (rnd() < 0.6) crater(ctx, cx + (rnd() - 0.5) * 20, cz + (rnd() - 0.5) * 20, 5 + rnd() * 4, 0.3);
        continue;
      }
      if (kind === "crater") {
        const R = 10 + rnd() * 8;
        crater(ctx, cx, cz, R, 0.18);
        craters.push({ x: cx, z: cz, r: R * 1.4 });
        for (let i = 0; i < 4; i++) pile(ctx, cx + (rnd() - 0.5) * 50, cz + (rnd() - 0.5) * 50, 4, 12, 2, 0.18);
        for (let i = 0; i < 3; i++) car(ctx, cx + (rnd() - 0.5) * 44, cz + (rnd() - 0.5) * 44, rnd() * 6, 0.18);
        if (rnd() < 0.6) fire(ctx, cx + R * 0.9, cz, fires, 0.5);
        continue;
      }
      if (kind === "rubble") {
        for (let i = 0; i < 7; i++) {
          rubbleLot(ctx, cx + (rnd() - 0.5) * 44, cz + (rnd() - 0.5) * 44, 14, 14, fires);
        }
        continue;
      }

      // городской квартал: делим на участки
      const inner = BLOCK - 8;
      const nx = pick(rnd, [1, 2, 2, 3]);
      const nz = pick(rnd, [2, 2, 3]);
      const gap = 3;
      const split = (n: number) => {
        const wts = Array.from({ length: n }, () => 0.7 + rnd() * 0.6);
        const sum = wts.reduce((s, v) => s + v, 0);
        const avail = inner - gap * (n - 1);
        let pos = 0;
        return wts.map((wt) => {
          const size = (avail * wt) / sum;
          const r = { start: pos, size };
          pos += size + gap;
          return r;
        });
      };
      const sx = split(nx);
      const sz = split(nz);
      const baseH = dist < 0.3 ? 40 + rnd() * 70 : dist < 0.65 ? 18 + rnd() * 35 : 7 + rnd() * 17;
      for (const a of sx) {
        for (const b of sz) {
          const lw = a.size - 1 - rnd() * 2;
          const ld = b.size - 1 - rnd() * 2;
          const lx = x0 + 4 + a.start + a.size / 2;
          const lz = z0 + 4 + b.start + b.size / 2;
          const h = baseH * (0.6 + rnd() * 0.8);
          const floors = Math.max(2, Math.round(h / FL));
          building(ctx, lx, lz, lw, ld, floors, D, colliders, fires);
          buildings++;
        }
      }
    }
  }

  /* --- улицы: машины, баррикады, завалы, воронки --- */
  for (let i = 0; i < N * N * 2; i++) {
    const p = roadPoint();
    car(ctx, p.x, p.z, p.yaw + (rnd() < 0.5 ? 0 : Math.PI) + (rnd() - 0.5) * 0.5);
  }
  for (let i = 0; i < N * N * 0.8; i++) {
    const p = roadPoint();
    barrier(ctx, p.x, p.z, p.yaw);
  }
  for (let i = 0; i < N * N * 1.2; i++) {
    const p = roadPoint();
    ctx.at(p.x, p.z);
    pile(ctx, p.x, p.z, 3 + rnd() * 3, 10 + Math.floor(rnd() * 8), 1.8);
  }
  for (let i = 0; i < N * N * 0.3; i++) {
    const p = roadPoint();
    const R = 4 + rnd() * 6;
    crater(ctx, p.x, p.z, R, 0);
    craters.push({ x: p.x, z: p.z, r: R * 1.4 });
    if (rnd() < 0.4) fire(ctx, p.x + R * 0.8, p.z, fires, 0.3);
  }
  for (let i = 0; i < N * N * 0.15; i++) {
    const p = roadPoint();
    ctx.at(p.x, p.z);
    box(ctx, "plain", p.w * 0.7, 0.8, 8, p.x, 0, p.z, ctx.jc(0x7d7a74, 0.3), {
      rx: 0.25 + rnd() * 0.2,
      ry: p.yaw + Math.PI / 2 + (rnd() - 0.5) * 0.5,
    });
    pile(ctx, p.x, p.z, 5, 12, 2);
  }
  for (let i = 0; i < N * N * 0.15; i++) {
    const p = roadPoint();
    fire(ctx, p.x, p.z, fires, 0.3);
  }

  /* --- точки спавна на перекрёстках --- */
  const spawns: CityData["spawns"] = [];
  for (let i = 0; i < 12; i++) {
    spawns.push({ x: roadC(ri(rnd, 0, N)), z: roadC(ri(rnd, 0, N)), yaw: rnd() * Math.PI * 2 });
  }

  /* --- сборка мешей --- */
  const group = new THREE.Group();
  group.name = "RuinedCity";
  let triangles = 0;
  let vertices = 0;

  // земля
  const ground = new THREE.PlaneGeometry(total, total);
  ground.rotateX(-Math.PI / 2);
  paint(ground, 0x2c2c2e);
  const gm = new THREE.Mesh(ground, mats.plain);
  gm.name = "Asphalt";
  gm.userData.noCull = true;
  group.add(gm);
  const terr = new THREE.PlaneGeometry(9000, 9000);
  terr.rotateX(-Math.PI / 2);
  terr.translate(0, -0.25, 0);
  paint(terr, 0x3d372f);
  const tm = new THREE.Mesh(terr, mats.plain);
  tm.name = "Wasteland";
  tm.userData.noCull = true;
  group.add(tm);

  const matFor = (k: string) => (k === "solid" ? mats.solid : k === "fire" ? mats.fire : mats.plain);
  for (const [key, geos] of ctx.batches) {
    const [kind, kx, kz] = key.split("|");
    const merged = geos.length === 1 ? geos[0] : mergeGeometries(geos, false);
    if (geos.length > 1) geos.forEach((g) => g.dispose());
    if (!merged) continue;
    merged.computeBoundingSphere();
    const mesh = new THREE.Mesh(merged, matFor(kind));
    mesh.name = `${kind}_${kx}_${kz}`;
    group.add(mesh);
    vertices += merged.getAttribute("position").count;
    triangles += (merged.index ? merged.index.count : merged.getAttribute("position").count) / 3;
  }
  ctx.batches.clear();

  return {
    group,
    options: opts,
    colliders,
    fires,
    craters,
    spawns,
    blocks,
    half,
    stats: { buildings, triangles, vertices, props: ctx.props, craters: craters.length, fires: fires.length },
  };
}

function paint(g: THREE.BufferGeometry, hex: number) {
  const c = new THREE.Color(hex);
  const n = g.getAttribute("position").count;
  const col = new Float32Array(n * 3);
  for (let i = 0; i < n; i++) {
    col[i * 3] = c.r;
    col[i * 3 + 1] = c.g;
    col[i * 3 + 2] = c.b;
  }
  g.setAttribute("color", new THREE.BufferAttribute(col, 3));
  const uv = g.getAttribute("uv");
  for (let i = 0; i < uv.count; i++) uv.setXY(i, 0.02, 0.02);
}
