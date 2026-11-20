import fs from 'node:fs';
import * as THREE from 'three';
import {createCanvas,ImageData} from '@napi-rs/canvas';
import {GLTFExporter} from 'three/examples/jsm/exporters/GLTFExporter.js';
import {mergeGeometries,mergeVertices} from 'three/examples/jsm/utils/BufferGeometryUtils.js';
import {buildMap} from './build';
(globalThis as any).document={createElement:()=>createCanvas(1,1),createElementNS:()=>createCanvas(1,1)};
(globalThis as any).HTMLCanvasElement=createCanvas(1,1).constructor;
(globalThis as any).ImageData=ImageData;
(globalThis as any).FileReader=class{result:any;onloadend:any;readAsArrayBuffer(b:Blob){b.arrayBuffer().then(x=>{this.result=x;this.onloadend?.();});}readAsDataURL(b:Blob){b.arrayBuffer().then(x=>{this.result=`data:${b.type};base64,${Buffer.from(x).toString('base64')}`;this.onloadend?.();});}};
let seed=20240229;Math.random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
const {scene,fires,colliders,mapVehicles}=buildMap();scene.updateMatrixWorld(true);
const groups=new Map<THREE.Material,THREE.BufferGeometry[]>();
const textures=new Map<THREE.Texture,THREE.Texture>();
const uvTransforms=new Map<THREE.Material,THREE.Matrix3>();
function material(m:THREE.Material){
 if(groups.has(m))return;
 const mat=m as THREE.MeshStandardMaterial;
 if(mat.map?.image?.getContext){
  const old=mat.map;old.updateMatrix();uvTransforms.set(m,old.matrix.clone());let tex=textures.get(old);
  if(!tex){const c=old.image;const data=c.getContext('2d').getImageData(0,0,c.width,c.height).data;
   tex=new THREE.DataTexture(new Uint8Array(data),c.width,c.height);tex.colorSpace=old.colorSpace;tex.wrapS=old.wrapS;tex.wrapT=old.wrapT;tex.flipY=old.flipY;textures.set(old,tex);}
  mat.map=tex;
 }
 groups.set(m,[]);
}
scene.traverse(o=>{
 const mesh=o as THREE.Mesh;if(!mesh.isMesh)return;
 const instances=(mesh as THREE.InstancedMesh).isInstancedMesh?(mesh as THREE.InstancedMesh).count:1;
 for(let i=0;i<instances;i++){
  let transform=mesh.matrixWorld.clone();const color=new THREE.Color(1,1,1);
  if((mesh as THREE.InstancedMesh).isInstancedMesh){const im=mesh as THREE.InstancedMesh;const matrix=new THREE.Matrix4();im.getMatrixAt(i,matrix);transform.multiply(matrix);if(im.instanceColor)im.getColorAt(i,color);}
  const geo=mesh.geometry.index?mesh.geometry.toNonIndexed():mesh.geometry.clone();geo.applyMatrix4(transform);
  if(!geo.attributes.normal)geo.computeVertexNormals();const count=geo.attributes.position.count;
  const cols=new Float32Array(count*3);const c=geo.attributes.color;
  for(let v=0;v<count;v++){cols[v*3]=(c?c.getX(v):1)*color.r;cols[v*3+1]=(c?c.getY(v):1)*color.g;cols[v*3+2]=(c?c.getZ(v):1)*color.b;}
  geo.setAttribute('color',new THREE.BufferAttribute(cols,3));if(!geo.attributes.uv)geo.setAttribute('uv',new THREE.BufferAttribute(new Float32Array(count*2),2));
  if(transform.determinant()<0){for(const key of ['position','normal','uv','color']){const a=geo.getAttribute(key);for(let v=0;v<count;v+=3)for(let c=0;c<a.itemSize;c++){const b=a.getComponent(v+1,c);a.setComponent(v+1,c,a.getComponent(v+2,c));a.setComponent(v+2,c,b);}}}
  const ranges=Array.isArray(mesh.material)?geo.groups:[{start:0,count,materialIndex:0}];
  for(const range of ranges){const m=Array.isArray(mesh.material)?mesh.material[range.materialIndex!]:mesh.material;material(m);m.vertexColors=true;
   const part=new THREE.BufferGeometry();for(const key of ['position','normal','uv','color']){const attr=geo.getAttribute(key);part.setAttribute(key,new THREE.BufferAttribute((attr.array as Float32Array).slice(range.start*attr.itemSize,(range.start+range.count)*attr.itemSize),attr.itemSize));}
   const uvMatrix=uvTransforms.get(m);if(uvMatrix){const uv=part.getAttribute('uv');const v=new THREE.Vector2();for(let k=0;k<uv.count;k++){v.set(uv.getX(k),uv.getY(k)).applyMatrix3(uvMatrix);uv.setXY(k,v.x,v.y);}}
   groups.get(m)!.push(part);
  }
  geo.dispose();
 }
});
const root=new THREE.Group();root.name='MilitaryCity20240229';let triangles=0;
for(const [m,parts] of groups){const merged=mergeGeometries(parts);if(!merged)throw Error('merge failed');triangles+=merged.attributes.position.count/3;root.add(new THREE.Mesh(mergeVertices(merged),m));merged.dispose();parts.forEach(p=>p.dispose());}
console.log({triangles,materials:groups.size,fires:fires.length,vehicles:mapVehicles.length});
const output=await new GLTFExporter().parseAsync(root,{binary:true,maxTextureSize:2048});
const base=new URL('../../sys/android/app/src/main/',import.meta.url);
fs.writeFileSync(new URL('assets/models/military_city.glb',base),Buffer.from(output as ArrayBuffer));
const emitters=[...fires,...[[210,-150],[-230,-70],[70,250],[-160,-240],[260,120],[-250,160]].map(([x,z])=>({x,y:0,z,kind:'smoke',size:16,far:true}))];
fs.writeFileSync(new URL('./metadata.json',import.meta.url),JSON.stringify({triangles,materials:groups.size,fires:emitters,colliders,mapVehicles},null,2));
fs.writeFileSync(new URL('cpp/MilitaryEmitters.h',base),'// Generated from user military city source; coordinates in metres.\n#pragma once\n#include <array>\nnamespace military {\nstruct Emitter {float x,y,z,size;bool fire,far;};\ninline constexpr std::array<Emitter,'+emitters.length+'> emitters{{\n'+emitters.map(e=>`{${e.x},${e.y},${e.z},${e.size},${e.kind==='fire'},${!!e.far}}`).join(',\n')+'\n}};\n}\n');
