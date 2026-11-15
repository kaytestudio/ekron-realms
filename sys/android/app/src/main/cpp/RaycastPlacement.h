#pragma once
#include "SandboxWorld.h"
#include "CollisionMesh.h"
#include <optional>
namespace sandbox {
class RaycastPlacement {
public:
 static bool valid(const World&w,const Object&o,const physics::CollisionMesh&city,const physics::CollisionMesh&built,const std::array<float,3>&lo,const std::array<float,3>&hi){
  if(o.type<0||o.type>7||!std::isfinite(o.y)||!std::isfinite(o.x)||!std::isfinite(o.z))return false;
  auto b=World::bounds(o);physics::Vec3 center{b.x,b.y,b.z},half{b.w/2,b.h/2,b.d/2};
  if(b.x-half.x<lo[0]||b.x+half.x>hi[0]||b.z-half.z<lo[2]||b.z+half.z>hi[2]||o.y<lo[1]-.1f||o.y+b.h>hi[1]+5)return false;
  // Minimum distance is to the entire volume, not just its pivot. Never surround the player.
  physics::Vec3 closest{std::clamp(w.x,b.x-half.x,b.x+half.x),std::clamp(w.y,b.y-half.y,b.y+half.y),std::clamp(w.z,b.z-half.z,b.z+half.z)};
  if(physics::length(closest-physics::Vec3{w.x,w.y,w.z})<2)return false;
  for(const auto&existing:w.objects){auto other=World::bounds(existing);
   if(std::abs(b.x-other.x)<(b.w+other.w)/2-.003f&&std::abs(b.y-other.y)<(b.h+other.h)/2-.003f&&std::abs(b.z-other.z)<(b.d+other.d)/2-.003f)return false;
  }
  physics::Vec3 inset=half-physics::Vec3{.003f,.003f,.003f};
  return !city.intersectsBox(center,inset)&&!built.intersectsBox(center,inset);
 }
 static std::optional<Object> preview(const World&w,const physics::CollisionMesh&city,const physics::CollisionMesh&built,const std::array<float,3>&lo,const std::array<float,3>&hi){
  physics::Vec3 origin{w.x,w.y,w.z},dir{-std::sin(w.yaw)*std::cos(w.pitch),std::sin(w.pitch),-std::cos(w.yaw)*std::cos(w.pitch)};
  physics::CollisionMesh::RayHit hit{},other{};
  bool found=city.raycast(origin,dir,20,hit);
  if(built.raycast(origin,dir,found?hit.distance:20,other)){hit=other;found=true;}
  if(!found||hit.distance<2||w.objects.size()>=World::limit)return {};
  Object candidate{w.selected,w.rotation,0,0};auto b=World::bounds(candidate);
  float extent=std::abs(hit.normal.x)*b.w/2+std::abs(hit.normal.y)*b.h/2+std::abs(hit.normal.z)*b.d/2;
  auto center=hit.position+hit.normal*(extent+.012f);
  candidate.x=center.x;candidate.z=center.z;candidate.y=center.y-b.h/2;
  candidate.anchored=std::abs(hit.normal.y)<.64f;
  if(!valid(w,candidate,city,built,lo,hi)){
   // Push out along the visible support normal, never through to the other side.
   bool foundFit=false;
   for(float push=.025f;push<=.6f;push+=.025f){
    Object adjusted=candidate;adjusted.x+=hit.normal.x*push;adjusted.y+=hit.normal.y*push;adjusted.z+=hit.normal.z*push;
    if(valid(w,adjusted,city,built,lo,hi)){candidate=adjusted;foundFit=true;break;}
   }
   if(!foundFit)return {};
  }
  return candidate;
 }
};
}
