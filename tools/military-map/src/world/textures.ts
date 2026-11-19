import * as THREE from 'three';
import { BLOCK, BLOCK_IDX, CRATERS, HALF, PITCH, STREETS } from './layout';
import { makeRng, range } from './rng';

function canvas(w: number, h = w) {
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  return { c, g: c.getContext('2d')! };
}

function finish(c: HTMLCanvasElement, srgb = true, aniso = 8) {
  const t = new THREE.CanvasTexture(c);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = aniso;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  return t;
}

function speckle(g: CanvasRenderingContext2D, w: number, h: number, n: number, rnd: () => number, dark = 0.25, light = 0.1) {
  for (let i = 0; i < n; i++) {
    const x = rnd() * w;
    const y = rnd() * h;
    const s = rnd() * 2 + 0.5;
    g.fillStyle = rnd() > 0.45 ? `rgba(0,0,0,${rnd() * dark})` : `rgba(255,255,255,${rnd() * light})`;
    g.fillRect(x, y, s, s);
  }
}

/** Текстура ячейки стены: бетон/кирпич, окно или глухая стена, копоть, пробоины */
export function wallTexture(brick: boolean, windowed: boolean, seed: number) {
  const rnd = makeRng(seed);
  const S = 256;
  const { c, g } = canvas(S);
  g.fillStyle = brick ? '#8d5b45' : '#a5a298';
  g.fillRect(0, 0, S, S);

  if (brick) {
    const bh = 12;
    const bw = 28;
    for (let y = 0, row = 0; y < S; y += bh, row++) {
      for (let x = -(row % 2) * (bw / 2); x < S; x += bw) {
        const v = 0.78 + rnd() * 0.4;
        g.fillStyle = `rgb(${Math.floor(150 * v)},${Math.floor(86 * v)},${Math.floor(66 * v)})`;
        g.fillRect(x + 1, y + 1, bw - 2, bh - 2);
      }
    }
  } else {
    // бетонные панели + швы
    g.strokeStyle = 'rgba(0,0,0,0.18)';
    g.lineWidth = 2;
    g.strokeRect(1, 1, S - 2, S - 2);
    g.beginPath();
    g.moveTo(0, S * 0.5);
    g.lineTo(S, S * 0.5);
    g.stroke();
    for (let i = 0; i < 6; i++) {
      const gr = g.createRadialGradient(rnd() * S, rnd() * S, 4, rnd() * S, rnd() * S, 80);
      gr.addColorStop(0, 'rgba(60,55,45,0.18)');
      gr.addColorStop(1, 'rgba(60,55,45,0)');
      g.fillStyle = gr;
      g.fillRect(0, 0, S, S);
    }
  }
  speckle(g, S, S, 2500, rnd);

  if (windowed) {
    const x0 = 62;
    const y0 = 62;
    const w = 132;
    const h = 130;
    // копоть над окном
    const soot = g.createLinearGradient(0, y0 - 70, 0, y0 + 10);
    soot.addColorStop(0, 'rgba(10,8,6,0)');
    soot.addColorStop(1, 'rgba(10,8,6,0.55)');
    g.fillStyle = soot;
    g.fillRect(x0 - 14, 0, w + 28, y0 + 10);
    // проём
    g.fillStyle = '#050607';
    g.fillRect(x0, y0, w, h);
    const inner = g.createLinearGradient(0, y0, 0, y0 + h);
    inner.addColorStop(0, '#15130f');
    inner.addColorStop(1, '#2a2620');
    g.fillStyle = inner;
    g.fillRect(x0 + 8, y0 + 8, w - 16, h - 16);
    // рама
    g.strokeStyle = '#3d3a34';
    g.lineWidth = 8;
    g.strokeRect(x0 - 2, y0 - 2, w + 4, h + 4);
    // подоконник
    g.fillStyle = '#6d6a62';
    g.fillRect(x0 - 10, y0 + h + 2, w + 20, 9);
    // остатки стёкол
    g.fillStyle = 'rgba(160,190,200,0.35)';
    const shards = 2 + Math.floor(rnd() * 3);
    for (let i = 0; i < shards; i++) {
      const sx = x0 + 6 + rnd() * (w - 30);
      g.beginPath();
      g.moveTo(sx, y0 + 6);
      g.lineTo(sx + 14 + rnd() * 16, y0 + 6);
      g.lineTo(sx + 4 + rnd() * 8, y0 + 20 + rnd() * 34);
      g.closePath();
      g.fill();
    }
    // следы от пуль вокруг
    for (let i = 0; i < 6; i++) {
      g.fillStyle = 'rgba(20,18,15,0.7)';
      g.beginPath();
      g.arc(rnd() * S, rnd() * S, 1.5 + rnd() * 3, 0, 7);
      g.fill();
    }
  } else {
    // глухая стена: трещины, пробоины, граффити-метки
    g.strokeStyle = 'rgba(15,12,10,0.7)';
    g.lineWidth = 2;
    for (let k = 0; k < 3; k++) {
      let x = rnd() * S;
      let y = rnd() * S * 0.4;
      g.beginPath();
      g.moveTo(x, y);
      for (let i = 0; i < 9; i++) {
        x += (rnd() - 0.5) * 36;
        y += 12 + rnd() * 18;
        g.lineTo(x, y);
      }
      g.stroke();
    }
    if (rnd() > 0.35) {
      // большая пробоина от снаряда
      const cx = 60 + rnd() * 140;
      const cy = 60 + rnd() * 140;
      const r = 24 + rnd() * 26;
      const gr = g.createRadialGradient(cx, cy, 2, cx, cy, r * 1.7);
      gr.addColorStop(0, '#0a0908');
      gr.addColorStop(0.55, '#1a1714');
      gr.addColorStop(0.62, 'rgba(40,34,28,0.8)');
      gr.addColorStop(1, 'rgba(30,26,22,0)');
      g.fillStyle = gr;
      g.beginPath();
      g.arc(cx, cy, r * 1.7, 0, 7);
      g.fill();
    }
    for (let i = 0; i < 16; i++) {
      g.fillStyle = 'rgba(15,13,10,0.7)';
      g.beginPath();
      g.arc(rnd() * S, rnd() * S, 1 + rnd() * 2.5, 0, 7);
      g.fill();
    }
    // метка краской
    if (rnd() > 0.7) {
      g.fillStyle = 'rgba(210,190,60,0.55)';
      g.font = 'bold 54px Impact, sans-serif';
      g.fillText(['Z', 'V', 'X', '№7'][Math.floor(rnd() * 4)], 70 + rnd() * 60, 130 + rnd() * 70);
    }
  }
  // низ - копоть/грязь
  const dirt = g.createLinearGradient(0, S * 0.6, 0, S);
  dirt.addColorStop(0, 'rgba(25,20,15,0)');
  dirt.addColorStop(1, 'rgba(25,20,15,0.4)');
  g.fillStyle = dirt;
  g.fillRect(0, 0, S, S);

  const t = finish(c, true, 4);
  t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping;
  return t;
}

