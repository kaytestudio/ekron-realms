#pragma once
#include "SandboxWorld.h"
#include "CollisionMesh.h"
#include "ObjectCcd.h"
#include "ObjectOverlap.h"
#include "BodyContacts.h"
namespace sandbox {
// Lightweight box dynamics, deliberately separate from the immutable city triangle BVH.
class ObjectDynamics {
 float accumulator_=0;
public:
 bool update(World&w,const physics::CollisionMesh&mesh,float dt,int held,const std::array<float,3>&lo,const std::array<float,3>&hi,int*heldIndex=nullptr,int*seatIndex=nullptr){
  accumulator_+=std::clamp(dt,0.f,.05f);bool changed=false;constexpr float step=1.f/120;
  while(accumulator_>=step){accumulator_-=step;
   for(size_t index=0;index<w.objects.size();index++){
    auto&o=w.objects[index];
    if(!std::isfinite(o.x)||!std::isfinite(o.y)||!std::isfinite(o.z)||o.x<lo[0]||o.x>hi[0]||o.z<lo[2]||o.z>hi[2]||o.y<std::max(-50.f,lo[1]-30)){
     if(int(index)!=held)o.destroyed=true;
    }
    if(o.destroyed)continue;
    if(int(index)==held||o.anchored||o.frozen)continue;
    if(!o.awake){o.supportCheckTime+=step;if(o.supportCheckTime<.2f)continue;o.supportCheckTime=0;}
    auto panel=[](int t){return t==0||t==2||t==3||t==9||t==10||t==11;};
    // Broad phase only selects neighbours; all narrow-phase geometry is rotated.
    physics::CollisionMesh neighbours;
    for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].destroyed){
     auto a=World::bounds(o),b=World::bounds(w.objects[j]);
     float reach=(a.w+a.h+a.d+b.w+b.h+b.d)*.5f+1;
     if(std::abs(a.x-b.x)<reach&&std::abs(a.y-b.y)<reach&&std::abs(a.z-b.z)<reach)appendBodySolid(neighbours,w.objects[j]);
    }
    neighbours.build();
    auto ground=[&](float x,float z,float top,float bottom){return std::max(mesh.groundHeight(x,z,top,bottom),neighbours.groundHeight(x,z,top,bottom));};
    Object beforeStep=o;
    auto b=World::bounds(o);physics::Vec3 half{b.w/2,b.h/2,b.d/2};float oldY=o.y,oldX=o.x,oldZ=o.z;
    auto radius=[&](){return std::abs(std::sin(o.tiltZ))*half.x+std::abs(std::cos(o.tiltZ)*std::cos(o.tiltX))*half.y+std::abs(std::cos(o.tiltZ)*std::sin(o.tiltX))*half.z;};
    float oldBottom=oldY+half.y-radius();
    if(!o.awake){
     bool supported=false;
     for(float cx:{-1.f,0.f,1.f})for(float cy:{-1.f,0.f,1.f})for(float cz:{-1.f,0.f,1.f}){
      auto p=objectCorner(o,cx,cy,cz);
      float h=ground(p.x,p.z,p.y+.025f,p.y-.035f);
      if(std::isfinite(h)&&std::abs(p.y-h)<.035f)supported=true;
     }
     if(supported){o.vx=o.vy=o.vz=o.spinX=o.spinZ=0;continue;}
     o.awake=true;o.restTime=0;
    }
    auto collides=[&](float x,float y,float z){
     physics::Vec3 center{x,y+half.y,z};
     Object candidate=o;candidate.x=x;candidate.y=y;candidate.z=z;
     if(objectOverlap(mesh,candidate,center))return true;
     return overlapsBodies(w,candidate,int(index),held,center);
    };
    auto impact=[&](bool axisX,float x,float z){
     float &velocity=axisX?o.vx:o.vz;
     Object candidate=o;candidate.x=x;candidate.z=z;
     for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].destroyed){
      auto &other=w.objects[j];
      if(!bodiesOverlap(candidate,other))continue;
      float &otherVelocity=axisX?other.vx:other.vz;
      float direction=(axisX?other.x-o.x:other.z-o.z)>=0?1.f:-1.f;
      float closing=(velocity-otherVelocity)*direction;
      if(closing<=0)continue;
      float inverse=1/World::mass(o),inverseOther=other.frozen||other.anchored?0:1/World::mass(other);
      float impulse=(1+contactMaterial(o).restitution)*closing/(inverse+inverseOther);
      velocity-=direction*impulse*inverse;
      if(inverseOther>0){otherVelocity+=direction*impulse*inverseOther;other.awake=true;other.restTime=0;}
      return;
     }
     velocity=std::abs(velocity)<.5f?0:-velocity*.35f;
    };
    float nx=o.x+o.vx*step,nz=o.z+o.vz*step;
    if(collides(nx,o.y,o.z))impact(true,nx,o.z);else o.x=nx;
    if(collides(o.x,o.y,nz))impact(false,o.x,nz);else o.z=nz;
    const float airDrag=std::exp(-.015f*step),angularDrag=std::exp(-.1f*step);
    o.vx*=airDrag;o.vz*=airDrag;o.spinX*=angularDrag;o.spinZ*=angularDrag;
    o.vy=std::max(-25.f,o.vy-18.f*step);float nextY=o.y+o.vy*step;
    std::array<float,4> heights{};int probe=0;
    float support=-1e6f;
    for(int ix:{-1,1})for(int iz:{-1,1}){
     float px=o.x+ix*half.x*.85f,pz=o.z+iz*half.z*.85f;
     float h=ground(px,pz,oldBottom+.12f,lo[1]-20);
     heights[probe++]=h;
     if(std::isfinite(h)){support=std::max(support,h);}
    }
    float centre=ground(o.x,o.z,oldBottom+.12f,lo[1]-20);
    if(std::isfinite(centre))support=std::max(support,centre);
    // Only contacts at the highest surface support the object. Far lower ground
    // must not make a one-corner ledge look like a four-wheel support polygon.
    int contacts=0;float balanceX=0,balanceZ=0;
    for(int p=0;p<4;p++)if(std::isfinite(heights[p])&&heights[p]>=support-.22f){contacts++;balanceX+=(p<2?-1.f:1.f);balanceZ+=(p%2?-1.f:1.f);}
    float lowest=*std::min_element(heights.begin(),heights.end());
    bool unstable=(!std::isfinite(centre)||centre<support-.22f)&&(support-lowest>.5f)&&contacts>0&&(contacts<3)&&(std::abs(balanceX)>.5f||std::abs(balanceZ)>.5f);
    o.tiltX+=o.spinX*step;o.tiltZ+=o.spinZ*step;
    float verticalRadius=radius();
    // A surface below the upright footprint is NOT necessarily below the
    // tilted body. Compute the permitted centre height from actual corners.
    float actualSupport=-1e6f;physics::Vec3 contactPoint{};int contactCount=0;
    for(float cx:{-1.f,0.f,1.f})for(float cy:{-1.f,0.f,1.f})for(float cz:{-1.f,0.f,1.f}){
     auto p=objectCorner(o,cx,cy,cz);
     float h=ground(p.x,p.z,p.y+.04f,lo[1]-20);
     if(std::isfinite(h)){float candidate=h-(p.y-(o.y+half.y-verticalRadius));if(candidate>actualSupport+.002f){actualSupport=candidate;contactPoint=p;contactCount=1;}
      else if(std::abs(candidate-actualSupport)<=.002f){contactPoint=contactPoint+p;contactCount++;}}
    }
    if(contactCount>0)contactPoint=contactPoint*(1.f/contactCount);
    support=actualSupport;
    unstable=unstable&&(!std::isfinite(centre)||centre<actualSupport-.5f);
    if(o.vy<=0&&nextY+half.y-verticalRadius<=actualSupport+.02f&&(!std::isfinite(centre)||centre<actualSupport-.3f)){
     // Gravity about an off-centre support: do not balance indefinitely on
     // a single edge. This is torque, never a forced X/Z translation.
     o.spinZ+=std::clamp((contactPoint.x-o.x)*18.f/(half.x*half.x+half.y*half.y),-8.f,8.f)*step;
     o.spinX-=std::clamp((contactPoint.z-o.z)*18.f/(half.z*half.z+half.y*half.y),-8.f,8.f)*step;
    }

