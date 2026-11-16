#pragma once
#include "ObjectOverlap.h"
namespace sandbox {
// The same oriented solid is used by overlap, support rays and swept contacts.
// Frozen/anchored bodies remain solid; only their inverse mass is zero.
inline void appendBodySolid(physics::CollisionMesh&mesh,const Object&o){
 physics::Vec3 v[8];int n=0;
 for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f}){
  auto p=objectCorner(o,x,y,z);
  if(o.type==3&&std::abs(o.doorAngle)>.0001f){
   float hx=-.55f,hz=0;for(int i=0;i<o.rotation;i++){float old=hx;hx=hz;hz=-old;}
   float px=p.x-o.x-hx,pz=p.z-o.z-hz,c=std::cos(o.doorAngle),s=std::sin(o.doorAngle);
   p.x=o.x+hx+c*px+s*pz;p.z=o.z+hz-s*px+c*pz;
  }
  v[n++]=p;
 }
 constexpr int faces[12][3]={{0,1,3},{0,3,2},{4,6,7},{4,7,5},{0,4,5},{0,5,1},{2,3,7},{2,7,6},{0,2,6},{0,6,4},{1,5,7},{1,7,3}};
 for(const auto&f:faces)mesh.add({v[f[0]],v[f[1]],v[f[2]]});
}
struct ContactMaterial {
 float staticFriction=.7f,dynamicFriction=.5f,restitution=.1f;
};
inline ContactMaterial contactMaterial(const Object&o){
 if(o.type==8)return {.9f,.7f,.05f};
 if(o.type==11)return {.6f,.4f,.1f};
 return {};
}
inline void applyContactFriction(Object&o,float dt,float gravity=18.f){
 auto material=contactMaterial(o);float speed=std::hypot(o.vx,o.vz);
 float stop=material.staticFriction*gravity*dt;
 if(speed<=stop){o.vx=o.vz=0;return;}
 float scale=std::max(0.f,1-material.dynamicFriction*gravity*dt/speed);
 o.vx*=scale;o.vz*=scale;
}
}
