#include "../../sys/android/app/src/main/cpp/GltfScene.h"
#include "../../sys/android/app/src/main/cpp/LevelPlacement.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main(){
 city::Scene scene;std::string log;city::LoadOptions options;options.targetExtent=998;
 auto read=[](const std::string&p,std::vector<unsigned char>&b){std::ifstream f(p,std::ios::binary);if(!f)return false;b.assign(std::istreambuf_iterator<char>(f),{});return true;};
 assert(scene.load("sys/android/app/src/main/assets/models/destroyed_city.glb",read,log,options));
 assert(scene.batches.size()>50&&scene.batches.size()<200);
 assert(scene.materials.size()==3&&scene.images.size()==1);
 assert(scene.maximum[0]-scene.minimum[0]>997);
 size_t triangles=0;bool colored=false;
 for(const auto&batch:scene.batches){triangles+=batch.vertices.size()/3;for(const auto&v:batch.vertices)if(v.r!=1||v.g!=1||v.b!=1)colored=true;}
 assert(triangles>300000&&triangles<350000&&colored);
 const auto&image=scene.images[0];assert(image.width==512&&image.height==512);
 bool varied=false;for(size_t i=4;i<image.rgba.size();i+=4)if(image.rgba[i]!=image.rgba[0]){varied=true;break;}assert(varied);
 assert(scene.collision&&scene.collision->size()>300000);
 sandbox::World world;world.walls=scene.colliders;world.wallSegments=scene.wallSegments;
 world.x=scene.spawnX;world.z=scene.spawnZ;
 assert(!world.blocked({world.x,0,world.z,.62f,2,.62f,0,0,0}));
 city::SpawnPoint spawn;assert(city::findRoadSpawn(world,scene.minimum,scene.maximum,world.x,world.z,spawn));
 float x=0,z=0;assert(city::findCarParking(world,scene.minimum,scene.maximum,spawn.x+3,spawn.z-9,x,z));
 // Recovery must work away from the origin on the kilometre-wide map, too.
 assert(city::findRoadSpawn(world,scene.minimum,scene.maximum,300,300,spawn));
 std::cout<<log<<" triangles="<<triangles<<" parking="<<x<<","<<z<<"\n";
}
