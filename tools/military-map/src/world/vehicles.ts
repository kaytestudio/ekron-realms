import * as THREE from 'three';
import { Rng, pick, range } from './rng';

const matCache = new Map<string, THREE.MeshStandardMaterial>();
export function mat(color: string | number | THREE.Color, rough = 0.85, metal = 0.15) {
  const col = new THREE.Color(color);
  const key = `${col.getHexString()}-${rough}-${metal}`;
  let m = matCache.get(key);
  if (!m) {
    m = new THREE.MeshStandardMaterial({ color: col, roughness: rough, metalness: metal });
    matCache.set(key, m);
  }
  return m;
}

const boxGeo = new THREE.BoxGeometry(1, 1, 1);
const cylGeo = new THREE.CylinderGeometry(1, 1, 1, 14);
const cylGeo10 = new THREE.CylinderGeometry(1, 1, 1, 10);

export function addBox(
  parent: THREE.Object3D,
  w: number,
  h: number,
  d: number,
  m: THREE.Material,
  x: number,
  y: number,
  z: number,
  rx = 0,
  ry = 0,
  rz = 0
) {
  const mesh = new THREE.Mesh(boxGeo, m);
  mesh.scale.set(w, h, d);
  mesh.position.set(x, y, z);
  mesh.rotation.set(rx, ry, rz);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  parent.add(mesh);
  return mesh;
}

/** Цилиндр: r — радиус, h — высота, axis — вдоль какой оси лежит */
export function addCyl(
  parent: THREE.Object3D,
  rTop: number,
  h: number,
  m: THREE.Material,
  x: number,
  y: number,
  z: number,
  axis: 'x' | 'y' | 'z' = 'y',
  rBottom = rTop,
  low = false
) {
  const mesh = new THREE.Mesh(low ? cylGeo10 : cylGeo, m);
  if (rBottom !== rTop) {
    mesh.geometry = new THREE.CylinderGeometry(rTop, rBottom, 1, low ? 10 : 14);
    mesh.scale.set(1, h, 1);
  } else {
    mesh.scale.set(rTop, h, rTop);
  }
  mesh.position.set(x, y, z);
  if (axis === 'x') mesh.rotation.z = Math.PI / 2;
  if (axis === 'z') mesh.rotation.x = Math.PI / 2;
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  parent.add(mesh);
  return mesh;
}

export interface FireSpec {
  x: number;
  y: number;
  z: number;
  kind: 'fire' | 'smoke';
  size: number;
}

export interface VehicleResult {
  group: THREE.Group;
  fires: FireSpec[];
  radius: number;
}

// ───────────────────────────── ТАНК ─────────────────────────────
export type TankVariant = 'burnt' | 'turretless' | 'tilted' | 'drooping' | 'intact';
export type Paint = 'olive' | 'desert' | 'grey';

const PAINT: Record<Paint, string> = { olive: '#4b5232', desert: '#8c7b56', grey: '#585d5f' };

