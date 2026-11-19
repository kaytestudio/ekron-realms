import * as THREE from 'three';
import { Batch } from './batch';
import { Colliders } from './buildings';
import { Rng, pick, range } from './rng';
import { FireSpec, addBox, addCyl, mat } from './vehicles';

export interface PropCtx {
  scene: THREE.Group;
  rng: Rng;
  colliders: Colliders;
  fires: FireSpec[];
  sandbags: Batch;
  addRubble: (x: number, z: number, scale: number, rng: Rng, y?: number, dark?: number) => void;
  blob: THREE.Texture;
}

const bagColors = ['#8b7d5a', '#7d7050', '#6e6446', '#9a8c66', '#5d5a40'];

/** Стена из мешков с песком */
export function sandbagWall(c: PropCtx, x: number, z: number, len: number, rot: number, rows = 3, curve = 0) {
  const { rng, sandbags } = c;
  const n = Math.ceil(len / 0.95);
  const cos = Math.cos(rot);
  const sin = Math.sin(rot);
  for (let r = 0; r < rows; r++) {
    for (let i = 0; i < n; i++) {
      const t = (i - n / 2 + (r % 2) * 0.5) * 0.95;
      const lz = Math.sin((i / n) * Math.PI) * curve;
      const px = x + cos * t - sin * lz;
      const pz = z - sin * t - cos * lz;
      if (rng() < 0.05 + r * 0.06) continue; // выбитые мешки
      sandbags.add(px, 0.17 + r * 0.32, pz, 1, 1, 1, range(rng, -0.05, 0.05), rot + range(rng, -0.1, 0.1), range(rng, -0.05, 0.05), pick(rng, bagColors));
    }
  }
  const steps = Math.ceil(len / 2);
  for (let i = 0; i < steps; i++) {
    const t = (i - steps / 2 + 0.5) * 2;
    const lz = Math.sin(((i + 0.5) / steps) * Math.PI) * curve;
    c.colliders.circles.push({ x: x + cos * t - sin * lz, z: z - sin * t - cos * lz, r: 1.1 });
  }
}

/** Бетонные блоки-«джерси» */
export function barriers(c: PropCtx, x: number, z: number, count: number, rot: number, gap = 0.1) {
  const m = mat('#7b7870', 0.95, 0);
  const cos = Math.cos(rot);
  const sin = Math.sin(rot);
  for (let i = 0; i < count; i++) {
    const t = (i - count / 2) * (3 + gap);
    const g = new THREE.Group();
    addBox(g, 0.6, 1.0, 3, m, 0, 0.5, 0);
    addBox(g, 0.95, 0.35, 3, m, 0, 0.17, 0);
    const bx = x + cos * t;
    const bz = z - sin * t;
    g.position.set(bx, 0, bz);
    g.rotation.y = rot + range(c.rng, -0.12, 0.12);
    if (c.rng() < 0.2) {
      g.rotation.z = range(c.rng, 0.15, 0.4);
      g.position.y = 0.2;
    }
    c.scene.add(g);
    c.colliders.circles.push({ x: bx, z: bz, r: 1.5 });
  }
}

/** Противотанковые ежи */
export function hedgehogs(c: PropCtx, x: number, z: number, count: number, spread: number) {
  const m = mat('#5a3a26', 0.95, 0.3);
  for (let i = 0; i < count; i++) {
    const g = new THREE.Group();
    addBox(g, 0.16, 0.16, 2.1, m, 0, 0.7, 0, 0.7, 0, 0);
    addBox(g, 0.16, 0.16, 2.1, m, 0, 0.7, 0, -0.7, 0.8, 0.5);
    addBox(g, 2.1, 0.16, 0.16, m, 0, 0.7, 0, 0.3, 0.2, 0.7);
    const hx = x + range(c.rng, -spread, spread);
    const hz = z + range(c.rng, -spread / 3, spread / 3);
    g.position.set(hx, 0, hz);
    g.rotation.y = c.rng() * 6;
    c.scene.add(g);
    c.colliders.circles.push({ x: hx, z: hz, r: 0.9 });
  }
}

