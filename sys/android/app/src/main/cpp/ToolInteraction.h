#pragma once
#include "SandboxWorld.h"
#include "CollisionMesh.h"
#include <optional>
namespace sandbox {
enum class HandTool { HAND_EMPTY=0, SPAWN_GUN=1, GRAVITY_GUN=2, DELETE_TOOL=3 };
class ToolInteraction {
public:
 HandTool tool=HandTool::HAND_EMPTY;
 int held=-1;
 physics::Vec3 display{};
 Object original{};
 bool holding()const{return held>=0;}
 void cancel(World&w){if(holding()&&size_t(held)<w.objects.size())w.objects[held]=original;held=-1;}
 void select(World&w,HandTool next){cancel(w);tool=next;}
 bool grab(World&w,const physics::CollisionMesh*city){
  if(tool!=HandTool::GRAVITY_GUN||holding())return false;
  physics::Vec3 origin{w.x,w.y,w.z},dir{-std::sin(w.yaw)*std::cos(w.pitch),std::sin(w.pitch),-std::cos(w.yaw)*std::cos(w.pitch)};
  float nearest=8;int found=-1;
  for(size_t i=0;i<w.objects.size();i++){
   auto b=World::bounds(w.objects[i]);float lo=0,hi=nearest;
   auto slab=[&](float o,float d,float center,float half){if(std::abs(d)<1e-6f)return std::abs(o-center)<=half;float a=(center-half-o)/d,c=(center+half-o)/d;if(a>c)std::swap(a,c);lo=std::max(lo,a);hi=std::min(hi,c);return lo<=hi;};
   if(slab(origin.x,dir.x,b.x,b.w/2)&&slab(origin.y,dir.y,b.y,b.h/2)&&slab(origin.z,dir.z,b.z,b.d/2)&&lo<nearest){nearest=lo;found=int(i);}
  }
  if(found<0)return false;
  // Reject a sight ray hidden by solid city geometry (including the alpha fence BVH).
  if(city)for(float t=0;t<nearest-.03f;t+=.04f){auto p=origin+dir*t;physics::Bounds area;area.add(p-physics::Vec3{.025f,.025f,.025f});area.add(p+physics::Vec3{.025f,.025f,.025f});bool hit=false;city->query(area,[&](const physics::Triangle&tri){if(physics::length(p-physics::closestTriangle(p,tri))<.03f)hit=true;});if(hit)return false;}
  held=found;original=w.objects[held];w.objects[held].awake=false;auto b=World::bounds(original);display={b.x,b.y,b.z};return true;
 }
 void pull(const World&w,float dt,const physics::CollisionMesh*mesh=nullptr){
  if(!holding())return;
  physics::Vec3 target{w.x-std::sin(w.yaw)*3.5f,w.y+std::sin(w.pitch)*2,w.z-std::cos(w.yaw)*3.5f};
  auto next=display+(target-display)*(1-std::exp(-8*dt));
  if(mesh){auto b=World::bounds(w.objects[held]);physics::Vec3 half{b.w/2-.01f,b.h/2-.01f,b.d/2-.01f};if(mesh->intersectsBox(next,half))return;}
  display=next;
 }
 void rotate(World&w){if(holding())w.objects[held].rotation=(w.objects[held].rotation+1)%4;}
 bool release(World&w){
  if(!holding())return false;
  auto &o=w.objects[held];auto b=World::bounds(o);o.x=display.x;o.z=display.z;o.y=display.y-b.h/2;
  float impulse=o.type==8?10.f:16.f;
  o.vx=-std::sin(w.yaw)*std::cos(w.pitch)*impulse;o.vz=-std::cos(w.yaw)*std::cos(w.pitch)*impulse;o.vy=std::sin(w.pitch)*impulse+2;
  o.awake=true;o.anchored=false;held=-1;return true;
 }
 int target(World&w,const physics::CollisionMesh*city){
  if(tool!=HandTool::GRAVITY_GUN||holding())return -1;
  if(!grab(w,city))return -1;
  int result=held;w.objects[held]=original;held=-1;return result;
 }
};
}