export function buildTank(rng: Rng, variant: TankVariant, paint: Paint): VehicleResult {
  const g = new THREE.Group();
  const fires: FireSpec[] = [];
  const burnt = variant === 'burnt';
  const baseCol = new THREE.Color(PAINT[paint]);
  const scorch = (k: number) => baseCol.clone().multiplyScalar(k);
  const hullMat = mat(burnt ? '#1d1b18' : scorch(range(rng, 0.55, 0.9)), 0.9, 0.2);
  const turMat = mat(burnt ? '#171614' : scorch(range(rng, 0.45, 0.85)), 0.9, 0.2);
  const darkMat = mat('#161614', 0.95, 0.1);
  const trackMat = mat('#1f1e1b', 0.98, 0.1);
  const metalMat = mat('#2e2d2a', 0.6, 0.5);
  const rustMat = mat('#6a3a1e', 0.95, 0.1);
  const charMat = mat('#0a0908', 1, 0);

  const body = new THREE.Group();
  g.add(body);

  // гусеницы и катки
  for (const s of [-1, 1]) {
    const broken = variant === 'tilted' && s === 1;
    if (!broken) {
      addBox(body, 0.85, 1.0, 7.1, trackMat, s * 1.5, 0.62, 0);
      addBox(body, 0.12, 0.35, 7.3, hullMat, s * 1.95, 1.08, 0);
    } else {
      addBox(body, 0.4, 0.5, 6.4, darkMat, s * 1.35, 0.5, 0);
    }
    for (let i = 0; i < 6; i++) {
      const z = -2.7 + i * 1.08;
      addCyl(body, 0.46, 0.34, metalMat, s * 1.95, 0.52, z, 'x', 0.46, true);
    }
    addCyl(body, 0.52, 0.36, metalMat, s * 1.95, 0.85, 3.35, 'x', 0.52, true);
    addCyl(body, 0.5, 0.36, metalMat, s * 1.95, 0.8, -3.4, 'x', 0.5, true);
    addBox(body, 1.0, 0.1, 7.3, hullMat, s * 1.5, 1.22, 0);
  }
  if (variant === 'tilted') {
    // сорванная гусеница, расползшаяся по земле
    addBox(g, 0.85, 0.08, 4.5, trackMat, 3.7, 0.06, 1.2, 0, 0.25, 0);
    addBox(g, 0.85, 0.08, 2.2, trackMat, 4.6, 0.06, -2.2, 0, -0.5, 0);
    addBox(g, 0.85, 0.5, 0.9, trackMat, 3.3, 0.3, 3.6, 0.5, 0.2, 0);
  }

  // корпус
  addBox(body, 2.3, 0.9, 6.0, hullMat, 0, 1.0, 0);
  addBox(body, 2.3, 0.12, 1.8, hullMat, 0, 1.28, 3.1, 0.5, 0, 0); // лоб
  addBox(body, 2.3, 0.8, 0.12, hullMat, 0, 1.0, -3.02);
  addBox(body, 2.1, 0.3, 1.9, darkMat, 0, 1.6, -2.1); // решётка МТО
  addBox(body, 0.5, 0.25, 0.4, metalMat, 0.6, 1.62, 1.6); // люк мех-вода
  // бочки на корме и ЗИП
  addCyl(body, 0.22, 1.1, rustMat, 0.9, 1.75, -2.9, 'z', 0.22, true);
  addBox(body, 0.9, 0.3, 0.5, rustMat, -0.7, 1.75, -2.6);

  // башня
  const makeTurret = () => {
    const t = new THREE.Group();
    addCyl(t, 1.15, 0.7, turMat, 0, 0.35, 0, 'y', 1.3, true);
    addBox(t, 1.8, 0.62, 1.3, turMat, 0, 0.36, 1.0);
    addBox(t, 1.9, 0.5, 1.0, turMat, 0, 0.36, -1.2);
    addBox(t, 0.8, 0.55, 0.4, turMat, 0, 0.4, 1.75);
    const barrel = new THREE.Group();
    barrel.position.set(0, 0.4, 1.9);
    const droop = variant === 'drooping' ? 0.28 : variant === 'burnt' ? -0.15 : range(rng, -0.12, 0.08);
    barrel.rotation.x = droop;
    addCyl(barrel, 0.12, 4.4, metalMat, 0, 0, 2.2, 'z', 0.12, true);
    addCyl(barrel, 0.2, 0.45, metalMat, 0, 0, 4.4, 'z', 0.2, true);
    if (variant === 'drooping' || variant === 'tilted') {
      // оторванный кусок ствола
      addCyl(barrel, 0.13, 0.7, charMat, 0, 0, 4.7, 'z', 0.13, true).rotation.y = 0.6;
    }
    t.add(barrel);
    addCyl(t, 0.32, 0.14, metalMat, -0.45, 0.76, -0.1, 'y', 0.32, true);
    addBox(t, 0.25, 0.2, 0.5, darkMat, 0.5, 0.82, 0.1);
    addCyl(t, 0.04, 0.8, metalMat, 0.5, 1.2, 0.1, 'y', 0.04, true);
    return t;
  };

  if (variant === 'turretless') {
    addCyl(body, 1.05, 0.06, charMat, 0, 1.5, 0.4, 'y', 1.05, true);
    const ring = addCyl(body, 1.2, 0.12, rustMat, 0, 1.47, 0.4, 'y', 1.2, true);
    ring.scale.y = 0.12;
    const tur = makeTurret();
    const side = rng() > 0.5 ? 1 : -1;
    tur.position.set(side * range(rng, 4.5, 6.5), 0.95, range(rng, -1, 3));
    tur.rotation.set(0.1, range(rng, 0, 6), Math.PI * 0.95);
    g.add(tur);
    fires.push({ x: 0, y: 1.8, z: 0.4, kind: 'fire', size: 2.2 });
    fires.push({ x: 0, y: 2.0, z: 0.4, kind: 'smoke', size: 3.2 });
    // обломки
    for (let i = 0; i < 6; i++) {
      addBox(g, range(rng, 0.2, 0.7), range(rng, 0.1, 0.35), range(rng, 0.3, 0.9), darkMat, range(rng, -4, 4), 0.15, range(rng, -5, 5), range(rng, 0, 3), range(rng, 0, 3), range(rng, 0, 3));
    }
  } else {
    const tur = makeTurret();
    tur.position.set(0, 1.45, 0.4);
    tur.rotation.y = variant === 'tilted' ? 0.9 : variant === 'burnt' ? -0.4 : range(rng, -0.7, 0.7);
    body.add(tur);
    if (burnt) {
      fires.push({ x: 0, y: 2.4, z: 0.2, kind: 'fire', size: 1.6 });
      fires.push({ x: 0, y: 2.6, z: -2.1, kind: 'fire', size: 1.8 });
      fires.push({ x: 0, y: 3.0, z: 0.2, kind: 'smoke', size: 3.4 });
    }
    if (variant === 'tilted') {
      body.rotation.z = 0.14;
      body.position.y = 0.12;
      fires.push({ x: 0, y: 2.0, z: -2.1, kind: 'smoke', size: 2.6 });
    }
    if (variant === 'drooping') fires.push({ x: 0, y: 2.6, z: 0.2, kind: 'smoke', size: 2.4 });
  }

  // пробоины: тёмные пятна
  addBox(body, 0.04, 0.35, 0.35, charMat, 1.17, 1.05, range(rng, -1, 1.5));
  addBox(body, 0.04, 0.3, 0.3, charMat, -1.17, 1.0, range(rng, -1, 1.5));
  return { group: g, fires, radius: 3.6 };
}