/** Колючая проволока (спираль) */
export function barbedWire(c: PropCtx, x: number, z: number, len: number, rot: number) {
  const pts: THREE.Vector3[] = [];
  const turns = len * 2.4;
  for (let i = 0; i <= turns * 8; i++) {
    const a = i / 8;
    const t = (a / turns - 0.5) * len;
    pts.push(new THREE.Vector3(t, 0.55 + Math.cos(a * 2.4) * 0.35, Math.sin(a * 2.4) * 0.35));
  }
  const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), new THREE.LineBasicMaterial({ color: 0x1a1a18 }));
  line.position.set(x, 0, z);
  line.rotation.y = rot;
  c.scene.add(line);
  // колья
  for (let i = 0; i <= Math.floor(len / 2.5); i++) {
    const t = (i * 2.5 - len / 2) * 1;
    const post = addBox(c.scene, 0.07, 1.1, 0.07, mat('#2b2018'), x + Math.cos(rot) * t, 0.55, z - Math.sin(rot) * t, range(c.rng, -0.2, 0.2), 0, range(c.rng, -0.2, 0.2));
    post.castShadow = false;
  }
}

export function drum(c: PropCtx, x: number, z: number, burning = false, tipped = false) {
  const col = pick(c.rng, ['#6d3b1e', '#3d4a2a', '#2f3b4f', '#7a2c20', '#4a4a48']);
  const g = new THREE.Group();
  addCyl(g, 0.3, 0.9, mat(col, 0.7, 0.5), 0, 0.45, 0, 'y', 0.3, true);
  addCyl(g, 0.31, 0.05, mat('#222'), 0, 0.2, 0, 'y', 0.31, true);
  addCyl(g, 0.31, 0.05, mat('#222'), 0, 0.7, 0, 'y', 0.31, true);
  g.position.set(x, 0, z);
  if (tipped) {
    g.rotation.z = Math.PI / 2;
    g.position.y = 0.3;
    g.rotation.y = c.rng() * 6;
  }
  c.scene.add(g);
  if (burning) {
    c.fires.push({ x, y: 1.1, z, kind: 'fire', size: 1.0 });
    c.fires.push({ x, y: 1.5, z, kind: 'smoke', size: 1.8 });
  }
  c.colliders.circles.push({ x, z, r: 0.35 });
}

export function crates(c: PropCtx, x: number, z: number, n: number) {
  const wood = mat('#6b5a3c', 0.95, 0);
  const olive = mat('#434a2e', 0.9, 0.1);
  for (let i = 0; i < n; i++) {
    const s = range(c.rng, 0.6, 1.1);
    const cx = x + range(c.rng, -1.4, 1.4);
    const cz = z + range(c.rng, -1.4, 1.4);
    addBox(c.scene, s * 1.3, s, s, c.rng() > 0.5 ? wood : olive, cx, s / 2, cz, 0, c.rng() * 3, 0);
    if (c.rng() > 0.6) addBox(c.scene, s, s * 0.8, s, wood, cx, s + s * 0.4, cz, 0, c.rng() * 3, 0);
    c.colliders.circles.push({ x: cx, z: cz, r: 0.7 });
  }
}

export function deadTree(c: PropCtx, x: number, z: number) {
  const m = mat('#241e19', 1, 0);
  const g = new THREE.Group();
  const h = range(c.rng, 4, 7);
  const trunk = addCyl(g, 0.1, h, m, 0, h / 2, 0, 'y', 0.28, true);
  trunk.rotation.z = range(c.rng, -0.12, 0.12);
  const br = 4 + Math.floor(c.rng() * 4);
  for (let i = 0; i < br; i++) {
    const y = h * range(c.rng, 0.45, 0.95);
    const l = range(c.rng, 1.2, 2.6);
    const b = addCyl(g, 0.02, l, m, 0, y, 0, 'y', 0.07, true);
    b.rotation.set(range(c.rng, -1.1, 1.1), c.rng() * 6, range(c.rng, 0.5, 1.2) * (c.rng() > 0.5 ? 1 : -1));
    b.position.x += Math.sin(b.rotation.z) * -l * 0.4;
  }
  g.position.set(x, 0, z);
  c.scene.add(g);
  c.colliders.circles.push({ x, z, r: 0.35 });
}

