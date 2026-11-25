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
 bool third=false;float mix=0,bodyYaw=0,walk=0,speed=0,idleTime=0;
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
  bodyYaw+=std::remainder(target-bodyYaw,6.2831853f)*std::min(1.f,dt*12);walk+=std::min(distance,.25f)*7;
  float instant=dt>0?distance/dt:0;speed+=(std::min(instant,6.f)-speed)*std::min(1.f,dt*8);
  idleTime+=dt;previous=eye;
 }
 void append(std::vector<MeshVertex>&out,physics::Vec3 eye,bool crouch,bool prone,float actualEyeHeight=0)const{
  size_t begin=out.size();float height=actualEyeHeight>0?actualEyeHeight:prone?.4f:crouch?.9f:1.7f;float floor=eye.y-height;
  float u=height/1.7f;
  // Living humanoid: two-segment limbs with knee/elbow flex, stride-linked body
  // bob and forward lean, idle breathing and arm sway. Local -Z is the facing
  // direction. Each box rotates around its joint, then its root, then the hip
  // (spine lean), so limbs stay attached while swinging. No model asset needed.
  float amp=std::clamp(speed/3.5f,0.f,1.f);
  float breath=std::sin(idleTime*1.9f)*.008f*u*(1.f-amp);
  float bob=(std::abs(std::sin(walk))-.5f)*.05f*u*amp;
  float lean=-amp*.09f;
  float hip=floor+.9f*u+bob;
  auto box=[&](float cx,float cy,float cz,float hx,float hy,float hz,physics::Vec3 color,float jY,float jA,float rY,float rA,float sA){
   static constexpr int faces[6][6]={{0,1,2,0,2,3},{5,4,7,5,7,6},{4,0,3,4,3,7},{1,5,6,1,6,2},{3,2,6,3,6,7},{4,5,1,4,1,0}};
   static constexpr float corners[8][3]={{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}};
   static constexpr float normals[6][3]={{0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
   float cj=std::cos(jA),sj=std::sin(jA),cr=std::cos(rA),sr=std::sin(rA),cs=std::cos(sA),ss=std::sin(sA);
   for(int f=0;f<6;f++)for(int i:faces[f]){
    float x=corners[i][0]*hx,y=cy+corners[i][1]*hy,z=cz+corners[i][2]*hz;
    float dy=y-jY;y=jY+dy*cj-z*sj;z=dy*sj+z*cj;
    dy=y-rY;y=rY+dy*cr-z*sr;z=dy*sr+z*cr;
    dy=y-hip;y=hip+dy*cs-z*ss;z=dy*ss+z*cs;
    float nx=normals[f][0],ny=normals[f][1],nz=normals[f][2],t;
    t=ny*cj-nz*sj;nz=ny*sj+nz*cj;ny=t;
    t=ny*cr-nz*sr;nz=ny*sr+nz*cr;ny=t;
    t=ny*cs-nz*ss;nz=ny*ss+nz*cs;ny=t;
    out.push_back({cx+x,y,z,color.x,color.y,color.z,nx,ny,nz,0,0,float(Plain)});
   }
  };
  auto tilt=[&](size_t from){float ca=std::cos(lean),sa=std::sin(lean);for(size_t i=from;i<out.size();i++){auto&v=out[i];float dy=v.y-hip,z=v.z;v.y=hip+dy*ca-z*sa;v.z=dy*sa+z*ca;float ny=v.ny,nz=v.nz;v.ny=ny*ca-nz*sa;v.nz=ny*sa+nz*ca;}};
  const physics::Vec3 skin{.72f,.53f,.38f},hair{.15f,.12f,.09f},jacket{.24f,.31f,.22f},sleeve{.28f,.35f,.25f},pants{.16f,.19f,.15f},boots{.09f,.08f,.07f};
  size_t headBegin=out.size();
  ellipsoid(out,{0,floor+1.585f*u+bob+breath,0},{.105f*u,.125f*u,.115f*u},skin);
  ellipsoid(out,{0,floor+1.64f*u+bob+breath,.01f*u},{.115f*u,.085f*u,.125f*u},hair);
  tilt(headBegin);
  box(0,floor+1.435f*u+bob+breath,0,.05f*u,.045f*u,.05f*u,skin,hip,lean,hip,0,0);   // neck
  box(0,floor+1.21f*u+bob+breath,0,.175f*u,.17f*u,.105f*u,jacket,hip,lean,hip,0,0); // torso
  box(0,floor+.96f*u+bob,0,.155f*u,.08f*u,.095f*u,pants,hip,lean*.5f,hip,0,0);      // pelvis
  for(int side:{-1,1}){
   float phase=walk+(side>0?0.f:3.14159265f),swing=std::sin(phase);
   float thigh=swing*.55f*amp;
   float knee=-amp*.85f*std::max(0.f,std::sin(phase-1.1f));        // heel kicks up on swing-through
   float arm=-swing*.45f*amp+std::sin(idleTime*1.9f)*.02f*(1.f-amp);
   float elbow=.12f+amp*.45f*std::max(0.f,-swing);                 // bends more when the arm swings forward
   float shoulder=floor+1.36f*u+bob+breath,elbowY=floor+1.08f*u+bob+breath,kneeY=floor+.5f*u+bob;
   float ax=side*.235f*u,lx=side*.1f*u;
   box(ax,floor+1.22f*u+bob+breath,0,.055f*u,.14f*u,.06f*u,jacket,shoulder,arm,shoulder,0,lean); // upper arm
   box(ax,floor+.955f*u+bob+breath,0,.048f*u,.125f*u,.052f*u,sleeve,elbowY,elbow,shoulder,arm,lean); // forearm
   box(ax,floor+.79f*u+bob+breath,0,.045f*u,.05f*u,.05f*u,skin,elbowY,elbow,shoulder,arm,lean);     // hand
   box(lx,floor+.7f*u+bob,0,.08f*u,.2f*u,.09f*u,pants,hip,thigh,hip,0,0);          // thigh
   box(lx,floor+.3f*u+bob,0,.065f*u,.2f*u,.07f*u,pants,kneeY,knee,hip,thigh,0);    // shin
   box(lx,floor+.05f*u+bob,-.03f*u,.075f*u,.05f*u,.12f*u,boots,kneeY,knee,hip,thigh,0); // boot, toe forward
  }
  float c=std::cos(bodyYaw),s=std::sin(bodyYaw);
  for(size_t i=begin;i<out.size();i++){auto&v=out[i];float x=v.x,z=v.z;v.x=eye.x+c*x+s*z;v.z=eye.z-s*x+c*z;x=v.nx;z=v.nz;v.nx=c*x+s*z;v.nz=-s*x+c*z;}
 }
};
}
