#pragma once
#include "SandboxWorld.h"
#include "CollisionMesh.h"
namespace sandbox {
// Lightweight box dynamics, deliberately separate from the immutable city triangle BVH.
class ObjectDynamics {
 float accumulator_=0;
public:
 bool update(World&w,const physics::CollisionMesh&mesh,float dt,int held,const std::array<float,3>&lo,const std::array<float,3>&hi){
  accumulator_+=std::clamp(dt,0.f,.05f);bool changed=false;constexpr float step=1.f/120;
  while(accumulator_>=step){accumulator_-=step;
   for(size_t index=0;index<w.objects.size();index++){
    auto&o=w.objects[index];if(int(index)==held||o.anchored||o.frozen||!o.awake)continue;
    auto b=World::bounds(o);physics::Vec3 half{b.w/2,b.h/2,b.d/2};float oldY=o.y;
    auto collides=[&](float x,float y,float z){
     physics::Vec3 center{x,y+half.y,z},inset{std::max(.01f,half.x-.015f),std::max(.01f,half.y-.04f),std::max(.01f,half.z-.015f)};
     if(mesh.intersectsBox(center,inset))return true;
     for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].frozen){auto other=World::bounds(w.objects[j]);
      if(std::abs(x-other.x)<half.x+other.w/2-.02f&&std::abs(y+half.y-other.y)<half.y+other.h/2-.02f&&std::abs(z-other.z)<half.z+other.d/2-.02f)return true;
     }return false;
    };
    float nx=o.x+o.vx*step,nz=o.z+o.vz*step;
    if(nx-half.x<lo[0]||nx+half.x>hi[0]||collides(nx,o.y,o.z))o.vx*=-.48f;else o.x=nx;
    if(nz-half.z<lo[2]||nz+half.z>hi[2]||collides(o.x,o.y,nz))o.vz*=-.48f;else o.z=nz;
    o.vy=std::max(-25.f,o.vy-18.f*step);float nextY=o.y+o.vy*step;
    std::array<float,4> heights{};int probe=0;
    float support=-1e6f;float left=0,right=0,front=0,back=0;int lc=0,rc=0,fc=0,bc=0;
    for(int ix:{-1,1})for(int iz:{-1,1}){
     float px=o.x+ix*half.x*.85f,pz=o.z+iz*half.z*.85f;
     float h=mesh.groundHeight(px,pz,oldY+.2f,lo[1]-20);
     for(size_t j=0;j<w.objects.size();j++)if(j!=index&&int(j)!=held&&!w.objects[j].frozen){auto other=World::bounds(w.objects[j]);float top=other.y+other.h/2;
      if(std::abs(px-other.x)<=other.w/2&&std::abs(pz-other.z)<=other.d/2&&top<=oldY+.2f)h=std::max(h,top);
     }
     heights[probe++]=h;
     if(std::isfinite(h)){support=std::max(support,h);if(ix<0){left+=h;lc++;}else{right+=h;rc++;}if(iz<0){front+=h;fc++;}else{back+=h;bc++;}}
    }
    // Only contacts at the highest surface support the object. Far lower ground
    // must not make a one-corner ledge look like a four-wheel support polygon.
    int contacts=0;float balanceX=0,balanceZ=0;
    for(int p=0;p<4;p++)if(std::isfinite(heights[p])&&heights[p]>=support-.22f){contacts++;balanceX+=(p<2?-1.f:1.f);balanceZ+=(p%2?-1.f:1.f);}
    bool unstable=contacts>0&&(contacts<3)&&(std::abs(balanceX)>.5f||std::abs(balanceZ)>.5f);
    if(unstable&&o.vy<=0&&nextY<=support+.08f){
     float awayX=-balanceX/contacts,awayZ=balanceZ/contacts;
     o.spinX+=awayZ*5*step;o.spinZ-=awayX*5*step;
     o.vx+=awayX*4*step;o.vz+=awayZ*4*step;
     o.x=std::clamp(o.x+awayX*.65f*step,lo[0]+half.x,hi[0]-half.x);
     o.z=std::clamp(o.z+awayZ*.65f*step,lo[2]+half.z,hi[2]-half.z);
     support=-1e6f; // no centre-of-mass support: keep falling, never sleep here
    }
    o.tiltX+=o.spinX*step;o.tiltZ+=o.spinZ*step;
    if(o.vy<=0&&support>-1e5f&&nextY<=support+.003f){
     o.spinX*=.85f;o.spinZ*=.85f;
     o.y=support+.003f;o.vy=std::abs(o.vy)>2?-o.vy*.3f:0;
     float tx=fc&&bc?std::clamp(-std::atan2(back/bc-front/fc,b.d*.85f),-.45f,.45f):0;
     float tz=lc&&rc?std::clamp(std::atan2(right/rc-left/lc,b.w*.85f),-.45f,.45f):0;
     o.tiltX+=(tx-o.tiltX)*.15f;o.tiltZ+=(tz-o.tiltZ)*.15f;
     if(lc==2&&rc==2&&fc==2&&bc==2&&std::abs(tx)<.44f&&std::abs(tz)<.44f){
      float mean=(left+right)/4;
      o.y=mean-half.y*(1-std::cos(o.tiltX)*std::cos(o.tiltZ))+.01f;
     }
     o.vx*=.98f;o.vz*=.98f;
     if(std::hypot(o.vx,o.vz)<.08f&&o.vy==0&&std::abs(tx-o.tiltX)<.01f&&std::abs(tz-o.tiltZ)<.01f){o.vx=o.vz=0;o.awake=false;}
    }else if(o.vy>0&&collides(o.x,nextY,o.z))o.vy*=-.35f;
    else o.y=nextY;
    // A real hole has no artificial y=0 floor. Last-resort reset only below the entire model.
    if(o.y<lo[1]-30){o.y=lo[1]+5;o.vx=o.vy=o.vz=0;}
    changed=true;
   }
  }return changed;
 }
};
}