/** Столбы ЛЭП с провисшими проводами, часть лежит на земле */
export function powerLine(c: PropCtx, axis: 'x' | 'z', fixed: number, from: number, to: number, step: number) {
  const m = mat('#3b2f25', 1, 0);
  const posts: THREE.Vector3[] = [];
  for (let t = from; t <= to; t += step) {
    const g = new THREE.Group();
    const fallen = c.rng() < 0.15;
    addCyl(g, 0.14, 9, m, 0, 4.5, 0, 'y', 0.2, true);
    addBox(g, 2.4, 0.14, 0.14, m, 0, 8.3, 0);
    addBox(g, 2.0, 0.12, 0.12, m, 0, 7.4, 0);
    const px = axis === 'x' ? t : fixed;
    const pz = axis === 'x' ? fixed : t;
    g.position.set(px, 0, pz);
    g.rotation.y = axis === 'x' ? Math.PI / 2 : 0;
    if (fallen) {
      g.rotation.x = 1.45 * (c.rng() > 0.5 ? 1 : -1);
      g.position.y = 0.25;
    } else {
      g.rotation.z = range(c.rng, -0.07, 0.07);
      posts.push(new THREE.Vector3(px, 0, pz));
    }
    c.scene.add(g);
  }
  const wireMat = new THREE.LineBasicMaterial({ color: 0x0c0c0c });
  for (let i = 0; i < posts.length - 1; i++) {
    const a = posts[i];
    const b = posts[i + 1];
    if (a.distanceTo(b) > step * 1.2) continue;
    for (const off of [-1.0, 0, 1.0]) {
      const pts: THREE.Vector3[] = [];
      for (let k = 0; k <= 12; k++) {
        const u = k / 12;
        const sag = Math.sin(u * Math.PI) * 1.3;
        const lx = a.x + (b.x - a.x) * u;
        const lz = a.z + (b.z - a.z) * u;
        pts.push(new THREE.Vector3(lx + (axis === 'x' ? 0 : off), 8.3 - sag, lz + (axis === 'x' ? off : 0)));
      }
      c.scene.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), wireMat));
    }
  }
}

export function tent(c: PropCtx, x: number, z: number, rot: number, collapsed = false) {
  const m = mat(pick(c.rng, ['#4b5232', '#5a5a3a', '#6a6446']), 1, 0);
  const g = new THREE.Group();
  const body = new THREE.Mesh(new THREE.ConeGeometry(3.2, 2.4, 4), m);
  body.rotation.y = Math.PI / 4;
  body.scale.set(1.3, collapsed ? 0.5 : 1, 1);
  body.position.y = collapsed ? 0.6 : 1.2;
  body.castShadow = true;
  body.receiveShadow = true;
  g.add(body);
  addBox(g, 0.5, 1.2, 0.05, mat('#0a0a09'), 1.8, 0.8, 1.4, 0, 0.5, 0);
  g.position.set(x, 0, z);
  g.rotation.y = rot;
  c.scene.add(g);
  c.colliders.circles.push({ x, z, r: 2.4 });
}

export function watchtower(c: PropCtx, x: number, z: number) {
  const m = mat('#3d3426', 0.95, 0);
  const roof = mat('#3a3d2b', 0.95, 0.1);
  const g = new THREE.Group();
  for (const sx of [-1, 1]) {
    for (const sz of [-1, 1]) {
      addBox(g, 0.22, 6.2, 0.22, m, sx * 1.2, 3.1, sz * 1.2, sz * 0.08, 0, -sx * 0.08);
    }
  }
  for (const y of [1.5, 3.5]) {
    addBox(g, 2.6, 0.12, 0.12, m, 0, y, 1.2);
    addBox(g, 2.6, 0.12, 0.12, m, 0, y, -1.2);
    addBox(g, 0.12, 0.12, 2.6, m, 1.2, y, 0);
    addBox(g, 0.12, 0.12, 2.6, m, -1.2, y, 0);
  }
  addBox(g, 3.2, 0.2, 3.2, m, 0, 6.2, 0);
  addBox(g, 3.2, 0.9, 0.1, m, 0, 6.75, 1.55);
  addBox(g, 0.1, 0.9, 3.2, m, 1.55, 6.75, 0);
  addBox(g, 0.1, 0.9, 3.2, m, -1.55, 6.75, 0);
  addBox(g, 3.6, 0.12, 3.6, roof, 0, 8.4, 0, 0.1, 0, 0.12);
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) addBox(g, 0.12, 2.1, 0.12, m, sx * 1.6, 7.3, sz * 1.6);
  g.position.set(x, 0, z);
  g.rotation.set(0.05, 0.5, 0.08);
  c.scene.add(g);
  c.colliders.circles.push({ x, z, r: 1.8 });
}

