#include "../../sys/android/app/src/main/cpp/GltfScene.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <cstring>
bool read(const std::string&p,std::vector<unsigned char>&b){std::ifstream f(p,std::ios::binary);if(!f)return false;b.assign(std::istreambuf_iterator<char>(f),{});return true;}
int main(){
 // JSON glTF + external .bin, missing normals, node TRS and untextured material.
 const std::string json=R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"translation":[4,2,7],"scale":[2,1,3]}],"buffers":[{"uri":"floor.bin","byteLength":72}],"bufferViews":[{"buffer":0,"byteLength":72}],"accessors":[{"bufferView":0,"componentType":5126,"count":6,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
 float points[]={-2,0,-2,2,0,2,2,0,-2,-2,0,-2,-2,0,2,2,0,2};
 city::ReadFile fixture=[&](const std::string&p,std::vector<unsigned char>&out){if(p=="fixture/scene.gltf"){out.assign(json.begin(),json.end());return true;}if(p=="fixture/floor.bin"){out.resize(sizeof(points));std::memcpy(out.data(),points,sizeof(points));return true;}return false;};
 city::Scene external;std::string details;assert(external.load("fixture/scene.gltf",fixture,details));assert(external.batches.size()==1&&external.batches[0].vertices.size()==6);assert(external.materials.empty());assert(std::abs(external.maximum[2]-external.minimum[2]-80)<.001f);
city::Scene scene;std::string log;assert(!scene.load("/missing.glb",read,log));log.clear();bool ok=scene.load("sys/android/app/src/main/assets/models/abandoned_city_street_for_games.glb",read,log);std::cout<<log<<std::endl;assert(ok);assert(scene.batches.size()==92);assert(scene.images.size()==8);assert(scene.materials.size()==6);assert(!scene.colliders.empty());sandbox::World world;world.walls=scene.colliders;world.x=scene.spawnX;world.z=scene.spawnZ;assert(!world.blocked({world.x,0,world.z,.6f,2,.6f,0,0,0}));for(auto&i:scene.images){std::cout<<i.width<<'x'<<i.height<<'\n';assert(i.width>0&&i.height>0&&i.rgba.size()==size_t(i.width)*i.height*4);}std::cout<<"spawn "<<scene.spawnX<<' '<<scene.spawnZ<<" bounds "<<scene.minimum[0]<<' '<<scene.minimum[1]<<' '<<scene.minimum[2]<<" -> "<<scene.maximum[0]<<' '<<scene.maximum[1]<<' '<<scene.maximum[2]<<std::endl;}
