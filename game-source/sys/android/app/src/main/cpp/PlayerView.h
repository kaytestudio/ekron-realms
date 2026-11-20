#pragma once
#include "CollisionMesh.h"
#include "MeshGeometry.h"
namespace sandbox {
inline void ellipsoid(std::vector<MeshVertex>&out,physics::Vec3 p,physics::Vec3 size,physics::Vec3 color){
 constexpr int rings=8,sides=12;
 auto vertex=[&](int r,int s){float a=3.14159265f*r/rings,b=6.2831853f*s/sides;physics::Vec3 n{std::sin(a)*std::cos(b),std::cos(a),std::sin(a)*std::sin(b)};
 return MeshVertex{p.x+n.x*size.x,p.y+n.y*size.y,p.z+n.z*size.z,color.x,color.y,color.z,n.x,n.y,n.z,0,0,float(Plain)};};
 for(int r=0;r<rings;r++)for(int s=0;s<sides;s++)for(auto v:{vertex(r,s),vertex(r+1,s),vertex(r+1,s+1),vertex(r,s),vertex(r+1,s+1),vertex(r,s+1)})out.push_back(v);
}
struct PlayerView {
 bool third=false;float mix=0,bodyYaw=0,walk=0;
 physics::Vec3 previous{};bool initialized=false;
 void toggle(){third=!third;}
 physics::Vec3 camera(physics::Vec3 eye,float yaw,float pitch,float dt,bool enabled,const physics::CollisionMesh&city,const physics::CollisionMesh&built){
  mix=std::clamp(mix+(third?1.f:-1.f)*std::clamp(dt,0.f,.05f)/.55f,0.f,1.f);
  if(!enabled)return eye;
  float t=mix*mix*(3-2*mix);
  physics::Vec3 back{std::sin(yaw)*std::cos(pitch)*3.5f,-std::sin(pitch)*3.5f+.25f,std::cos(yaw)*std::cos(pitch)*3.5f};
  auto delta=back*t;int steps=std::max(1,int(std::ceil(physics::length(delta)/.035f)));auto safe=eye;
  for(int i=1;i<=steps;i++){auto p=eye+delta*(float(i)/steps);if(city.intersectsBox(p,{.16f,.16f,.16f})||built.intersectsBox(p,{.16f,.16f,.16f}))break;safe=p;}
  return safe;
 }
 void updateBody(physics::Vec3 eye,float yaw,float dt){
  if(!initialized){previous=eye;bodyYaw=yaw;initialized=true;}
  auto d=eye-previous;float distance=std::hypot(d.x,d.z);float target=distance>.001f&&distance<1?std::atan2(-d.x,-d.z):yaw;
  bodyYaw+=std::remainder(target-bodyYaw,6.2831853f)*std::min(1.f,dt*12);walk+=std::min(distance,.25f)*7;previous=eye;
 }
 void append(std::vector<MeshVertex>&out,physics::Vec3 eye,bool crouch,bool prone)const{
  size_t begin=out.size();float height=prone?.55f:crouch?1.15f:1.7f;float floor=eye.y-height;
  // Colored ellipsoids: head, torso and articulated limbs; no model asset required.
  ellipsoid(out,{0,floor+height*.94f,0},{.16f,.19f,.16f},{.67f,.48f,.34f});
  ellipsoid(out,{0,floor+height*.62f,0},{.24f,height*.22f,.15f},{.19f,.26f,.18f});
  float swing=std::sin(walk)*.12f;
  for(int side:{-1,1}){ellipsoid(out,{side*.13f,floor+height*.23f,side*swing},{.1f,height*.23f,.1f},{.13f,.16f,.13f});ellipsoid(out,{side*.32f,floor+height*.57f,-side*swing},{.075f,height*.2f,.075f},{.22f,.28f,.18f});}
  float c=std::cos(bodyYaw),s=std::sin(bodyYaw);
  for(size_t i=begin;i<out.size();i++){auto&v=out[i];float x=v.x,z=v.z;v.x=eye.x+c*x+s*z;v.z=eye.z-s*x+c*z;x=v.nx;z=v.nz;v.nx=c*x+s*z;v.nz=-s*x+c*z;}
 }
};
}