/** Центральная площадь: разбитый фонтан и памятник */
export function monument(c: PropCtx, x: number, z: number) {
  const stone = mat('#7d7a72', 0.9, 0);
  const dark = mat('#2b2a27', 1, 0);
  const g = new THREE.Group();
  addCyl(g, 4.2, 0.7, stone, 0, 0.35, 0, 'y', 4.4, true);
  addCyl(g, 3.6, 0.72, mat('#0b0d0e', 0.3, 0.5), 0, 0.37, 0, 'y', 3.6, true);
  addBox(g, 3, 1.6, 3, stone, 0, 1.3, 0);
  addBox(g, 2.2, 1.2, 2.2, stone, 0, 2.7, 0, 0, 0.1, 0);
  addBox(g, 1.5, 3.2, 1.5, dark, 0.2, 4.6, 0, 0, 0.2, 0.18); // обломок колонны
  // статуя — только ступни и голени
  addBox(g, 0.5, 1.0, 0.7, mat('#4c4a45', 0.7, 0.3), -0.5, 3.8, 0);
  addBox(g, 0.5, 1.0, 0.7, mat('#4c4a45', 0.7, 0.3), 0.5, 3.8, 0);
  // упавшая часть
  const fall = new THREE.Group();
  addBox(fall, 1.0, 2.6, 0.9, mat('#4c4a45', 0.7, 0.3), 0, 0, 0);
  addBox(fall, 1.5, 0.8, 0.9, mat('#4c4a45', 0.7, 0.3), 0, 1.7, 0);
  fall.position.set(5.5, 0.55, 3.5);
  fall.rotation.set(0.1, 0.8, Math.PI / 2 - 0.1);
  g.add(fall);
  g.position.set(x, 0, z);
  c.scene.add(g);
  c.colliders.circles.push({ x, z, r: 4.6 });
  c.fires.push({ x: x + 0.2, y: 6.2, z, kind: 'smoke', size: 2.4 });
}

/** Валики воронки: неровное кольцо земли и обломков */
export function craterRim(c: PropCtx, x: number, z: number, r: number) {
  const geo = new THREE.TorusGeometry(r, Math.max(0.5, r * 0.09), 5, 28);
  const m = mat('#3b342b', 1, 0);
  const t = new THREE.Mesh(geo, m);
  t.rotation.x = Math.PI / 2;
  t.scale.set(1, 1, 0.55);
  t.position.set(x, 0.1, z);
  t.receiveShadow = true;
  t.castShadow = true;
  c.scene.add(t);
  const n = Math.floor(r * 5);
  for (let i = 0; i < n; i++) {
    const a = c.rng() * Math.PI * 2;
    const rr = r * range(c.rng, 0.95, 1.8);
    c.addRubble(x + Math.cos(a) * rr, z + Math.sin(a) * rr, range(c.rng, 0.2, 0.7), c.rng, 0, 0.05);
  }
}

/** Тёмное пятно копоти на земле */
export function scorch(c: PropCtx, x: number, z: number, r: number, rot = 0, stretch = 1.5) {
  const m = new THREE.MeshBasicMaterial({ map: c.blob, color: 0x000000, transparent: true, opacity: 0.7, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4 });
  const p = new THREE.Mesh(new THREE.PlaneGeometry(r * 2, r * 2 * stretch), m);
  p.rotation.set(-Math.PI / 2, 0, rot);
  p.position.set(x, 0.04, z);
  c.scene.add(p);
}
