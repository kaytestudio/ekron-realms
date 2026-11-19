import * as THREE from 'three';

const dummy = new THREE.Object3D();

/** Накопитель инстансов — потом превращается в один InstancedMesh */
export class Batch {
  private items: { m: THREE.Matrix4; c: THREE.Color }[] = [];

  get count() {
    return this.items.length;
  }

  add(
    x: number,
    y: number,
    z: number,
    sx: number,
    sy: number,
    sz: number,
    rx: number,
    ry: number,
    rz: number,
    color: THREE.Color | number | string = 0xffffff
  ) {
    dummy.position.set(x, y, z);
    dummy.rotation.set(rx, ry, rz);
    dummy.scale.set(sx, sy, sz);
    dummy.updateMatrix();
    this.items.push({ m: dummy.matrix.clone(), c: new THREE.Color(color) });
  }

  build(
    geo: THREE.BufferGeometry,
    mat: THREE.Material | THREE.Material[],
    parent: THREE.Object3D,
    cast = true,
    receive = true
  ): THREE.InstancedMesh | null {
    if (!this.items.length) return null;
    const mesh = new THREE.InstancedMesh(geo, mat, this.items.length);
    this.items.forEach((it, i) => {
      mesh.setMatrixAt(i, it.m);
      mesh.setColorAt(i, it.c);
    });
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
    mesh.castShadow = cast;
    mesh.receiveShadow = receive;
    mesh.computeBoundingSphere();
    parent.add(mesh);
    return mesh;
  }
}