/** Большая текстура всего города: дороги, тротуары, воронки, следы гусениц */
export function cityGroundTexture(S = 2048) {
  const rnd = makeRng(777);
  const { c, g } = canvas(S);
  const px = (v: number) => ((v + HALF) / (HALF * 2)) * S;
  const m = S / (HALF * 2); // пикселей на метр

  // грунт
  g.fillStyle = '#4a4339';
  g.fillRect(0, 0, S, S);
  speckle(g, S, S, 60000, rnd, 0.3, 0.08);

  // асфальт
  g.fillStyle = '#2f2f2e';
  g.fillRect(px(-HALF), px(-HALF), S, S);
  speckle(g, S, S, 120000, rnd, 0.35, 0.12);

  // кварталы (тротуар + двор)
  for (const i of BLOCK_IDX) {
    for (const j of BLOCK_IDX) {
      const cx = i * PITCH;
      const cz = j * PITCH;
      const x0 = px(cx - BLOCK / 2);
      const y0 = px(cz - BLOCK / 2);
      const w = BLOCK * m;
      g.fillStyle = '#76726a';
      g.fillRect(x0, y0, w, w);
      g.fillStyle = '#4e483f';
      g.fillRect(x0 + 2.2 * m, y0 + 2.2 * m, w - 4.4 * m, w - 4.4 * m);
      // потрескавшаяся плитка
      g.strokeStyle = 'rgba(0,0,0,0.25)';
      g.lineWidth = 1;
      for (let k = 0; k < 14; k++) {
        g.beginPath();
        g.moveTo(x0 + rnd() * w, y0 + rnd() * w);
        g.lineTo(x0 + rnd() * w, y0 + rnd() * w);
        g.stroke();
      }
      // бордюр
      g.strokeStyle = '#9a958a';
      g.lineWidth = 3;
      g.strokeRect(x0, y0, w, w);
    }
  }
  speckle(g, S, S, 80000, rnd, 0.3, 0.08);

  // разметка
  const lane = (x1: number, z1: number, x2: number, z2: number, col: string, dash: number[], lw: number) => {
    g.strokeStyle = col;
    g.lineWidth = lw;
    g.setLineDash(dash);
    g.beginPath();
    g.moveTo(px(x1), px(z1));
    g.lineTo(px(x2), px(z2));
    g.stroke();
    g.setLineDash([]);
  };
  for (const s of STREETS) {
    for (let k = -2; k <= 2; k++) {
      // по сегментам между кварталами
      const a = k * PITCH - BLOCK / 2;
      const b = k * PITCH + BLOCK / 2;
      lane(a, s, b, s, 'rgba(210,180,60,0.7)', [m * 3, m * 3], 0.25 * m);
      lane(s, a, s, b, 'rgba(210,180,60,0.7)', [m * 3, m * 3], 0.25 * m);
    }
  }
  // зебры на перекрёстках
  g.fillStyle = 'rgba(220,220,210,0.55)';
  for (const sx of STREETS) {
    for (const sz of STREETS) {
      if (Math.abs(sx) === 120 || Math.abs(sz) === 120) continue;
      for (let k = -6; k <= 6; k += 2) {
        g.fillRect(px(sx + k) - 0.4 * m, px(sz - 10), 0.8 * m, 2 * m);
        g.fillRect(px(sx + k) - 0.4 * m, px(sz + 8), 0.8 * m, 2 * m);
        g.fillRect(px(sx - 10), px(sz + k) - 0.4 * m, 2 * m, 0.8 * m);
        g.fillRect(px(sx + 8), px(sz + k) - 0.4 * m, 2 * m, 0.8 * m);
      }
    }
  }

  // трещины в асфальте
  g.strokeStyle = 'rgba(8,8,8,0.55)';
  g.lineWidth = 1.2;
  for (let k = 0; k < 260; k++) {
    let x = rnd() * S;
    let y = rnd() * S;
    g.beginPath();
    g.moveTo(x, y);
    for (let i = 0; i < 8; i++) {
      x += (rnd() - 0.5) * 40;
      y += (rnd() - 0.5) * 40;
      g.lineTo(x, y);
    }
    g.stroke();
  }

  // следы гусениц
  const tracks = (pts: [number, number][]) => {
    for (const off of [-1.6, 1.6]) {
      g.strokeStyle = 'rgba(14,10,6,0.42)';
      g.lineWidth = 0.85 * m;
      g.lineCap = 'butt';
      g.beginPath();
      pts.forEach(([x, z], i) => {
        const wob = Math.sin(i * 1.7) * 0.4;
        const horizontal = i < pts.length - 1 ? Math.abs(pts[i + 1][0] - x) > Math.abs(pts[i + 1][1] - z) : true;
        const X = horizontal ? x : x + off + wob;
        const Z = horizontal ? z + off + wob : z;
        if (i === 0) g.moveTo(px(X), px(Z));
        else g.lineTo(px(X), px(Z));
      });
      g.stroke();
      g.strokeStyle = 'rgba(0,0,0,0.25)';
      g.lineWidth = 0.9 * m;
      g.setLineDash([1.2, m * 0.45]);
      g.stroke();
      g.setLineDash([]);
    }
  };
  const line = (x1: number, z1: number, x2: number, z2: number): [number, number][] => {
    const n = 60;
    return Array.from({ length: n + 1 }, (_, i) => [x1 + ((x2 - x1) * i) / n, z1 + ((z2 - z1) * i) / n]);
  };
  tracks(line(-124, 24, 20, 24));
  tracks(line(24, 124, 24, -110));
  tracks(line(124, 72, 60, 72));
  tracks(line(-72, 124, -72, 30));

  // пятна гари/масла
  for (let k = 0; k < 60; k++) {
    const x = rnd() * S;
    const y = rnd() * S;
    const r = range(rnd, 6, 28);
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    gr.addColorStop(0, 'rgba(5,4,3,0.55)');
    gr.addColorStop(1, 'rgba(5,4,3,0)');
    g.fillStyle = gr;
    g.beginPath();
    g.arc(x, y, r, 0, 7);
    g.fill();
  }

  // воронки
  for (const cr of CRATERS) {
    const x = px(cr.x);
    const y = px(cr.z);
    const r = cr.r * m;
    const sc = g.createRadialGradient(x, y, r * 0.3, x, y, r * 2.2);
    sc.addColorStop(0, 'rgba(5,4,3,0.85)');
    sc.addColorStop(0.5, 'rgba(10,8,6,0.5)');
    sc.addColorStop(1, 'rgba(10,8,6,0)');
    g.fillStyle = sc;
    g.beginPath();
    g.arc(x, y, r * 2.2, 0, 7);
    g.fill();
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    gr.addColorStop(0, '#0b0907');
    gr.addColorStop(0.7, '#1f1a15');
    gr.addColorStop(0.92, '#3a3228');
    gr.addColorStop(1, '#4d4336');
    g.fillStyle = gr;
    g.beginPath();
    for (let a = 0; a <= 32; a++) {
      const ang = (a / 32) * Math.PI * 2;
      const rr = r * (0.92 + Math.sin(ang * 3 + cr.x) * 0.06 + rnd() * 0.04);
      const X = x + Math.cos(ang) * rr;
      const Y = y + Math.sin(ang) * rr;
      if (a === 0) g.moveTo(X, Y);
      else g.lineTo(X, Y);
    }
    g.fill();
    // лучи разлёта
    g.strokeStyle = 'rgba(8,6,4,0.4)';
    g.lineWidth = 2;
    for (let k = 0; k < 14; k++) {
      const a = rnd() * 7;
      g.beginPath();
      g.moveTo(x + Math.cos(a) * r, y + Math.sin(a) * r);
      g.lineTo(x + Math.cos(a) * r * range(rnd, 1.5, 2.6), y + Math.sin(a) * r * range(rnd, 1.5, 2.6));
      g.stroke();
    }
  }

  return finish(c, true, 16);
}

