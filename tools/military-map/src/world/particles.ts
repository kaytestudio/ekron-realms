import * as THREE from 'three';

const vert = /* glsl */ `
  attribute float aSize;
  attribute vec4 aColor;
  varying vec4 vColor;
  uniform float uScale;
  void main() {
    vColor = aColor;
    vec4 mv = modelViewMatrix * vec4(position, 1.0);
    gl_PointSize = clamp(aSize * uScale / max(0.1, -mv.z), 0.0, 700.0);
    gl_Position = projectionMatrix * mv;
  }
`;
const frag = /* glsl */ `
  uniform sampler2D map;
  varying vec4 vColor;
  void main() {
    vec4 t = texture2D(map, gl_PointCoord);
    gl_FragColor = vec4(vColor.rgb, vColor.a * t.a);
  }
`;

export class ParticleSystem {
  readonly points: THREE.Points;
  private n: number;
  private pos: Float32Array;
  private vel: Float32Array;
  private age: Float32Array;
  private life: Float32Array;
  private s0: Float32Array;
  private s1: Float32Array;
  private col: Float32Array;
  private a0: Float32Array;
  private size: Float32Array;
  private rgba: Float32Array;
  private head = 0;
  private geo: THREE.BufferGeometry;
  private mat: THREE.ShaderMaterial;

  constructor(n: number, map: THREE.Texture, additive: boolean) {
    this.n = n;
    this.pos = new Float32Array(n * 3).fill(-9999);
    this.vel = new Float32Array(n * 3);
    this.age = new Float32Array(n).fill(99);
    this.life = new Float32Array(n).fill(1);
    this.s0 = new Float32Array(n);
    this.s1 = new Float32Array(n);
    this.col = new Float32Array(n * 3);
    this.a0 = new Float32Array(n);
    this.size = new Float32Array(n);
    this.rgba = new Float32Array(n * 4);
    this.geo = new THREE.BufferGeometry();
    this.geo.setAttribute('position', new THREE.BufferAttribute(this.pos, 3).setUsage(THREE.DynamicDrawUsage));
    this.geo.setAttribute('aSize', new THREE.BufferAttribute(this.size, 1).setUsage(THREE.DynamicDrawUsage));
    this.geo.setAttribute('aColor', new THREE.BufferAttribute(this.rgba, 4).setUsage(THREE.DynamicDrawUsage));
    this.mat = new THREE.ShaderMaterial({
      uniforms: { map: { value: map }, uScale: { value: 800 } },
      vertexShader: vert,
      fragmentShader: frag,
      transparent: true,
      depthWrite: false,
      blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
    });
    this.points = new THREE.Points(this.geo, this.mat);
    this.points.frustumCulled = false;
    this.points.renderOrder = additive ? 3 : 2;
  }

  setScale(s: number) {
    this.mat.uniforms.uScale.value = s;
  }

  spawn(x: number, y: number, z: number, vx: number, vy: number, vz: number, life: number, s0: number, s1: number, r: number, g: number, b: number, a: number) {
    const i = this.head;
    this.head = (this.head + 1) % this.n;
    this.pos[i * 3] = x;
    this.pos[i * 3 + 1] = y;
    this.pos[i * 3 + 2] = z;
    this.vel[i * 3] = vx;
    this.vel[i * 3 + 1] = vy;
    this.vel[i * 3 + 2] = vz;
    this.age[i] = 0;
    this.life[i] = life;
    this.s0[i] = s0;
    this.s1[i] = s1;
    this.col[i * 3] = r;
    this.col[i * 3 + 1] = g;
    this.col[i * 3 + 2] = b;
    this.a0[i] = a;
  }

  update(dt: number, drag: number, buoyancy: number) {
    for (let i = 0; i < this.n; i++) {
      if (this.age[i] >= this.life[i]) {
        this.rgba[i * 4 + 3] = 0;
        continue;
      }
      this.age[i] += dt;
      const f = Math.min(1, this.age[i] / this.life[i]);
      this.vel[i * 3] *= 1 - drag * dt;
      this.vel[i * 3 + 2] *= 1 - drag * dt;
      this.vel[i * 3 + 1] += buoyancy * dt;
      this.pos[i * 3] += this.vel[i * 3] * dt;
      this.pos[i * 3 + 1] += this.vel[i * 3 + 1] * dt;
      this.pos[i * 3 + 2] += this.vel[i * 3 + 2] * dt;
      this.size[i] = this.s0[i] + (this.s1[i] - this.s0[i]) * f;
      const fade = Math.min(1, f * 6) * (1 - f);
      this.rgba[i * 4] = this.col[i * 3];
      this.rgba[i * 4 + 1] = this.col[i * 3 + 1];
      this.rgba[i * 4 + 2] = this.col[i * 3 + 2];
      this.rgba[i * 4 + 3] = this.a0[i] * fade;
    }
    this.geo.attributes.position.needsUpdate = true;
    this.geo.attributes.aSize.needsUpdate = true;
    this.geo.attributes.aColor.needsUpdate = true;
  }

  dispose() {
    this.geo.dispose();
    this.mat.dispose();
  }
}