    bool restingContact=false;
    if(o.vy<=0&&support>-1e5f&&nextY+half.y-verticalRadius<=support+.012f){
     restingContact=true;
     o.spinX*=.85f;o.spinZ*=.85f;
     o.y=support+.003f;o.vy=std::abs(o.vy)>2?-o.vy*contactMaterial(o).restitution:0;
     // Rest on the current rotated body, not on an upright replacement.
     o.y=support+verticalRadius-half.y+.005f;
     // A dropped thin door settles onto its broad face on an actually flat
     // support, rather than damping to an impossible balanced diagonal pose.
     bool settlingDoor=false;
     if(panel(o.type)&&std::hypot(o.vx,o.vz)<.5f){
      constexpr float pi=3.14159265f;
      Object settled=o;
      float targetX=half.z<half.x?std::round((o.tiltX-pi/2)/pi)*pi+pi/2:std::round(o.tiltX/pi)*pi;
      float targetZ=half.z<half.x?std::round(o.tiltZ/pi)*pi:std::round((o.tiltZ-pi/2)/pi)*pi+pi/2;
      settled.tiltX+=std::clamp(targetX-o.tiltX,-step*1.8f,step*1.8f);
      settled.tiltZ+=std::clamp(targetZ-o.tiltZ,-step*1.8f,step*1.8f);
      float r=std::abs(std::sin(settled.tiltZ))*half.x+std::abs(std::cos(settled.tiltZ)*std::cos(settled.tiltX))*half.y+std::abs(std::cos(settled.tiltZ)*std::sin(settled.tiltX))*half.z;
      settled.y=support+r-half.y+.005f;
      // Recompute support AFTER rotation; reusing the previous pose's support
      // height rejected every settling step on the actual uneven city ground.
      float correction=-1e6f;
      for(float x:{-1.f,0.f,1.f})for(float y:{-1.f,0.f,1.f})for(float z:{-1.f,0.f,1.f}){
       auto point=objectCorner(settled,x,y,z);
       float h=ground(point.x,point.z,point.y+.25f,point.y-2.f);
       if(std::isfinite(h))correction=std::max(correction,h-point.y+.005f);
      }
      if(correction>-1e5f)settled.y+=std::clamp(correction,-.2f,.2f);
      auto bounds=World::bounds(settled);
      if(!objectOverlap(mesh,settled,{bounds.x,bounds.y,bounds.z})&&!overlapsBodies(w,settled,int(index),held,{bounds.x,bounds.y,bounds.z})){
       o=settled;o.spinX=o.spinZ=0;
       settlingDoor=std::abs(targetX-o.tiltX)+std::abs(targetZ-o.tiltZ)>.01f;
      }
     }
     // Four wheel support rays (model's long axis is X). Align only a
     // near-upright car with four nearby supports, never right an airborne
     // or overturned car. Chassis CCD remains active for side/roof impacts.
     if(o.type==8&&std::cos(o.tiltX)*std::cos(o.tiltZ)>.7f){
      float wheel[4];int n=0;bool four=true;
      for(float x:{-.65f,.65f})for(float z:{-.72f,.72f}){
       auto point=objectCorner(o,x,-1,z);
       wheel[n]=ground(point.x,point.z,point.y+.45f,point.y-.45f);
       if(!std::isfinite(wheel[n]))four=false;
       n++;
      }
      if(four&&*std::max_element(wheel,wheel+4)-*std::min_element(wheel,wheel+4)<.35f){
       float targetZ=std::atan2((wheel[2]+wheel[3]-wheel[0]-wheel[1])*.5f,b.w*.65f);
       float targetX=-std::atan2((wheel[1]+wheel[3]-wheel[0]-wheel[2])*.5f,b.d*.72f);
       Object pose=o;pose.tiltX+=(targetX-o.tiltX)*(1-std::exp(-6.f*step));pose.tiltZ+=(targetZ-o.tiltZ)*(1-std::exp(-6.f*step));
       float r=std::abs(std::sin(pose.tiltZ))*half.x+std::abs(std::cos(pose.tiltZ)*std::cos(pose.tiltX))*half.y+std::abs(std::cos(pose.tiltZ)*std::sin(pose.tiltX))*half.z;
       pose.y=o.y+r-verticalRadius;auto pb=World::bounds(pose);
       if(!objectOverlap(mesh,pose,{pb.x,pb.y,pb.z})&&!overlapsBodies(w,pose,int(index),held,{pb.x,pb.y,pb.z})){
        o=pose;o.spinX=o.spinZ=0;
        settlingDoor=std::abs(targetX-o.tiltX)+std::abs(targetZ-o.tiltZ)>.005f;
       }
      }
     }
     // Contact is dissipative. Terrain gradients must not continuously inject
     // angular energy into a stationary car (the former levitation feedback).
     applyContactFriction(o,step);
     if(std::hypot(o.vx,o.vz)<.08f&&o.vy==0&&std::hypot(o.spinX,o.spinZ)<.03f&&!unstable&&!settlingDoor){
      o.restTime+=step;
      if(o.restTime>.35f){o.vx=o.vy=o.vz=o.spinX=o.spinZ=0;o.awake=false;}
     }else o.restTime=0;
    }else if(o.vy>0&&collides(o.x,nextY,o.z))o.vy*=-.35f;
    else o.y=nextY;
    // A validated slow resting correction already resolves support. Sweeping
    // its angular chord again could rewind it to t=0 on the same floor forever.
    bool resolvedRest=restingContact&&std::abs(beforeStep.vy)<2&&std::hypot(o.vx,o.vz)<.5f
      &&!objectOverlap(mesh,o,{o.x,o.y+half.y,o.z})&&!overlapsBodies(w,o,int(index),held,{o.x,o.y+half.y,o.z});
    if(!resolvedRest){sweepObjectFloor(beforeStep,o,mesh,true);sweepObjectFloor(beforeStep,o,neighbours,true);}
    if(!o.floorRecoveryUsed){
     float lift=0;auto current=World::bounds(o);
     for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f}){
      auto corner=objectCorner(o,x,y,z);if(corner.y>current.y)continue;
      float surface=ground(corner.x,corner.z,corner.y+.18f,corner.y+.003f);
      if(std::isfinite(surface))lift=std::max(lift,surface-corner.y+.005f);
     }
     if(lift>0){o.y+=lift;o.vy=o.spinX=o.spinZ=0;o.floorRecoveryUsed=true;}
    }
    // One bounded rescue only, onto a real nearby surface. Do not turn genuine
    // pits into a global invisible floor or reintroduce an endless respawn loop.
    if(o.y< -5&&!o.floorRecoveryUsed){
     float bottom=o.y+half.y-radius();
     float below=ground(o.x,o.z,bottom+.08f,lo[1]-20);
     float surface=ground(o.x,o.z,std::min(3.f,bottom+12),bottom+.1f);
     if(!std::isfinite(below)&&std::isfinite(surface)){
      o.y=surface+radius()-half.y+.02f;o.vx=o.vy=o.vz=o.spinX=o.spinZ=0;o.floorRecoveryUsed=true;o.restTime=0;
     }
    }
    // Deep persistent embedding is not a respawn trigger. Try a small upward
    // depenetration; delete after five seconds if the solid still traps the body.
    physics::Vec3 deepHalf=half*.6f;
    bool embedded=objectOverlap(mesh,o,{o.x,o.y+half.y,o.z},.6f);
    if(embedded&&std::hypot(o.x-oldX,o.z-oldZ)<.005f){
     o.embeddedTime+=step;
     if(o.embeddedTime<step*1.5f&&!mesh.intersectsBox({o.x,o.y+half.y+.02f,o.z},deepHalf))o.y+=.02f;
     if(o.embeddedTime>5)o.destroyed=true;
    }else o.embeddedTime=0;
    if(o.y<std::max(-50.f,lo[1]-30)||o.x<lo[0]||o.x>hi[0]||o.z<lo[2]||o.z>hi[2])o.destroyed=true;
    changed=true;
   }
  }
  // Reverse erasure preserves surviving state and repairs live grab/seat indices.
  for(size_t i=w.objects.size();i-->0;)if(w.objects[i].destroyed){
   auto repair=[&](int*p){if(p){if(*p==int(i))*p=-1;else if(*p>int(i))--*p;}};
   repair(heldIndex);repair(seatIndex);w.objects.erase(w.objects.begin()+i);changed=true;
  }
  return changed;
 }
};
}
