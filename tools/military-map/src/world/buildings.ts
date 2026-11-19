import * as THREE from 'three';
import { Batch } from './batch';
import { Rng, clamp, gauss, pick, range } from './rng';
import { wallTexture } from './textures';
import { FireSpec } from './vehicles';

export interface BoxCollider {
  minX: number;
  maxX: number;
  minZ: number;
  maxZ: number;
}
export interface CircleCollider {
  x: number;
  z: number;
  r: number;
}
export interface Colliders {
  boxes: BoxCollider[];
  circles: CircleCollider[];
}
export interface Lot {
  x: number;
  z: number;
  nx: number;
  nz: number;
  floors: number;
  collapsed: boolean;
}
export interface BuildingRect {
  x: number;
  z: number;
  w: number;
  d: number;
  damage: number;
}

const CW = 3; // ширина ячейки стены
const CH = 3.2; // высота этажа
const T = 0.5; // толщина стены

export interface RubbleSink {
  rubble: Batch;
  chunks: Batch;
  rebar: Batch;
}

export function createBuildingKit() {
  const sets = [
    { brick: false, win: true },
    { brick: true, win: true },
    { brick: false, win: false },
    { brick: true, win: false },
  ].map((o, i) => ({ ...o, batch: new Batch(), tex: wallTexture(o.brick, o.win, 100 + i) }));

  const slabs = new Batch();
  const pillars = new Batch();
  const rubble = new Batch();
  const chunks = new Batch();
  const rebar = new Batch();
  const rects: BuildingRect[] = [];

  function addRubble(x: number, z: number, scale: number, rng: Rng, y = 0, dark = 0) {
    const g = 0.22 + rng() * 0.25 - dark;
    const warm = rng() > 0.6;
    const c = new THREE.Color(warm ? g + 0.08 : g, warm ? g * 0.85 : g, warm ? g * 0.72 : g * 0.95);
    rubble.add(x, y + scale * 0.25, z, scale * range(rng, 0.8, 1.5), scale * range(rng, 0.5, 1), scale * range(rng, 0.8, 1.5), rng() * 3, rng() * 6, rng() * 3, c);
  }

  function addBuilding(lot: Lot, rng: Rng, colliders: Colliders, fires: FireSpec[]) {
    const W = lot.nx * CW;
    const D = lot.nz * CW;
    const F = lot.floors;
    const brick = rng() > 0.55;
    const tint = new THREE.Color().setHSL(range(rng, 0.06, 0.12), range(rng, 0.05, 0.25), range(rng, 0.62, 0.95));

    // точки попаданий
    const blasts = Array.from({ length: lot.collapsed ? 3 : 1 + (rng() > 0.45 ? 1 : 0) + (rng() > 0.8 ? 1 : 0) }, () => ({
      x: range(rng, -W / 2 - 2, W / 2 + 2),
      z: range(rng, -D / 2 - 2, D / 2 + 2),
      r: lot.collapsed ? range(rng, 14, 20) : range(rng, 6, 14),
    }));
    const damageAt = (x: number, z: number) => {
      let d = 0;
      for (const b of blasts) d += Math.max(0, 1 - Math.hypot(x - b.x, z - b.z) / b.r);
      return clamp(d, 0, 1);
    };
    const capAt = (x: number, z: number) => {
      const dmg = damageAt(x, z);
      let cap = Math.round(F * (1 - 0.96 * dmg));
      if (rng() < 0.3) cap += rng() < 0.5 ? -1 : 1;
      return clamp(cap, 0, F);
    };

    let totalDmg = 0;
    let n = 0;

    const sides = [
      { cnt: lot.nx, pos: (i: number) => [-W / 2 + CW / 2 + i * CW, -D / 2], rot: 0, ew: false },
      { cnt: lot.nx, pos: (i: number) => [-W / 2 + CW / 2 + i * CW, D / 2], rot: Math.PI, ew: false },
      { cnt: lot.nz, pos: (i: number) => [-W / 2, -D / 2 + CW / 2 + i * CW], rot: Math.PI / 2, ew: true },
      { cnt: lot.nz, pos: (i: number) => [W / 2, -D / 2 + CW / 2 + i * CW], rot: -Math.PI / 2, ew: true },
    ];

    for (const s of sides) {
      for (let i = 0; i < s.cnt; i++) {
        const [lx, lz] = s.pos(i);
        const dmg = damageAt(lx, lz);
        totalDmg += dmg;
        n++;
        const cap = capAt(lx, lz);
        const wx = lot.x + lx;
        const wz = lot.z + lz;
        for (let f = 0; f < cap; f++) {
          if (f > 0 && rng() < 0.05 + 0.3 * dmg) continue;
          if (f === 0 && rng() < 0.12 * dmg) continue;
          const set = sets[(brick ? 1 : 0) + (rng() < (f === 0 ? 0.55 : 0.8) ? 0 : 2)];
          const top = f === cap - 1 && cap < F;
          const light = (1 - 0.55 * dmg * rng()) * (f === 0 ? 0.85 : 1);
          const col = tint.clone().multiplyScalar(light);
          const tilt = top ? range(rng, -0.1, 0.1) : 0;
          const sy = top ? range(rng, 0.7, 1) : 1;
          set.batch.add(wx, f * CH + (CH * sy) / 2 + 0.12, wz, 1, sy, 1, top ? range(rng, -0.06, 0.06) : 0, s.rot, tilt, col);
          if (f === 0) {
            colliders.boxes.push(
              s.ew
                ? { minX: wx - 0.4, maxX: wx + 0.4, minZ: wz - CW / 2, maxZ: wz + CW / 2 }
                : { minX: wx - CW / 2, maxX: wx + CW / 2, minZ: wz - 0.4, maxZ: wz + 0.4 }
            );
          }
        }
        // арматура торчит из сломанных стен
        if (cap > 0 && cap < F && rng() < 0.55) {
          for (let k = 0; k < 4; k++) {
            const h = range(rng, 0.8, 2.6);
            rebar.add(wx + range(rng, -1.2, 1.2), cap * CH + 0.1 + h / 2, wz + range(rng, -0.2, 0.2), 1, h, 1, range(rng, -0.5, 0.5), 0, range(rng, -0.5, 0.5), '#5b3a22');
          }
        }
        // обломки у основания
        if (dmg > 0.2) {
          const cnt = Math.floor(3 + dmg * 7);
          const outX = s.ew ? Math.sign(lx) : 0;
          const outZ = s.ew ? 0 : Math.sign(lz);
          for (let k = 0; k < cnt; k++) {
            const dist = Math.abs(gauss(rng)) * 3.5 - 0.5;
            addRubble(wx + outX * dist + (s.ew ? 0 : range(rng, -1.6, 1.6)), wz + outZ * dist + (s.ew ? range(rng, -1.6, 1.6) : 0), range(rng, 0.25, 1.15) * (0.7 + dmg), rng);
          }
        }
      }
    }

    // перекрытия и колонны
    for (let ix = 0; ix < lot.nx; ix++) {
      for (let iz = 0; iz < lot.nz; iz++) {
        const lx = -W / 2 + CW / 2 + ix * CW;
        const lz = -D / 2 + CW / 2 + iz * CW;
        const cap = capAt(lx, lz);
        const wx = lot.x + lx;
        const wz = lot.z + lz;
        for (let f = 1; f <= Math.min(cap + 1, F); f++) {
          if (f > cap && rng() > 0.35) continue;
          if (rng() < 0.2) continue;
          const col = new THREE.Color().setHSL(0.09, 0.05, range(rng, 0.14, 0.28));
          const hanging = f >= cap && cap < F && rng() < 0.7;
          if (hanging) {
            slabs.add(wx + range(rng, -0.4, 0.4), f * CH - range(rng, 0.2, 0.9), wz + range(rng, -0.4, 0.4), CW * range(rng, 0.7, 1), 0.3, CW * range(rng, 0.7, 1), range(rng, -0.6, 0.6), rng() * 3, range(rng, -0.6, 0.6), col);
            rebar.add(wx + range(rng, -1, 1), f * CH - 0.7, wz + range(rng, -1, 1), 1, range(rng, 1, 2.2), 1, range(rng, -0.8, 0.8), 0, range(rng, -0.8, 0.8), '#5b3a22');
          } else {
            slabs.add(wx, f * CH + 0.12, wz, CW * 0.99, 0.3, CW * 0.99, 0, 0, 0, col);
          }
        }
        // внутренние колонны
        if (ix > 0 && iz > 0 && rng() < 0.45) {
          const h = Math.max(1, cap * CH - range(rng, 0, 1.2));
          pillars.add(wx - CW / 2, h / 2 + 0.12, wz - CW / 2, 0.55, h, 0.55, 0, 0, 0, new THREE.Color().setHSL(0.09, 0.04, range(rng, 0.25, 0.4)));
          if (cap < F) {
            rebar.add(wx - CW / 2 + range(rng, -0.1, 0.1), h + 0.7, wz - CW / 2, 1, 1.6, 1, range(rng, -0.3, 0.3), 0, range(rng, -0.3, 0.3), '#5b3a22');
          }
          colliders.circles.push({ x: wx - CW / 2, z: wz - CW / 2, r: 0.4 });
        }
      }
    }

    // фундамент
    slabs.add(lot.x, 0.06, lot.z, W + 0.4, 0.12, D + 0.4, 0, 0, 0, new THREE.Color(0x3a3834));
    // мусор внутри
    const inside = Math.floor(10 + (totalDmg / Math.max(1, n)) * 40);
    for (let i = 0; i < inside; i++) {
      addRubble(lot.x + range(rng, -W / 2 + 1, W / 2 - 1), lot.z + range(rng, -D / 2 + 1, D / 2 - 1), range(rng, 0.2, 0.9), rng, 0.1);
    }
    for (let i = 0; i < 4; i++) {
      chunks.add(lot.x + range(rng, -W / 2 - 3, W / 2 + 3), 0.3, lot.z + range(rng, -D / 2 - 3, D / 2 + 3), range(rng, 0.8, 2.4), range(rng, 0.25, 0.5), range(rng, 0.8, 1.8), range(rng, -0.4, 0.4), rng() * 6, range(rng, -0.4, 0.4), new THREE.Color().setHSL(0.09, 0.05, range(rng, 0.25, 0.42)));
    }

    const avg = totalDmg / Math.max(1, n);
    rects.push({ x: lot.x, z: lot.z, w: W, d: D, damage: avg });
    if (avg > 0.35 && rng() < 0.45) {
      fires.push({ x: lot.x + range(rng, -W / 3, W / 3), y: 0.8 + Math.floor(rng() * 2) * CH, z: lot.z + range(rng, -D / 3, D / 3), kind: 'fire', size: 2.4 });
      fires.push({ x: lot.x + range(rng, -W / 3, W / 3), y: 2 + CH * Math.min(2, F - 1), z: lot.z + range(rng, -D / 3, D / 3), kind: 'smoke', size: 3.6 });
    }
  }

  /** Полностью сложившийся дом — груда щебня */
  function addRuinMound(x: number, z: number, w: number, d: number, rng: Rng) {
    const cnt = Math.floor(w * d * 0.9);
    for (let i = 0; i < cnt; i++) {
      const px = x + range(rng, -w / 2, w / 2);
      const pz = z + range(rng, -d / 2, d / 2);
      const k = 1 - Math.max(Math.abs(px - x) / (w / 2), Math.abs(pz - z) / (d / 2));
      const s = range(rng, 0.4, 1.4);
      addRubble(px, pz, s, rng, k * 1.8 * rng(), 0.04);
    }
    for (let i = 0; i < 14; i++) {
      const px = x + range(rng, -w / 2, w / 2);
      const pz = z + range(rng, -d / 2, d / 2);
      const k = 1 - Math.max(Math.abs(px - x) / (w / 2), Math.abs(pz - z) / (d / 2));
      chunks.add(px, 0.4 + k * 1.4, pz, range(rng, 1, 3), range(rng, 0.3, 0.5), range(rng, 1, 2.4), range(rng, -0.5, 0.5), rng() * 6, range(rng, -0.5, 0.5), new THREE.Color().setHSL(0.09, 0.05, range(rng, 0.25, 0.45)));
    }
    for (let i = 0; i < 9; i++) {
      const h = range(rng, 1, 3.4);
      rebar.add(x + range(rng, -w / 2, w / 2) * 0.8, 1 + h / 2, z + range(rng, -d / 2, d / 2) * 0.8, 1, h, 1, range(rng, -0.7, 0.7), 0, range(rng, -0.7, 0.7), '#5b3a22');
    }
    // перемычки стен
    const set = sets[pick(rng, [2, 3])];
    for (let i = 0; i < 4; i++) {
      set.batch.add(x + range(rng, -w / 3, w / 3), 1.2, z + range(rng, -d / 3, d / 3), 1, 2.4, 1, range(rng, -0.5, 0.5), rng() * 6, range(rng, -0.6, 0.6), new THREE.Color(0.45, 0.43, 0.4));
    }
  }

  function build(parent: THREE.Object3D) {
    const sideMat = new THREE.MeshStandardMaterial({ color: 0x6d6a63, roughness: 1 });
    const cellGeo = new THREE.BoxGeometry(CW, CH, T);
    sets.forEach((s) => {
      const face = new THREE.MeshStandardMaterial({ map: s.tex, roughness: 0.95, metalness: 0 });
      s.batch.build(cellGeo, [sideMat, sideMat, sideMat, sideMat, face, face], parent, true, true);
    });
    const unit = new THREE.BoxGeometry(1, 1, 1);
    const concrete = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 1 });
    slabs.build(unit, concrete, parent, true, true);
    pillars.build(unit, concrete, parent, true, true);
    chunks.build(unit, concrete, parent, false, true);
    const rub = new THREE.IcosahedronGeometry(1, 0);
    rubble.build(rub, new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 1, flatShading: true }), parent, false, true);
    rebar.build(new THREE.CylinderGeometry(0.035, 0.035, 1, 5), new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.9, metalness: 0.4 }), parent, false, false);
  }

  return { addBuilding, addRuinMound, addRubble, build, rects, rubble, chunks, rebar };
}