// ───────────────────────────── ЛЕГКОВУШКИ ─────────────────────────────
export interface CarOpts {
  kind: 'sedan' | 'van' | 'suv';
  color: string;
  burnt: boolean;
  flipped: boolean;
}

export function buildCar(rng: Rng, o: CarOpts): VehicleResult {
  const root = new THREE.Group();
  const g = new THREE.Group();
  root.add(g);
  const fires: FireSpec[] = [];
  const col = o.burnt ? '#1a1816' : o.color;
  const body = mat(col, 0.75, 0.35);
  const glass = mat('#08090b', 0.2, 0.6);
  const rim = mat('#4a4743', 0.6, 0.6);
  const tire = mat('#101010', 0.95, 0);
  const rust = mat('#5a3320', 0.95, 0.1);
  const L = o.kind === 'van' ? 5.0 : o.kind === 'suv' ? 4.7 : 4.3;

  addBox(g, 1.85, 0.55, L, body, 0, 0.65, 0);
  if (o.kind === 'van') {
    addBox(g, 1.85, 1.0, L * 0.72, body, 0, 1.4, -0.5);
    addBox(g, 1.87, 0.45, L * 0.7, glass, 0, 1.55, -0.45);
  } else {
    const cl = o.kind === 'suv' ? 2.8 : 2.1;
    addBox(g, 1.6, 0.08, cl, body, 0, 1.5, -0.3);
    addBox(g, 1.64, 0.4, cl - 0.12, glass, 0, 1.28, -0.3);
    addBox(g, 1.5, 0.55, cl - 0.2, o.burnt ? mat('#0c0b0a') : body, 0, 1.18, -0.3).scale.x = 1.5;
  }
  // колёса — часть отсутствует
  const wheelPos: [number, number][] = [
    [-0.95, L * 0.32],
    [0.95, L * 0.32],
    [-0.95, -L * 0.32],
    [0.95, -L * 0.32],
  ];
  wheelPos.forEach(([x, z]) => {
    const missing = rng() < 0.22;
    if (missing) {
      addBox(g, 0.35, 0.18, 0.5, rust, x * 0.85, 0.2, z, 0, 0, 0.1);
    } else {
      addCyl(g, 0.35, 0.24, o.burnt ? rim : tire, x, 0.35, z, 'x', 0.35, true);
      addCyl(g, 0.18, 0.26, rim, x, 0.35, z, 'x', 0.18, true);
    }
  });
  // открытый капот / дверь / багажник
  if (rng() > 0.45) addBox(g, 1.7, 0.06, 1.2, body, 0, 1.3, L * 0.42, -1.1, 0, 0);
  if (rng() > 0.5) addBox(g, 0.06, 0.55, 1.1, body, 1.08, 0.85, 0.2, 0, 0.9, 0);
  if (rng() > 0.6) addBox(g, 1.6, 0.06, 0.9, body, 0, 1.2, -L * 0.5, 0.9, 0, 0);
  // вмятина-пробоина
  addBox(g, 0.5, 0.3, 0.05, mat('#0a0908'), range(rng, -0.5, 0.5), 0.8, L / 2 + 0.01);
  if (o.burnt) {
    fires.push({ x: 0, y: 1.2, z: L * 0.25, kind: rng() > 0.55 ? 'fire' : 'smoke', size: 1.2 });
  }
  if (o.flipped) {
    g.rotation.z = Math.PI * (rng() > 0.5 ? 1 : -1) * 0.97;
    g.position.y = 1.45;
    fires.forEach((f) => (f.y = 0.8));
  } else {
    g.rotation.z = range(rng, -0.04, 0.04);
  }
  return { group: root, fires, radius: 2.5 };
}

