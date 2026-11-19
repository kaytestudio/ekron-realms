// Общая раскладка города: сетка кварталов 5x5, шаг 48 м, ширина улицы 16 м
export const PITCH = 48;
export const BLOCK = 32;
export const HALF = 128; // половина размера города
export const STREETS = [-120, -72, -24, 24, 72, 120];
export const BLOCK_IDX = [-2, -1, 0, 1, 2];

export interface Crater {
  x: number;
  z: number;
  r: number;
}

export const CRATERS: Crater[] = [
  { x: 11, z: 10, r: 7 },
  { x: -24, z: -72, r: 7 },
  { x: 72, z: -24, r: 8 },
  { x: 24, z: -72, r: 6 },
  { x: -72, z: 24, r: 7 },
  { x: 96, z: 24, r: 5 },
  { x: -96, z: -24, r: 6 },
  { x: 24, z: 80, r: 4.5 },
  { x: -48, z: 24, r: 3 },
  { x: 48, z: -24, r: 3.2 },
  { x: -72, z: -96, r: 4 },
  { x: 72, z: 100, r: 3.4 },
  { x: -120, z: 48, r: 3 },
  { x: 120, z: -50, r: 3.6 },
  { x: 8, z: -120, r: 3.2 },
  { x: -30, z: 120, r: 3.8 },
];

export interface Poi {
  id: string;
  name: string;
  desc: string;
  x: number;
  z: number;
  yaw: number;
}

// yaw = 0 -> смотрим на -Z (север на карте)
export const POIS: Poi[] = [
  { id: 'spawn', name: 'Точка высадки', desc: 'Южный въезд в сектор', x: 24, z: 104, yaw: 0 },
  { id: 'plaza', name: 'Центральная площадь', desc: 'Воронка, памятник, подбитый Т-72', x: 14, z: 22, yaw: 0.6 },
  { id: 'column', name: 'Колонна на проспекте', desc: 'Горящие танки на проспекте Победы', x: -90, z: 30, yaw: -Math.PI / 2 },
  { id: 'barricade', name: 'Баррикада', desc: 'Мешки, ежи, бетонные блоки', x: 72, z: 100, yaw: 0.2 },
  { id: 'checkpoint', name: 'Блокпост', desc: 'Палатки и вышка', x: 80, z: -52, yaw: -0.35 },
  { id: 'park', name: 'Сожжённый парк', desc: 'Траншеи и мёртвые деревья', x: -80, z: 60, yaw: 2.72 },
];
