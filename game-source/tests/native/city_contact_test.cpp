#include "../../sys/android/app/src/main/cpp/GltfScene.h"
#include "../../sys/android/app/src/main/cpp/ObjectDynamics.h"
#include <fstream>
#include <cassert>
#include <iostream>
int main(){
 city::Scene city;std::string log;
 auto read=[](const std::string&p,std::vector<unsigned char>&out){std::ifstream f(p,std::ios::binary);if(!f)return false;out.assign(std::istreambuf_iterator<char>(f),{});return true;};
 assert(city.load("sys/android/app/src/main/assets/models/abandoned_city_street_for_games.glb",read,log));
 using namespace sandbox;
 for(auto position:std::vector<physics::Vec3>{{.8f,0,16.2f},{-3.9f,0,26.2f},{1.6f,0,28.5f}}){
  for(int type:{3,8,12}){
   float floor=city.collision->groundHeight(position.x,position.z,2,-4);assert(std::isfinite(floor));
   World w;ObjectDynamics sim;w.objects={{type,0,position.x,position.z}};w.objects[0].y=floor+1.5f;if(type==3){w.objects[0].tiltX=.4f;w.objects[0].tiltZ=.3f;}
   for(int i=0;i<1800;i++)sim.update(w,*city.collision,1.f/120,-1,city.minimum,city.maximum);
   assert(w.objects.size()==1);auto resting=w.objects[0];
   assert(!resting.awake);
   bool supported=false;
   for(float x:{-1.f,0.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,0.f,1.f}){
    auto point=objectCorner(resting,x,y,z);float h=city.collision->groundHeight(point.x,point.z,point.y+.03f,point.y-.06f);
    if(std::isfinite(h)&&std::abs(point.y-h)<.06f)supported=true;
   }
   assert(supported);
   for(int i=0;i<2400;i++)sim.update(w,*city.collision,1.f/120,-1,city.minimum,city.maximum);
   assert(w.objects.size()==1);
   assert(w.objects[0].x==resting.x&&w.objects[0].y==resting.y&&w.objects[0].z==resting.z);
   std::cout<<"Map rest type="<<type<<" X="<<position.x<<" Z="<<position.z<<" Y="<<resting.y<<'\n';
  }
 }
}