// ───────────────────────────── ГРУЗОВИК ─────────────────────────────
export function buildTruck(rng: Rng, burnt: boolean): VehicleResult {
  const g = new THREE.Group();
  const fires: FireSpec[] = [];
  const cab = mat(burnt ? '#1b1a17' : '#4a5233', 0.85, 0.2);
  const dark = mat('#141412');
  const tire = mat('#0f0f0f', 0.95, 0);
  const glass = mat('#08090b', 0.2, 0.6);
  const wood = mat(burnt ? '#1a140e' : '#5d4a33', 0.95, 0);
  const tarp = mat(burnt ? '#1b1a16' : '#575a3d', 1, 0);

  addBox(g, 2.1, 0.45, 8.0, dark, 0, 1.0, 0);
  addBox(g, 2.4, 1.7, 2.1, cab, 0, 2.05, 2.9); // кабина
  addBox(g, 2.42, 0.6, 2.0, glass, 0, 2.5, 3.0);
  addBox(g, 2.3, 1.0, 1.6, cab, 0, 1.55, 4.5); // капот
  addBox(g, 2.3, 0.2, 4.8, wood, 0, 1.3, -1.8); // кузов
  for (const s of [-1, 1]) {
    addBox(g, 0.1, 0.9, 4.8, wood, s * 1.2, 1.8, -1.8, 0, 0, s * 0.05);
  }
  addBox(g, 2.4, 0.9, 0.1, wood, 0, 1.8, -4.2);
  // каркас/тент
  if (!burnt && rng() > 0.4) {
    addBox(g, 2.4, 1.5, 4.7, tarp, 0, 2.5, -1.8, 0, 0, 0.04);
    addBox(g, 1.2, 0.8, 1.6, mat('#0a0a09'), 0.9, 2.6, -3.6, 0.2, 0.2, 0.3); // дыра
  } else {
    for (let i = 0; i < 5; i++) addBox(g, 2.3, 0.07, 0.07, dark, 0, 2.5, -4 + i * 1.1);
    addBox(g, 0.07, 1.4, 0.07, dark, 1.15, 2.0, -4);
    addBox(g, 0.07, 1.4, 0.07, dark, -1.15, 2.0, -4);
    for (let i = 0; i < 4; i++) addBox(g, range(rng, 0.4, 0.9), 0.4, 0.5, mat('#6b5a3c'), range(rng, -0.8, 0.8), 1.6, range(rng, -3.5, -0.5), 0, range(rng, 0, 3), 0);
  }
  for (const s of [-1, 1]) {
    [3.4, -0.9, -2.7].forEach((z, i) => {
      if (rng() > 0.1 || i === 0) {
        addCyl(g, 0.58, 0.45, tire, s * 1.15, 0.58, z, 'x', 0.58, true);
        addCyl(g, 0.28, 0.48, dark, s * 1.15, 0.58, z, 'x', 0.28, true);
      }
    });
  }
  if (burnt) {
    fires.push({ x: 0, y: 2.4, z: -1.5, kind: 'fire', size: 2.0 });
    fires.push({ x: 0, y: 3.0, z: -1.5, kind: 'smoke', size: 3.0 });
  }
  return { group: g, fires, radius: 4 };
}

