import fs from 'node:fs';
import * as THREE from 'three';
import {createCanvas, Canvas, ImageData} from '@napi-rs/canvas';
import {GLTFExporter} from 'three/examples/jsm/exporters/GLTFExporter.js';
import {generateCity,cityExtent} from './src/city/generateCity';
import {createMaterials} from './src/city/textures';
(globalThis as any).document={createElement:()=>createCanvas(512,512),createElementNS:()=>createCanvas(512,512)};
(globalThis as any).HTMLCanvasElement=createCanvas(1,1).constructor;
(globalThis as any).ImageData=ImageData;
(globalThis as any).FileReader=class {
 result:any; onloadend:any;
 readAsArrayBuffer(blob:Blob){blob.arrayBuffer().then(b=>{this.result=b;this.onloadend?.();});}
 readAsDataURL(blob:Blob){blob.arrayBuffer().then(b=>{this.result=`data:${blob.type};base64,${Buffer.from(b).toString('base64')}`;this.onloadend?.();});}
};
// Seed facade noise as well as the generator, for reproducible assets.
let seed=2025;Math.random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
const materials=createMaterials();
const canvas=materials.map.image;
const pixels=canvas.getContext('2d').getImageData(0,0,512,512).data;
const texture=new THREE.DataTexture(new Uint8Array(pixels),512,512);
texture.colorSpace=THREE.SRGBColorSpace;texture.wrapS=texture.wrapT=THREE.RepeatWrapping;texture.flipY=true;
materials.solid.map=texture;
const city=generateCity({seed:2025,blocks:12,destruction:.65},materials);
console.log(city.stats,cityExtent(12));
const result=await new GLTFExporter().parseAsync(city.group,{binary:true,maxTextureSize:1024});
fs.writeFileSync(new URL('../../sys/android/app/src/main/assets/models/destroyed_city.glb',import.meta.url),Buffer.from(result as ArrayBuffer));
fs.writeFileSync(new URL('./metadata.json',import.meta.url),JSON.stringify({extent:cityExtent(12),stats:city.stats,spawns:city.spawns,options:city.options},null,2));
