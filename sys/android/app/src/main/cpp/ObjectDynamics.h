#pragma once
#include "SandboxWorld.h"
#include "CollisionMesh.h"
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
    auto b=World::bounds(o);physics::Vec3 half{b.w/2,b.h/2,b.d/2};float oldY=o.y,oldX=o.x,oldZ=o.z;
    auto radius=[&](){return std::abs(std::sin(o.tiltZ))*half.x+std::abs(std::cos(o.tiltZ)*std::cos(o.tiltX))*half.y+std::abs(std::cos(o.tiltZ)*std::sin(o.tiltX))*half.z;};
    float oldRadius=radius(),oldBottom=oldY+half.y-oldRadius;
    if(!o.awake){
     o.supportCheckTime+=step;if(o.supportCheckTime<.2f)continue;o.supportCheckTime=0;
     bool supported=false;
     for(float dx:{-half.x*.85f,0.f,half.x*.85f})for(float dz:{-half.z*.85f,0.f,half.z*.85f}){
      float h=mesh.groundHeight(o.x+dx,o.z+dz,oldBottom+.08f,oldBottom-.12f);if(std::isfinite(h))supported=true;
      for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].frozen){auto other=World::bounds(w.objects[j]);float top=other.y+other.h/2;if(std::abs(o.x+dx-other.x)<=other.w/2&&std::abs(o.z+dz-other.z)<=other.d/2&&std::abs(oldBottom-top)<.12f)supported=true;}
     }
     if(supported){o.vx=o.vy=o.vz=o.spinX=o.spinZ=0;continue;}
     o.awake=true;o.restTime=0;
    }
    auto collides=[&](float x,float y,float z){
     physics::Vec3 center{x,y+half.y,z},inset{std::max(.01f,half.x-.015f),std::max(.01f,half.y-.04f),std::max(.01f,half.z-.015f)};
     if(mesh.intersectsBox(center,inset))return true;
     for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].frozen){auto other=World::bounds(w.objects[j]);
      if(std::abs(x-other.x)<half.x+other.w/2-.02f&&std::abs(y+half.y-other.y)<half.y+other.h/2-.02f&&std::abs(z-other.z)<half.z+other.d/2-.02f)return true;
     }return false;
    };
    float nx=o.x+o.vx*step,nz=o.z+o.vz*step;
    if(collides(nx,o.y,o.z))o.vx*=-.48f;else o.x=nx;
    if(collides(o.x,o.y,nz))o.vz*=-.48f;else o.z=nz;
    o.vy=std::max(-25.f,o.vy-18.f*step);float nextY=o.y+o.vy*step;
    std::array<float,4> heights{};int probe=0;
    float support=-1e6f;
    for(int ix:{-1,1})for(int iz:{-1,1}){
     float px=o.x+ix*half.x*.85f,pz=o.z+iz*half.z*.85f;
     float h=mesh.groundHeight(px,pz,oldBottom+.12f,lo[1]-20);
     for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].frozen){auto other=World::bounds(w.objects[j]);float top=other.y+other.h/2;
      if(std::abs(px-other.x)<=other.w/2&&std::abs(pz-other.z)<=other.d/2&&top<=oldBottom+.12f)h=std::max(h,top);
     }
     heights[probe++]=h;
     if(std::isfinite(h)){support=std::max(support,h);}
    }
    float centre=mesh.groundHeight(o.x,o.z,oldBottom+.12f,lo[1]-20);
    for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].frozen){auto other=World::bounds(w.objects[j]);float top=other.y+other.h/2;if(std::abs(o.x-other.x)<=other.w/2&&std::abs(o.z-other.z)<=other.d/2&&top<=oldBottom+.12f)centre=std::max(centre,top);}
    if(std::isfinite(centre))support=std::max(support,centre);
    // Only contacts at the highest surface support the object. Far lower ground
    // must not make a one-corner ledge look like a four-wheel support polygon.
    int contacts=0;float balanceX=0,balanceZ=0;
    for(int p=0;p<4;p++)if(std::isfinite(heights[p])&&heights[p]>=support-.22f){contacts++;balanceX+=(p<2?-1.f:1.f);balanceZ+=(p%2?-1.f:1.f);}
    float lowest=*std::min_element(heights.begin(),heights.end());
    bool unstable=(!std::isfinite(centre)||centre<support-.22f)&&(support-lowest>.5f)&&contacts>0&&(contacts<3)&&(std::abs(balanceX)>.5f||std::abs(balanceZ)>.5f);
    if(unstable&&o.vy<=0&&nextY+half.y-oldRadius<=support+.08f){
     float awayX=-balanceX/contacts,awayZ=balanceZ/contacts;
     o.spinX+=awayZ*5*step;o.spinZ-=awayX*5*step;
     o.vx+=awayX*4*step;o.vz+=awayZ*4*step;
     o.x=o.x+awayX*.65f*step;
     o.z=o.z+awayZ*.65f*step;
     support=-1e6f; // no centre-of-mass support: keep falling, never sleep here
    }
    o.tiltX+=o.spinX*step;o.tiltZ+=o.spinZ*step;
    float verticalRadius=radius();
    if(o.vy<=0&&support>-1e5f&&nextY+half.y-verticalRadius<=support+.003f){
     o.spinX*=.85f;o.spinZ*=.85f;
     o.y=support+.003f;o.vy=std::abs(o.vy)>2?-o.vy*.3f:0;
     // Rest on the current rotated body, not on an upright replacement.
     o.y=support+verticalRadius-half.y+.005f;
     // Contact is dissipative. Terrain gradients must not continuously inject
     // angular energy into a stationary car (the former levitation feedback).
     o.vx*=.94f;o.vz*=.94f;
     if(std::hypot(o.vx,o.vz)<.08f&&o.vy==0&&std::hypot(o.spinX,o.spinZ)<.03f&&!unstable){
      o.restTime+=step;
      if(o.restTime>.35f){o.vx=o.vy=o.vz=o.spinX=o.spinZ=0;o.awake=false;}
     }else o.restTime=0;
    }else if(o.vy>0&&collides(o.x,nextY,o.z))o.vy*=-.35f;
    else o.y=nextY;
    // Deep persistent embedding is not a respawn trigger. Try a small upward
    // depenetration; delete after five seconds if the solid still traps the body.
    physics::Vec3 deepHalf=half*.6f;
    bool embedded=mesh.intersectsBox({o.x,o.y+half.y,o.z},deepHalf);
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