// ───────────────────────────── АВТОБУС ─────────────────────────────
export function buildBus(rng: Rng): VehicleResult {
  const root = new THREE.Group();
  const g = new THREE.Group();
  root.add(g);
  const fires: FireSpec[] = [];
  const burnt = rng() > 0.4;
  const body = mat(burnt ? '#1c1a17' : pick(rng, ['#8a2e22', '#b0a037', '#35566e']), 0.8, 0.25);
  const glass = mat('#07080a', 0.2, 0.6);
  const tire = mat('#0f0f0f', 0.95, 0);
  const rust = mat('#5a3320', 0.95, 0.1);
  addBox(g, 2.6, 3.0, 11.0, body, 0, 2.0, 0);
  addBox(g, 2.63, 0.95, 10.4, glass, 0, 2.55, 0);
  addBox(g, 2.62, 0.25, 11.02, rust, 0, 1.05, 0);
  addBox(g, 2.4, 0.08, 10.8, mat('#3c3a36'), 0, 3.55, 0);
  for (const s of [-1, 1]) {
    [3.6, -3.4, -4.4].forEach((z) => {
      addCyl(g, 0.6, 0.45, tire, s * 1.3, 0.6, z, 'x', 0.6, true);
    });
  }
  // дверь вырвана
  addBox(g, 0.08, 2.0, 1.0, body, 1.55, 1.8, 3.2, 0, 1.2, 0);
  g.rotation.set(range(rng, -0.02, 0.02), 0, range(rng, 0.03, 0.09));
  g.position.y = 0.05;
  if (burnt) {
    fires.push({ x: 0, y: 3.8, z: 1, kind: 'fire', size: 2.4 });
    fires.push({ x: 0, y: 4.4, z: 1, kind: 'smoke', size: 3.6 });
    fires.push({ x: 0, y: 3.8, z: -3, kind: 'smoke', size: 3 });
  }
  return { group: root, fires, radius: 6 };
}
