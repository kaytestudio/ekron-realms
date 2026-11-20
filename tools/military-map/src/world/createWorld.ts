import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { Ambience } from './audio';
import { Batch } from './batch';
import { BuildingRect, Colliders, Lot, createBuildingKit } from './buildings';
import { BLOCK_IDX, CRATERS, HALF, PITCH, POIS, STREETS } from './layout';
import { ParticleSystem } from './particles';
import { barbedWire, barriers, craterRim, crates, deadTree, drum, hedgehogs, monument, PropCtx, powerLine, sandbagWall, scorch, tent, watchtower } from './props';
import { Rng, clamp, makeRng, pick, range } from './rng';
import { blobTexture, cityGroundTexture, dirtTexture, glowSprite, smokeSprite } from './textures';
import { FireSpec, Paint, TankVariant, VehicleResult, buildBus, buildCar, buildTank, buildTruck } from './vehicles';

export type Mode = 'fps' | 'orbit';
export type TimeOfDay = 'dusk' | 'day' | 'night';

export interface MapVehicle {
  x: number;
  z: number;
  kind: 'tank' | 'car' | 'truck' | 'bus';
  rot: number;
}
export interface MapData {
  half: number;
  buildings: BuildingRect[];
  vehicles: MapVehicle[];
  fires: { x: number; z: number }[];
  craters: { x: number; z: number; r: number }[];
  stats: { tanks: number; cars: number; buildings: number; fires: number };
}
export interface WorldState {
  x: number;
  z: number;
  yaw: number;
  mode: Mode;
  locked: boolean;
  fps: number;
  speed: number;
}
export interface WorldHandle {
  state: WorldState;
  map: MapData;
  setTime(t: TimeOfDay): void;
  setMode(m: Mode): void;
  teleport(x: number, z: number, yaw?: number): void;
  lock(): void;
  startAudio(): void;
  setMuted(m: boolean): void;
  dispose(): void;
}

interface Preset {
  top: string;
  mid: string;
  hor: string;
  fog: string;
  fogD: number;
  sun: string;
  sunI: number;
  sunDir: [number, number, number];
  hemiSky: string;
  hemiGround: string;
  hemiI: number;
  exposure: number;
  fireI: number;
  smoke: number;
}

const PRESETS: Record<TimeOfDay, Preset> = {
  dusk: { top: '#2b2e36', mid: '#6b5348', hor: '#e07a35', fog: '#6a5043', fogD: 0.0105, sun: '#ff9448', sunI: 3.2, sunDir: [0.75, 0.2, -0.5], hemiSky: '#8a8f9c', hemiGround: '#3d2d22', hemiI: 0.75, exposure: 1.0, fireI: 1, smoke: 0.14 },
  day: { top: '#545e69', mid: '#8a8d90', hor: '#b9b3a3', fog: '#98948a', fogD: 0.0085, sun: '#fff0da', sunI: 3.4, sunDir: [0.4, 0.72, 0.35], hemiSky: '#a9b2bc', hemiGround: '#5a5046', hemiI: 1.05, exposure: 1.0, fireI: 0.55, smoke: 0.11 },
  night: { top: '#03050a', mid: '#0a0f17', hor: '#1c2534', fog: '#090d14', fogD: 0.0165, sun: '#7c9ad0', sunI: 0.6, sunDir: [-0.5, 0.55, 0.4], hemiSky: '#2c3a58', hemiGround: '#0d0d12', hemiI: 0.5, exposure: 1.15, fireI: 1.9, smoke: 0.22 },
};

interface Emitter extends FireSpec {
  acc: number;
  far?: boolean;
}

