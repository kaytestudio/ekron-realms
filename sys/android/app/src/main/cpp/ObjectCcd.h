#pragma once
#include "SandboxWorld.h"
#include "CollisionMesh.h"
namespace sandbox {
inline physics::Vec3 objectCorner(const Object&o,float x,float y,float z){
 auto b=World::bounds(o);x*=b.w/2;y*=b.h/2;z*=b.d/2;
 float py=std::cos(o.tiltX)*y-std::sin(o.tiltX)*z,pz=std::sin(o.tiltX)*y+std::cos(o.tiltX)*z;
 return {o.x+std::cos(o.tiltZ)*x-std::sin(o.tiltZ)*py,b.y+std::sin(o.tiltZ)*x+std::cos(o.tiltZ)*py,o.z+pz};
}
// Sweep actual rotated corners, not just upright XZ support probes. Angular
// subdivision bounds the curved corner path; floor hits stop at time of impact.
inline bool sweepObjectFloor(const Object&before,Object&after,const physics::CollisionMesh&mesh,bool includeWalls=false){
 int steps=std::clamp(int(std::ceil((std::abs(after.tiltX-before.tiltX)+std::abs(after.tiltZ-before.tiltZ))/.025f)),1,32);
 auto at=[&](float t){Object o=before;o.x+=(after.x-before.x)*t;o.y+=(after.y-before.y)*t;o.z+=(after.z-before.z)*t;o.tiltX+=(after.tiltX-before.tiltX)*t;o.tiltZ+=(after.tiltZ-before.tiltZ)*t;return o;};
 float earliest=2;physics::Vec3 contactNormal{},contactPoint{};
 for(int part=0;part<steps;part++){
  auto a=at(float(part)/steps),b=at(float(part+1)/steps);
  for(float x:{-1.f,0.f,1.f})for(float y:{-1.f,0.f,1.f})for(float z:{-1.f,0.f,1.f}){
   auto p=objectCorner(a,x,y,z),q=objectCorner(b,x,y,z),d=q-p;float length=physics::length(d);
   if(length<.00001f||(!includeWalls&&d.y>=0))continue;
   physics::CollisionMesh::RayHit hit;
   if(mesh.raycast(p,d,length+.0001f,hit)&&(includeWalls||hit.normal.y>.64f)){float t=(part+hit.distance/length)/steps;if(t<earliest){earliest=t;contactNormal=hit.normal;contactPoint=p+d*(hit.distance/length);}}
  }
 }
 if(earliest>1)return false;
 auto contact=at(std::max(0.f,earliest-.001f));after.x=contact.x+contactNormal.x*.004f;after.y=contact.y+contactNormal.y*.004f;after.z=contact.z+contactNormal.z*.004f;after.tiltX=contact.tiltX;after.tiltZ=contact.tiltZ;
 physics::Vec3 velocity{after.vx,after.vy,after.vz};float into=physics::dot(velocity,contactNormal);if(into<0)velocity=velocity-contactNormal*(into*(contactNormal.y>.64f?1.f:1.35f));after.vx=velocity.x;after.vy=velocity.y;after.vz=velocity.z;after.spinX*=.5f;after.spinZ*=.5f;
 // Remove angular velocity INTO the resolved static contact, rather than
 // letting suspension repeatedly wind up against a blocked chassis corner.
 auto bounds=World::bounds(after);auto lever=contactPoint-physics::Vec3{bounds.x,bounds.y,bounds.z};
 auto torque=physics::cross(lever,contactNormal);
 float ix=(bounds.h*bounds.h+bounds.d*bounds.d)/12,iz=(bounds.w*bounds.w+bounds.h*bounds.h)/12;
 float incoming=torque.x*after.spinX+torque.z*after.spinZ,denom=torque.x*torque.x/ix+torque.z*torque.z/iz;
 if(incoming<0&&denom>1e-7f){float impulse=-incoming/denom;after.spinX+=torque.x*impulse/ix;after.spinZ+=torque.z*impulse/iz;}
 return true;
}
}
