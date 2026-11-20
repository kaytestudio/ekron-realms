import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { Ambience } from './src/world/audio';
import { Batch } from './src/world/batch';
import { BuildingRect, Colliders, Lot, createBuildingKit } from './src/world/buildings';
import { BLOCK_IDX, CRATERS, HALF, PITCH, POIS, STREETS } from './src/world/layout';
import { ParticleSystem } from './src/world/particles';
import { barbedWire, barriers, craterRim, crates, deadTree, drum, hedgehogs, monument, PropCtx, powerLine, sandbagWall, scorch, tent, watchtower } from './src/world/props';
import { Rng, clamp, makeRng, pick, range } from './src/world/rng';
import { blobTexture, cityGroundTexture, dirtTexture, glowSprite, smokeSprite } from './src/world/textures';
import { FireSpec, Paint, TankVariant, VehicleResult, buildBus, buildCar, buildTank, buildTruck } from './src/world/vehicles';


export function buildMap(){
const scene=new THREE.Scene();
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


return {scene,fires,colliders,mapVehicles};
}
