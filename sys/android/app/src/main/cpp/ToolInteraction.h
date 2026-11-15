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
  held=found;original=w.objects[held];auto b=World::bounds(original);display={b.x,b.y,b.z};return true;
 }
 void pull(const World&w,float dt){
  if(!holding())return;
  physics::Vec3 target{w.x-std::sin(w.yaw)*3.5f,w.y+std::sin(w.pitch)*2,w.z-std::cos(w.yaw)*3.5f};
  display=display+(target-display)*(1-std::exp(-8*dt));
 }
 void rotate(World&w){if(holding())w.objects[held].rotation=(w.objects[held].rotation+1)%4;}
 bool release(World&w){
  if(!holding())return false;
  Object candidate=w.objects[held];candidate.x=std::round(display.x*2)/2;candidate.z=std::round(display.z*2)/2;
  // Existing save format supports grounded XZ constructions only. Validate before committing.
  Object saved=w.objects[held];w.objects.erase(w.objects.begin()+held);
  if(!w.canPlace(candidate)){w.objects.insert(w.objects.begin()+held,saved);return false;}
  w.objects.insert(w.objects.begin()+held,candidate);held=-1;return true;
 }
};
}