/** Грунт за пределами города */
export function dirtTexture() {
  const rnd = makeRng(99);
  const S = 512;
  const { c, g } = canvas(S);
  g.fillStyle = '#463e33';
  g.fillRect(0, 0, S, S);
  for (let i = 0; i < 40; i++) {
    const x = rnd() * S;
    const y = rnd() * S;
    const r = range(rnd, 20, 90);
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    const dark = rnd() > 0.5;
    gr.addColorStop(0, dark ? 'rgba(20,16,12,0.35)' : 'rgba(110,95,70,0.22)');
    gr.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = gr;
    g.fillRect(x - r, y - r, r * 2, r * 2);
  }
  speckle(g, S, S, 20000, rnd, 0.4, 0.15);
  const t = finish(c);
  t.repeat.set(60, 60);
  return t;
}

/** Мягкое пятно (копоть под техникой) */
export function blobTexture() {
  const { c, g } = canvas(128);
  const gr = g.createRadialGradient(64, 64, 4, 64, 64, 64);
  gr.addColorStop(0, 'rgba(255,255,255,1)');
  gr.addColorStop(0.6, 'rgba(255,255,255,0.55)');
  gr.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = gr;
  g.fillRect(0, 0, 128, 128);
  const t = new THREE.CanvasTexture(c);
  return t;
}

export function smokeSprite() {
  const rnd = makeRng(5);
  const { c, g } = canvas(128);
  for (let i = 0; i < 14; i++) {
    const x = 64 + (rnd() - 0.5) * 50;
    const y = 64 + (rnd() - 0.5) * 50;
    const r = 18 + rnd() * 26;
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    gr.addColorStop(0, 'rgba(255,255,255,0.35)');
    gr.addColorStop(1, 'rgba(255,255,255,0)');
    g.fillStyle = gr;
    g.fillRect(0, 0, 128, 128);
  }
  return new THREE.CanvasTexture(c);
}

export function glowSprite() {
  const { c, g } = canvas(64);
  const gr = g.createRadialGradient(32, 32, 0, 32, 32, 32);
  gr.addColorStop(0, 'rgba(255,255,255,1)');
  gr.addColorStop(0.35, 'rgba(255,255,255,0.5)');
  gr.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = gr;
  g.fillRect(0, 0, 64, 64);
  return new THREE.CanvasTexture(c);
}
