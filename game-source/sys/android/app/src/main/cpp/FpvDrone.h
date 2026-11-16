#pragma once
#include "CollisionMesh.h"
namespace sandbox {
// A single recoverable drone; separate from furniture IDs/save format.
struct FpvDrone {
 enum Mode {Absent=0,InHands=1,Ground=2,Entering=3,Flying=4,Leaving=5};
 Mode mode=Absent;physics::Vec3 position{},velocity{},bodyEye{},cameraStart{};
 float yaw=0,pitch=0,bodyYaw=0,bodyPitch=0,exitYaw=0,exitPitch=0,transition=0,fallSpeed=0;
 static constexpr float radius=.22f;
 bool piloting()const{return mode>=Entering;}
 bool blocked(physics::Vec3 p,const physics::CollisionMesh&city,const physics::CollisionMesh&built)const{
  return city.intersectsBox(p,{radius,.075f,radius})||built.intersectsBox(p,{radius,.075f,radius});
 }
 bool take(physics::Vec3 eye,float angle,const physics::CollisionMesh&city,const physics::CollisionMesh&built){
  if(piloting())return false;
  auto p=eye+physics::Vec3{-std::sin(angle)*.75f,-.35f,-std::cos(angle)*.75f};
  physics::CollisionMesh::RayHit hit;auto d=p-eye;float distance=physics::length(d);
  if(blocked(p,city,built)||city.raycast(eye,d,distance,hit)||built.raycast(eye,d,distance,hit))return false;
  position=p;yaw=angle;pitch=0;velocity={};mode=InHands;return true;
 }
 bool place(const physics::CollisionMesh&city,const physics::CollisionMesh&built){
  if(mode!=InHands)return false;
  float floor=std::max(city.groundHeight(position.x,position.z,position.y+.1f,position.y-3),built.groundHeight(position.x,position.z,position.y+.1f,position.y-3));
  if(!std::isfinite(floor))return false;
  auto target=position;target.y=floor+.08f;
  int steps=std::max(1,int(std::ceil(physics::length(target-position)/.03f)));
  for(int i=1;i<=steps;i++)if(blocked(position+(target-position)*(float(i)/steps),city,built))return false;
  position=target;mode=Ground;fallSpeed=0;return true;
 }
 bool aimed(physics::Vec3 eye,float angle,float tilt,const physics::CollisionMesh&city,const physics::CollisionMesh&built)const{
  if(mode!=Ground)return false;
  physics::Vec3 direction{-std::sin(angle)*std::cos(tilt),std::sin(tilt),-std::cos(angle)*std::cos(tilt)};
  auto delta=position-eye;float along=physics::dot(delta,direction);
  if(along<0||along>8||physics::length(delta-direction*along)>.35f)return false;
  physics::CollisionMesh::RayHit hit;
  return !city.raycast(eye,direction,std::max(0.f,along-radius),hit)&&!built.raycast(eye,direction,std::max(0.f,along-radius),hit);
 }
 bool connect(physics::Vec3 eye,float angle,float tilt,const physics::CollisionMesh&city,const physics::CollisionMesh&built){
  if(!aimed(eye,angle,tilt,city,built))return false;
  bodyEye=eye;bodyYaw=angle;bodyPitch=tilt;transition=0;mode=Entering;velocity={};return true;
 }
 float viewYaw()const {float t=blend();if(mode==Leaving)return bodyYaw+std::remainder(exitYaw-bodyYaw,6.28318530718f)*t;return bodyYaw+std::remainder(yaw-bodyYaw,6.28318530718f)*t;}
 float viewPitch()const {return bodyPitch+((mode==Leaving?exitPitch:pitch)-bodyPitch)*blend();}
 void leave(){if(mode==Entering||mode==Flying){cameraStart=camera();exitYaw=viewYaw();exitPitch=viewPitch();transition=0;mode=Leaving;velocity={};}}
 physics::Vec3 camera()const{
  auto droneEye=position+physics::Vec3{-std::sin(yaw)*.12f,.04f,-std::cos(yaw)*.12f};
  float t=std::clamp(transition/.7f,0.f,1.f);t=t*t*(3-2*t);
  if(mode==Entering)return bodyEye+(droneEye-bodyEye)*t;
  if(mode==Leaving)return cameraStart+(bodyEye-cameraStart)*t;
  return droneEye;
 }
 float blend()const {float t=std::clamp(transition/.7f,0.f,1.f);t=t*t*(3-2*t);return mode==Entering?t:mode==Leaving?1-t:1;}
 void update(float dt,float right,float forward,float vertical,float turn,float look,const physics::CollisionMesh&city,const physics::CollisionMesh&built){
  dt=std::clamp(dt,0.f,.05f);
  if(mode==Entering||mode==Leaving){transition+=dt;if(transition>=.7f)mode=mode==Entering?Flying:Ground;return;}
  if(mode!=Flying&&mode!=Ground)return;
  if(mode==Flying){yaw+=turn*.0035f;pitch=std::clamp(pitch-look*.0035f,-1.1f,1.1f);
   physics::Vec3 target{(std::cos(yaw)*right-std::sin(yaw)*forward)*4,vertical*3,(-std::sin(yaw)*right-std::cos(yaw)*forward)*4};
   velocity=velocity+(target-velocity)*(1-std::exp(-7*dt));
  }else {fallSpeed=std::max(-12.f,fallSpeed-18*dt);velocity={0,fallSpeed,0};}
  auto delta=velocity*dt;int steps=std::max(1,int(std::ceil(physics::length(delta)/.025f)));
  for(int i=0;i<steps;i++)for(int axis=0;axis<3;axis++){
   auto p=position;if(axis==0)p.x+=delta.x/steps;if(axis==1)p.y+=delta.y/steps;if(axis==2)p.z+=delta.z/steps;
   if(!blocked(p,city,built))position=p;else {if(axis==0)velocity.x=0;if(axis==1){velocity.y=0;fallSpeed=0;}if(axis==2)velocity.z=0;}
  }
 }
};
}
