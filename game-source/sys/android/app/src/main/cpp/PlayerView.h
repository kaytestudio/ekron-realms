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
 void append(std::vector<MeshVertex>&out,physics::Vec3 eye,bool crouch,bool prone,float actualEyeHeight=0)const{
  size_t begin=out.size();float height=actualEyeHeight>0?actualEyeHeight:prone?.4f:crouch?.9f:1.7f;float floor=eye.y-height;
  float u=height/1.7f;
  // Proportioned humanoid: head with hair, neck, jacket torso, pelvis, jointed
  // arms and legs with boots. Local -Z is the facing direction; limbs swing
  // around hip/shoulder pivots while walking. No model asset required.
  auto box=[&](float cx,float cy,float cz,float hx,float hy,float hz,physics::Vec3 color,float pivotY,float angle){
   static constexpr int faces[6][6]={{0,1,2,0,2,3},{5,4,7,5,7,6},{4,0,3,4,3,7},{1,5,6,1,6,2},{3,2,6,3,6,7},{4,5,1,4,1,0}};
   static constexpr float corners[8][3]={{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}};
   static constexpr float normals[6][3]={{0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
   float ca=std::cos(angle),sa=std::sin(angle);
   for(int f=0;f<6;f++)for(int i:faces[f]){
    float x=corners[i][0]*hx,y=cy+corners[i][1]*hy-pivotY,z=cz+corners[i][2]*hz;
    float nx=normals[f][0],ny=normals[f][1]*ca-normals[f][2]*sa,nz=normals[f][1]*sa+normals[f][2]*ca;
    out.push_back({cx+x,pivotY+y*ca-z*sa,y*sa+z*ca,color.x,color.y,color.z,nx,ny,nz,0,0,float(Plain)});
   }
  };
  const physics::Vec3 skin{.72f,.53f,.38f},hair{.15f,.12f,.09f},jacket{.24f,.31f,.22f},sleeve{.28f,.35f,.25f},pants{.16f,.19f,.15f},boots{.09f,.08f,.07f};
  ellipsoid(out,{0,floor+1.585f*u,0},{.105f*u,.125f*u,.115f*u},skin);
  ellipsoid(out,{0,floor+1.64f*u,.01f*u},{.115f*u,.085f*u,.125f*u},hair);
  box(0,floor+1.435f*u,0,.05f*u,.045f*u,.05f*u,skin,floor+1.435f*u,0);      // neck
  box(0,floor+1.21f*u,0,.175f*u,.17f*u,.105f*u,jacket,floor+1.21f*u,0);      // torso
  box(0,floor+.96f*u,0,.155f*u,.08f*u,.095f*u,pants,floor+.96f*u,0);         // pelvis
  float stride=std::sin(walk);
  for(int side:{-1,1}){
   float arm=-side*stride*.4f,leg=side*stride*.5f;
   float shoulder=floor+1.36f*u,hip=floor+.9f*u,ax=side*.235f*u,lx=side*.1f*u;
   box(ax,floor+1.22f*u,0,.055f*u,.14f*u,.06f*u,jacket,shoulder,arm);        // upper arm
   box(ax,floor+.955f*u,0,.048f*u,.125f*u,.052f*u,sleeve,shoulder,arm);      // forearm
   box(ax,floor+.79f*u,0,.045f*u,.05f*u,.05f*u,skin,shoulder,arm);           // hand
   box(lx,floor+.7f*u,0,.08f*u,.2f*u,.09f*u,pants,hip,leg);                  // thigh
   box(lx,floor+.3f*u,0,.065f*u,.2f*u,.07f*u,pants,hip,leg);                 // shin
   box(lx,floor+.05f*u,-.03f*u,.075f*u,.05f*u,.12f*u,boots,hip,leg);         // boot, toe forward
  }
  float c=std::cos(bodyYaw),s=std::sin(bodyYaw);
  for(size_t i=begin;i<out.size();i++){auto&v=out[i];float x=v.x,z=v.z;v.x=eye.x+c*x+s*z;v.z=eye.z-s*x+c*z;x=v.nx;z=v.nz;v.nx=c*x+s*z;v.nz=-s*x+c*z;}
 }
};
}
