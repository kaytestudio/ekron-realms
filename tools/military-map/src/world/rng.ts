export type Rng = () => number;

export function makeRng(seed: number): Rng {
  let s = seed >>> 0;
  return () => {
    s += 0x6d2b79f5;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export const range = (r: Rng, a: number, b: number) => a + (b - a) * r();
export const pick = <T>(r: Rng, arr: T[]): T => arr[Math.floor(r() * arr.length)];
export const clamp = (v: number, a: number, b: number) => Math.min(b, Math.max(a, v));
export const gauss = (r: Rng) => (r() + r() + r() + r() - 2) / 2;