export function createWorld(container: HTMLElement): WorldHandle {
  const W = () => container.clientWidth || window.innerWidth;
  const H = () => container.clientHeight || window.innerHeight;

  // ───────── рендерер ─────────
  const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
  renderer.setSize(W(), H());
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.domElement.style.display = 'block';
  renderer.domElement.style.outline = 'none';
  renderer.domElement.tabIndex = 0;
  container.appendChild(renderer.domElement);

  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0x6a5043, 0.0105);
  const camera = new THREE.PerspectiveCamera(68, W() / H(), 0.1, 2200);
  camera.rotation.order = 'YXZ';

  // ───────── небо ─────────
  const skyMat = new THREE.ShaderMaterial({
    side: THREE.BackSide,
    depthWrite: false,
    fog: false,
    uniforms: {
      uTop: { value: new THREE.Color() },
      uMid: { value: new THREE.Color() },
      uHor: { value: new THREE.Color() },
      uSunCol: { value: new THREE.Color() },
      uSunDir: { value: new THREE.Vector3(1, 0.2, 0) },
    },
    vertexShader: `varying vec3 vDir; void main(){ vDir = position; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }`,
    fragmentShader: `
      varying vec3 vDir;
      uniform vec3 uTop; uniform vec3 uMid; uniform vec3 uHor; uniform vec3 uSunCol; uniform vec3 uSunDir;
      float h(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }
      float n(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.-2.*f);
        return mix(mix(h(i),h(i+vec2(1,0)),f.x), mix(h(i+vec2(0,1)),h(i+vec2(1,1)),f.x), f.y); }
      float fbm(vec2 p){ float v=0.; float a=.5; for(int i=0;i<5;i++){ v+=a*n(p); p*=2.02; a*=.5; } return v; }
      void main(){
        vec3 d = normalize(vDir);
        float y = clamp(d.y, 0., 1.);
        vec3 col = mix(uHor, uMid, smoothstep(0., .2, y));
        col = mix(col, uTop, smoothstep(.15, .85, y));
        float sd = max(dot(d, normalize(uSunDir)), 0.);
        col += uSunCol * (pow(sd, 6.) * .32 + pow(sd, 220.) * 1.4);
        vec2 cp = d.xz / (d.y + .28) * 1.7;
        float c = fbm(cp + vec2(3.1, 1.7));
        c = smoothstep(.3, .78, c) * smoothstep(-.02, .14, d.y);
        col = mix(col, col * .38 + uMid * .22, c * .85);
        gl_FragColor = vec4(col, 1.);
        #include <tonemapping_fragment>
        #include <colorspace_fragment>
      }`,
  });
  const sky = new THREE.Mesh(new THREE.SphereGeometry(1800, 32, 16), skyMat);
  sky.frustumCulled = false;
  scene.add(sky);

  // ───────── свет ─────────
  const hemi = new THREE.HemisphereLight(0xffffff, 0x333333, 0.8);
  scene.add(hemi);
  const sun = new THREE.DirectionalLight(0xffffff, 3);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  sun.shadow.camera.near = 1;
  sun.shadow.camera.far = 420;
  sun.shadow.bias = -0.0004;
  sun.shadow.normalBias = 0.06;
  scene.add(sun, sun.target);
  const fireLights: THREE.PointLight[] = [];
  for (let i = 0; i < 6; i++) {
    const l = new THREE.PointLight(0xff8a3a, 0, 30, 2);
    l.visible = false;
    scene.add(l);
    fireLights.push(l);
  }

  // ───────── земля ─────────
  const cityTex = cityGroundTexture(2048);
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(HALF * 2, HALF * 2), new THREE.MeshStandardMaterial({ map: cityTex, roughness: 0.95, metalness: 0 }));
  ground.rotation.x = -Math.PI / 2;
  ground.receiveShadow = true;
  scene.add(ground);
  const dirt = new THREE.Mesh(new THREE.PlaneGeometry(4000, 4000), new THREE.MeshStandardMaterial({ map: dirtTexture(), roughness: 1 }));
  dirt.material.map!.repeat.set(160, 160);
  dirt.rotation.x = -Math.PI / 2;
  dirt.position.y = -0.03;
  dirt.receiveShadow = true;
  scene.add(dirt);

  // ───────── мир ─────────
  const world = new THREE.Group();
  scene.add(world);
  const rng: Rng = makeRng(20240229);
  const colliders: Colliders = { boxes: [], circles: [] };
  const fires: FireSpec[] = [];
  const kit = createBuildingKit();
  const sandbags = new Batch();
  const blob = blobTexture();
  const ctx: PropCtx = { scene: world, rng, colliders, fires, sandbags, addRubble: kit.addRubble, blob };
  const mapVehicles: MapVehicle[] = [];
  const placed: { x: number; z: number; r: number }[] = [];

  // кварталы
  for (const i of BLOCK_IDX) {
    for (const j of BLOCK_IDX) {
      const bx = i * PITCH;
      const bz = j * PITCH;
      if (i === 0 && j === 0) continue; // площадь
      if (i === -2 && j === 2) continue; // парк
      if (i === 2 && j === -2) continue; // блокпост
      for (const sx of [-1, 1]) {
        for (const sz of [-1, 1]) {
          const r = rng();
          const lot: Lot = {
            x: bx + sx * 7.6 + range(rng, -0.6, 0.6),
            z: bz + sz * 7.6 + range(rng, -0.6, 0.6),
            nx: 3 + Math.floor(rng() * 2),
            nz: 3 + Math.floor(rng() * 2),
            floors: 3 + Math.floor(rng() * 5),
            collapsed: r < 0.2,
          };
          if (r > 0.94) {
            // пустырь с мусором
            for (let k = 0; k < 18; k++) kit.addRubble(lot.x + range(rng, -6, 6), lot.z + range(rng, -6, 6), range(rng, 0.2, 0.8), rng);
            continue;
          }
          if (r < 0.08) {
            kit.addRuinMound(lot.x, lot.z, lot.nx * 3, lot.nz * 3, rng);
            fires.push({ x: lot.x, y: 1.5, z: lot.z, kind: 'smoke', size: 3.4 });
            kit.rects.push({ x: lot.x, z: lot.z, w: lot.nx * 3, d: lot.nz * 3, damage: 1 });
            continue;
          }
          kit.addBuilding(lot, rng, colliders, fires);
        }
      }
    }
  }
  // воронки
  for (const c of CRATERS) craterRim(ctx, c.x, c.z, c.r);

  // площадь
  monument(ctx, 0, 0);
  drum(ctx, 8, -9, true);
  drum(ctx, 9, -10.2, false, true);
  crates(ctx, -7, 9, 3);

  // ───── техника ─────
  const isNearPoi = (x: number, z: number) => POIS.some((p) => Math.hypot(p.x - x, p.z - z) < 7);
  function place(res: VehicleResult, x: number, z: number, rot: number, kind: MapVehicle['kind'], burnt: boolean) {
    res.group.position.set(x, 0, z);
    res.group.rotation.y = rot;
    world.add(res.group);
    res.group.updateMatrixWorld(true);
    res.fires.forEach((f) => {
      const v = res.group.localToWorld(new THREE.Vector3(f.x, f.y, f.z));
      fires.push({ x: v.x, y: v.y, z: v.z, kind: f.kind, size: f.size });
    });
    colliders.circles.push({ x, z, r: res.radius * 0.75 });
    placed.push({ x, z, r: res.radius });
    mapVehicles.push({ x, z, kind, rot });
    if (burnt || kind === 'tank') scorch(ctx, x, z, res.radius * 1.15, rot, 1.5);
  }

  const tanks: { x: number; z: number; rot: number; variant: TankVariant; paint: Paint }[] = [
    { x: 12, z: -9, rot: 0.7, variant: 'turretless', paint: 'olive' },
    { x: -64, z: 23, rot: Math.PI / 2, variant: 'burnt', paint: 'olive' },
    { x: -44, z: 26, rot: Math.PI / 2 + 0.12, variant: 'drooping', paint: 'olive' },
    { x: -25, z: 22.5, rot: Math.PI / 2 - 0.2, variant: 'tilted', paint: 'olive' },
    { x: -102, z: 26, rot: Math.PI / 2 + 0.05, variant: 'burnt', paint: 'olive' },
    { x: 25, z: -52, rot: Math.PI + 0.1, variant: 'turretless', paint: 'grey' },
    { x: 22, z: -98, rot: 0.25, variant: 'intact', paint: 'grey' },
    { x: 66, z: 78, rot: 0.3, variant: 'burnt', paint: 'desert' },
    { x: -70, z: 70, rot: 2.2, variant: 'tilted', paint: 'desert' },
    { x: 120, z: -72, rot: 1.2, variant: 'burnt', paint: 'desert' },
    { x: -26, z: -75, rot: -1.0, variant: 'drooping', paint: 'grey' },
    { x: 92, z: -86, rot: 0.8, variant: 'tilted', paint: 'desert' },
    { x: -120, z: 84, rot: 0.1, variant: 'turretless', paint: 'olive' },
    { x: 70, z: 24, rot: -1.7, variant: 'burnt', paint: 'grey' },
  ];
  tanks.forEach((t) => place(buildTank(rng, t.variant, t.paint), t.x, t.z, t.rot, 'tank', t.variant !== 'intact'));

  const carColors = ['#7a2a22', '#2f4a63', '#c9c2b0', '#3f5a3a', '#a38a2a', '#505050', '#8c4a1a', '#2a2a2e'];
  function scatter(count: number, radius: number, make: () => { res: VehicleResult; kind: MapVehicle['kind']; burnt: boolean }) {
    let done = 0;
    let tries = 0;
    while (done < count && tries < count * 40) {
      tries++;
      const axisX = rng() > 0.5;
      const s = pick(rng, STREETS);
      const t = range(rng, -124, 124);
      const off = range(rng, -6.5, 6.5);
      const x = axisX ? t : s + off;
      const z = axisX ? s + off : t;
      if (placed.some((p) => Math.hypot(p.x - x, p.z - z) < p.r + radius + 0.6)) continue;
      if (isNearPoi(x, z) || Math.hypot(x - 24, z - 104) < 10) continue;
      let rot = axisX ? (off > 0 ? Math.PI / 2 : -Math.PI / 2) : off > 0 ? 0 : Math.PI;
      rot += range(rng, -0.3, 0.3) + (rng() < 0.12 ? range(rng, -1.2, 1.2) : 0);
      const m = make();
      place(m.res, x, z, rot, m.kind, m.burnt);
      done++;
    }
  }
  scatter(38, 2.5, () => {
    const burnt = rng() < 0.55;
    return { res: buildCar(rng, { kind: pick(rng, ['sedan', 'sedan', 'van', 'suv'] as const), color: pick(rng, carColors), burnt, flipped: rng() < 0.12 }), kind: 'car', burnt };
  });
  scatter(6, 4, () => {
    const burnt = rng() < 0.5;
    return { res: buildTruck(rng, burnt), kind: 'truck', burnt };
  });
  scatter(4, 6, () => ({ res: buildBus(rng), kind: 'bus', burnt: true }));

  // ───── баррикады ─────
  // юг: (72, 72)
  sandbagWall(ctx, 72, 90, 15, 0, 3, -1.8);
  hedgehogs(ctx, 72, 94, 6, 6.5);
  barriers(ctx, 72, 97, 4, 0);
  barbedWire(ctx, 72, 100, 14, 0);
  crates(ctx, 78, 84, 3);
  drum(ctx, 64, 84, true);
  // запад: (-72,-72)
  hedgehogs(ctx, -72, -60, 7, 6);
  barriers(ctx, -72, -64, 4, 0.1);
  sandbagWall(ctx, -78, -80, 10, Math.PI / 2, 3, 1.5);
  barbedWire(ctx, -72, -56, 14, 0);
  // восток: (120,24)
  barriers(ctx, 120, 4, 4, 0.15);
  hedgehogs(ctx, 120, 8, 5, 6);
  sandbagWall(ctx, 112, 12, 8, Math.PI / 2, 2, -1);
  // центр: (24,-24)
  barriers(ctx, 24, -6, 3, 0.05, 1.5);
  hedgehogs(ctx, 24, -12, 5, 5);
  drum(ctx, 30, -10, true);
  drum(ctx, 18, -8, false, true);
  // улицы
  sandbagWall(ctx, -24, 40, 9, 0.2, 2, 1);
  barriers(ctx, -48, -24, 3, Math.PI / 2 + 0.2);
  hedgehogs(ctx, -96, -24, 4, 6);

  // ───── парк (-96, 96) ─────
  for (let i = 0; i < 26; i++) deadTree(ctx, -96 + range(rng, -13, 13), 96 + range(rng, -13, 13));
  for (const z of [88, 100]) {
    sandbagWall(ctx, -96, z - 1.4, 18, 0, 2);
    sandbagWall(ctx, -96, z + 1.4, 18, 0, 2);
    const trench = new THREE.Mesh(new THREE.PlaneGeometry(18, 2.8), new THREE.MeshBasicMaterial({ color: 0x120e0a }));
    trench.rotation.x = -Math.PI / 2;
    trench.position.set(-96, 0.05, z);
    world.add(trench);
  }
  scorch(ctx, -102, 108, 5, 0.3, 1.2);
  craterRim(ctx, -102, 108, 3);
  scorch(ctx, -88, 78, 4, 0.3, 1);
  craterRim(ctx, -88, 78, 2.4);
  drum(ctx, -85, 96, true);
  crates(ctx, -108, 90, 3);

  // ───── блокпост (96, -96) ─────
  watchtower(ctx, 104, -104);
  sandbagWall(ctx, 104, -98, 8, 0, 3);
  sandbagWall(ctx, 98, -104, 8, Math.PI / 2, 3);
  tent(ctx, 86, -94, 0.2);
  tent(ctx, 92, -106, 0.9, true);
  tent(ctx, 84, -104, 1.4);
  tent(ctx, 104, -88, 0.5);
  crates(ctx, 96, -92, 5);
  drum(ctx, 108, -92, true);
  drum(ctx, 110, -94, false, true);
  hedgehogs(ctx, 96, -78, 5, 9);
  barbedWire(ctx, 96, -81, 24, 0);
  barriers(ctx, 96, -76, 5, 0);

  // ───── ЛЭП ─────
  powerLine(ctx, 'z', 18, -108, 108, 24);
  powerLine(ctx, 'x', -18, -108, 108, 24);
  powerLine(ctx, 'z', -66, -108, 108, 24);

  // ───── мусор на улицах ─────
  for (let i = 0; i < 160; i++) {
    const axisX = rng() > 0.5;
    const s = pick(rng, STREETS);
    const t = range(rng, -124, 124);
    const off = range(rng, -7.5, 7.5);
    kit.addRubble(axisX ? t : s + off, axisX ? s + off : t, range(rng, 0.12, 0.5), rng);
  }
  // окраины
  for (let i = 0; i < 220; i++) {
    const a = rng() * Math.PI * 2;
    const r = range(rng, 134, 260);
    kit.addRubble(Math.cos(a) * r, Math.sin(a) * r, range(rng, 0.3, 1.4), rng);
  }
  for (let i = 0; i < 45; i++) {
    const a = rng() * Math.PI * 2;
    const r = range(rng, 134, 230);
    deadTree(ctx, Math.cos(a) * r, Math.sin(a) * r);
  }
  for (let i = 0; i < 8; i++) {
    const a = rng() * Math.PI * 2;
    const r = range(rng, 138, 180);
    place(buildCar(rng, { kind: 'sedan', color: pick(rng, carColors), burnt: true, flipped: rng() < 0.3 }), Math.cos(a) * r, Math.sin(a) * r, rng() * 6, 'car', true);
  }
  // собираем все инстансы (здания, щебень, мешки)
  kit.build(world);
  sandbags.build(new THREE.BoxGeometry(0.9, 0.32, 0.5), new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 1 }), world, true, true);

  // ───────── частицы ─────────
  const smoke = new ParticleSystem(3600, smokeSprite(), false);
  const flame = new ParticleSystem(900, glowSprite(), true);
  scene.add(smoke.points, flame.points);

  const emitters: Emitter[] = fires.map((f) => ({ ...f, acc: rng() }));
  // далёкие дымовые столбы
  [
    [210, -150],
    [-230, -70],
    [70, 250],
    [-160, -240],
    [260, 120],
    [-250, 160],
  ].forEach(([x, z]) => emitters.push({ x, y: 0, z, kind: 'smoke', size: 16, acc: 0, far: true }));

  // пепел
  const ASH = 900;
  const ashPos = new Float32Array(ASH * 3);
  for (let i = 0; i < ASH; i++) {
    ashPos[i * 3] = (rng() - 0.5) * 50;
    ashPos[i * 3 + 1] = rng() * 22;
    ashPos[i * 3 + 2] = (rng() - 0.5) * 50;
  }
  const ashGeo = new THREE.BufferGeometry();
  ashGeo.setAttribute('position', new THREE.BufferAttribute(ashPos, 3));
  const ashMat = new THREE.PointsMaterial({ color: 0xb8b0a2, size: 0.07, transparent: true, opacity: 0.7, depthWrite: false, sizeAttenuation: true });
  const ash = new THREE.Points(ashGeo, ashMat);
  ash.frustumCulled = false;
  scene.add(ash);

  // ───────── карта для HUD ─────────
  const map: MapData = {
    half: HALF,
    buildings: kit.rects,
    vehicles: mapVehicles,
    fires: fires.filter((f) => f.kind === 'fire').map((f) => ({ x: f.x, z: f.z })),
    craters: CRATERS,
    stats: {
      tanks: mapVehicles.filter((v) => v.kind === 'tank').length,
      cars: mapVehicles.filter((v) => v.kind !== 'tank').length,
      buildings: kit.rects.length,
      fires: fires.filter((f) => f.kind === 'fire').length,
    },
  };

  // ───────── управление ─────────
  const spawn = POIS[0];
  const state: WorldState = { x: spawn.x, z: spawn.z, yaw: spawn.yaw, mode: 'orbit', locked: false, fps: 60, speed: 0 };
  let pitch = 0;
  const keys = new Set<string>();
  let dragging = false;
  let bob = 0;

  const orbit = new OrbitControls(camera, renderer.domElement);
  orbit.enableDamping = true;
  orbit.dampingFactor = 0.07;
  orbit.maxPolarAngle = 1.5;
  orbit.minDistance = 12;
  orbit.maxDistance = 420;
  orbit.autoRotate = true;
  orbit.autoRotateSpeed = 0.35;
  orbit.target.set(0, 6, 0);
  camera.position.set(125, 80, 150);
  orbit.update();
  orbit.addEventListener('start', () => (orbit.autoRotate = false));

  const lim = HALF + 12;
  function resolve(px: number, pz: number, r: number): [number, number] {
    for (let pass = 0; pass < 2; pass++) {
      for (const b of colliders.boxes) {
        if (px + r <= b.minX || px - r >= b.maxX || pz + r <= b.minZ || pz - r >= b.maxZ) continue;
        const cx = clamp(px, b.minX, b.maxX);
        const cz = clamp(pz, b.minZ, b.maxZ);
        const dx = px - cx;
        const dz = pz - cz;
        const d = Math.hypot(dx, dz);
        if (d > 1e-5) {
          if (d < r) {
            px += (dx / d) * (r - d);
            pz += (dz / d) * (r - d);
          }
        } else {
          const l = px - b.minX;
          const rr = b.maxX - px;
          const t = pz - b.minZ;
          const bt = b.maxZ - pz;
          const m = Math.min(l, rr, t, bt);
          if (m === l) px = b.minX - r;
          else if (m === rr) px = b.maxX + r;
          else if (m === t) pz = b.minZ - r;
          else pz = b.maxZ + r;
        }
      }
      for (const c of colliders.circles) {
        const dx = px - c.x;
        const dz = pz - c.z;
        const rr = c.r + r;
        if (Math.abs(dx) > rr || Math.abs(dz) > rr) continue;
        const d = Math.hypot(dx, dz);
        if (d < rr && d > 1e-5) {
          px += (dx / d) * (rr - d);
          pz += (dz / d) * (rr - d);
        }
      }
    }
    return [px, pz];
  }
  [state.x, state.z] = resolve(state.x, state.z, 0.5);

  const dom = renderer.domElement;
  const onMouseMove = (e: MouseEvent) => {
    if (state.mode !== 'fps') return;
    if (!state.locked && !dragging) return;
    state.yaw -= e.movementX * 0.0022;
    pitch = clamp(pitch - e.movementY * 0.0022, -1.45, 1.45);
  };
  const onDown = () => {
    if (state.mode === 'fps') dragging = true;
  };
  const onUp = () => (dragging = false);
  const onLockChange = () => {
    state.locked = document.pointerLockElement === dom;
    if (!state.locked) keys.clear();
  };
  const onKey = (e: KeyboardEvent) => {
    if ((e.target as HTMLElement)?.tagName === 'INPUT') return;
    if (e.type === 'keydown') {
      keys.add(e.code);
      if (e.code === 'KeyV') api.setMode(state.mode === 'fps' ? 'orbit' : 'fps');
      if (['Space', 'ArrowUp', 'ArrowDown'].includes(e.code) && state.mode === 'fps') e.preventDefault();
    } else keys.delete(e.code);
  };
  const onClick = () => {
    if (state.mode === 'fps' && !state.locked) api.lock();
  };
  window.addEventListener('mousemove', onMouseMove);
  dom.addEventListener('mousedown', onDown);
  window.addEventListener('mouseup', onUp);
  document.addEventListener('pointerlockchange', onLockChange);
  window.addEventListener('keydown', onKey);
  window.addEventListener('keyup', onKey);
  dom.addEventListener('click', onClick);
  const onBlur = () => keys.clear();
  window.addEventListener('blur', onBlur);

  const audio = new Ambience();

  // ───────── время суток ─────────
  let preset = PRESETS.dusk;
  function applyPreset(t: TimeOfDay) {
    preset = PRESETS[t];
    const p = preset;
    (scene.fog as THREE.FogExp2).color.set(p.fog);
    (scene.fog as THREE.FogExp2).density = p.fogD;
    skyMat.uniforms.uTop.value.set(p.top);
    skyMat.uniforms.uMid.value.set(p.mid);
    skyMat.uniforms.uHor.value.set(p.hor);
    skyMat.uniforms.uSunCol.value.set(p.sun);
    skyMat.uniforms.uSunDir.value.set(...p.sunDir);
    sun.color.set(p.sun);
    sun.intensity = p.sunI;
    hemi.color.set(p.hemiSky);
    hemi.groundColor.set(p.hemiGround);
    hemi.intensity = p.hemiI;
    renderer.toneMappingExposure = p.exposure;
    ashMat.opacity = t === 'night' ? 0.35 : 0.7;
  }
  applyPreset('dusk');

  // ───────── цикл ─────────
  const clock = new THREE.Clock();
  const sunDirV = new THREE.Vector3();
  const focus = new THREE.Vector3();
  const fwd = new THREE.Vector3();
  let raf = 0;
  let lightTimer = 0;
  let fpsAcc = 0;
  let fpsN = 0;
  let time = 0;

  function resize() {
    renderer.setSize(W(), H());
    camera.aspect = W() / H();
    camera.updateProjectionMatrix();
  }
  const ro = new ResizeObserver(resize);
  ro.observe(container);

  function frame() {
    raf = requestAnimationFrame(frame);
    const dt = Math.min(0.05, clock.getDelta());
    time += dt;
    fpsAcc += dt;
    fpsN++;
    if (fpsAcc > 0.5) {
      state.fps = Math.round(fpsN / fpsAcc);
      fpsAcc = 0;
      fpsN = 0;
    }

    if (state.mode === 'fps') {
      const f = (keys.has('KeyW') || keys.has('ArrowUp') ? 1 : 0) - (keys.has('KeyS') || keys.has('ArrowDown') ? 1 : 0);
      const s = (keys.has('KeyD') || keys.has('ArrowRight') ? 1 : 0) - (keys.has('KeyA') || keys.has('ArrowLeft') ? 1 : 0);
      if (keys.has('KeyQ')) state.yaw += 1.6 * dt;
      if (keys.has('KeyE')) state.yaw -= 1.6 * dt;
      const sprint = keys.has('ShiftLeft') || keys.has('ShiftRight');
      const sp = sprint ? 8.5 : 4.4;
      const len = Math.hypot(f, s) || 1;
      const sy = Math.sin(state.yaw);
      const cy = Math.cos(state.yaw);
      const vx = ((-sy * f + cy * s) / len) * sp;
      const vz = ((-cy * f - sy * s) / len) * sp;
      let nx = state.x + vx * dt;
      let nz = state.z + vz * dt;
      nx = clamp(nx, -lim, lim);
      nz = clamp(nz, -lim, lim);
      [nx, nz] = resolve(nx, nz, 0.5);
      const moved = Math.hypot(nx - state.x, nz - state.z);
      state.speed = moved / Math.max(dt, 1e-4);
      state.x = nx;
      state.z = nz;
      bob += moved * (sprint ? 1.7 : 2.3);
      camera.position.set(state.x, 1.72 + (moved > 0 ? Math.sin(bob) * 0.045 : 0), state.z);
      camera.rotation.set(pitch, state.yaw, 0);
      focus.set(state.x, 0, state.z);
    } else {
      orbit.update();
      camera.getWorldDirection(fwd);
      state.yaw = Math.atan2(-fwd.x, -fwd.z);
      state.x = orbit.target.x;
      state.z = orbit.target.z;
      state.speed = 0;
      focus.copy(orbit.target);
    }

    // солнце и тени следуют за фокусом
    sunDirV.set(...preset.sunDir).normalize();
    sun.position.copy(focus).addScaledVector(sunDirV, 160);
    sun.target.position.copy(focus);
    const ext = state.mode === 'fps' ? 70 : clamp(camera.position.distanceTo(focus) * 0.55, 60, 150);
    const sc = sun.shadow.camera;
    if (sc.right !== ext) {
      sc.left = -ext;
      sc.right = ext;
      sc.top = ext;
      sc.bottom = -ext;
      sc.updateProjectionMatrix();
    }
    sky.position.copy(camera.position);
    // с высоты туман слабее, чтобы был виден весь город
    const fogK = state.mode === 'fps' ? 1 : clamp(36 / camera.position.distanceTo(focus), 0.12, 1);
    (scene.fog as THREE.FogExp2).density = preset.fogD * fogK;

    // пожары: ближайшие 6 получают свет
    lightTimer -= dt;
    if (lightTimer <= 0) {
      lightTimer = 0.4;
      const near = emitters
        .filter((e) => e.kind === 'fire')
        .map((e) => ({ e, d: Math.hypot(e.x - camera.position.x, e.z - camera.position.z) }))
        .sort((a, b) => a.d - b.d)
        .slice(0, fireLights.length);
      fireLights.forEach((l, i) => {
        const n = near[i];
        if (n && n.d < 90) {
          l.visible = true;
          l.position.set(n.e.x, n.e.y + 1, n.e.z);
        } else l.visible = false;
      });
    }
    fireLights.forEach((l, i) => {
      if (!l.visible) return;
      const fl = 0.75 + Math.sin(time * 13 + i * 5) * 0.12 + Math.sin(time * 29 + i) * 0.08 + Math.random() * 0.12;
      l.intensity = 85 * preset.fireI * fl;
    });

    // эмиттеры
    const cp = camera.position;
    const gsc = renderer.domElement.height / (2 * Math.tan((camera.fov * Math.PI) / 360));
    smoke.setScale(gsc);
    flame.setScale(gsc);
    const sm = preset.smoke;
    for (const e of emitters) {
      if (!e.far && Math.hypot(e.x - cp.x, e.z - cp.z) > 170) continue;
      if (e.kind === 'fire') {
        e.acc += dt * 22 * (e.size / 1.5);
        while (e.acc > 1) {
          e.acc -= 1;
          const hot = Math.random();
          flame.spawn(e.x + (Math.random() - 0.5) * e.size * 0.5, e.y, e.z + (Math.random() - 0.5) * e.size * 0.5, (Math.random() - 0.5) * 0.5, 1.2 + Math.random() * 1.6, (Math.random() - 0.5) * 0.5, 0.5 + Math.random() * 0.5, e.size * 0.9, e.size * 0.25, 1, 0.45 + hot * 0.4, 0.1 + hot * 0.15, 0.8);
        }
      } else if (e.far) {
        e.acc += dt * 5;
        while (e.acc > 1) {
          e.acc -= 1;
          smoke.spawn(e.x + (Math.random() - 0.5) * 8, 0, e.z + (Math.random() - 0.5) * 8, 2.2 + Math.random(), 5 + Math.random() * 2, 0.6 + Math.random() * 0.8, 20, e.size, e.size * 5, sm * 0.9, sm * 0.88, sm * 0.85, 0.7);
        }
      } else {
        e.acc += dt * (3 + e.size * 0.9);
        while (e.acc > 1) {
          e.acc -= 1;
          const g = sm * (0.7 + Math.random() * 0.6);
          smoke.spawn(e.x + (Math.random() - 0.5) * e.size * 0.4, e.y, e.z + (Math.random() - 0.5) * e.size * 0.4, 0.9 + Math.random() * 0.4, 1.3 + Math.random() * 1.0, 0.3 + Math.random() * 0.3, 5 + Math.random() * 4, e.size * 0.7, e.size * 3.2, g, g * 0.97, g * 0.94, 0.6);
        }
      }
    }
    smoke.update(dt, 0.12, 0.02);
    flame.update(dt, 0.4, 0.4);

    // пепел вокруг камеры
    const ap = ashGeo.attributes.position as THREE.BufferAttribute;
    for (let i = 0; i < ASH; i++) {
      let x = ashPos[i * 3] + (0.5 + Math.sin(time * 0.7 + i) * 0.4) * dt;
      let y = ashPos[i * 3 + 1] - (0.35 + (i % 5) * 0.08) * dt;
      let z = ashPos[i * 3 + 2] + Math.cos(time * 0.5 + i * 1.3) * 0.3 * dt;
      if (y < 0 || y > 22) y = ((y % 22) + 22) % 22;
      ashPos[i * 3] = x;
      ashPos[i * 3 + 1] = y;
      ashPos[i * 3 + 2] = z;
      x = x - cp.x;
      z = z - cp.z;
      if (Math.abs(x) > 25) ashPos[i * 3] -= Math.sign(x) * 50;
      if (Math.abs(z) > 25) ashPos[i * 3 + 2] -= Math.sign(z) * 50;
    }
    ap.needsUpdate = true;

    renderer.render(scene, camera);
  }
  // первичное размещение пепла вокруг камеры
  for (let i = 0; i < ASH; i++) {
    ashPos[i * 3] += camera.position.x;
    ashPos[i * 3 + 2] += camera.position.z;
  }
  frame();

  const api: WorldHandle = {
    state,
    map,
    setTime: applyPreset,
    setMode(m) {
      if (m === state.mode) return;
      if (m === 'fps') {
        orbit.enabled = false;
        orbit.autoRotate = false;
        state.mode = 'fps';
        pitch = -0.05;
        const [x, z] = resolve(clamp(state.x, -lim, lim), clamp(state.z, -lim, lim), 0.5);
        state.x = x;
        state.z = z;
      } else {
        if (document.pointerLockElement) document.exitPointerLock();
        state.mode = 'orbit';
        orbit.enabled = true;
        orbit.target.set(state.x, 2, state.z);
        const back = new THREE.Vector3(Math.sin(state.yaw), 0, Math.cos(state.yaw)).multiplyScalar(45);
        camera.position.set(state.x + back.x, 38, state.z + back.z);
        orbit.update();
      }
    },
    teleport(x, z, yaw) {
      x = clamp(x, -lim, lim);
      z = clamp(z, -lim, lim);
      if (state.mode === 'fps') {
        const [rx, rz] = resolve(x, z, 0.5);
        state.x = rx;
        state.z = rz;
        if (yaw !== undefined) state.yaw = yaw;
        pitch = 0;
      } else {
        const dx = x - orbit.target.x;
        const dz = z - orbit.target.z;
        orbit.target.set(x, 2, z);
        camera.position.x += dx;
        camera.position.z += dz;
        if (yaw !== undefined) {
          const off = camera.position.clone().sub(orbit.target);
          const d = Math.hypot(off.x, off.z);
          camera.position.x = x + Math.sin(yaw) * d;
          camera.position.z = z + Math.cos(yaw) * d;
        }
        orbit.autoRotate = false;
        orbit.update();
      }
    },
    lock() {
      try {
        const p = dom.requestPointerLock() as unknown as Promise<void> | undefined;
        p?.catch?.(() => undefined);
      } catch {
        /* pointer lock недоступен — работает режим перетаскивания */
      }
    },
    startAudio: () => audio.start(),
    setMuted: (m) => audio.setMuted(m),
    dispose() {
      cancelAnimationFrame(raf);
      ro.disconnect();
      window.removeEventListener('mousemove', onMouseMove);
      dom.removeEventListener('mousedown', onDown);
      window.removeEventListener('mouseup', onUp);
      document.removeEventListener('pointerlockchange', onLockChange);
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('keyup', onKey);
      window.removeEventListener('blur', onBlur);
      dom.removeEventListener('click', onClick);
      if (document.pointerLockElement === dom) document.exitPointerLock();
      orbit.dispose();
      audio.dispose();
      smoke.dispose();
      flame.dispose();
      renderer.dispose();
      dom.remove();
    },
  };
  return api;
}
