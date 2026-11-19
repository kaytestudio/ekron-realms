#pragma once
#include "MilitaryEmitters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace military {
struct Particle {float x=0,y=0,z=0,vx=0,vy=0,vz=0,age=0,life=0,s0=0,s1=0,r=0,g=0,b=0,a=0;};
struct Point {float x,y,z,size,r,g,b,a;};
class Particles {
 std::array<Particle,3600> smoke_{};
 std::array<Particle,900> flame_{};
 std::array<Point,900> ash_{};
 std::array<float,emitters.size()> acc_{};
 size_t smHead_=0,flHead_=0;
 uint32_t seed_=20240229;
 float time_=0;
 bool initialized_=false;
 float random(){seed_=seed_*1664525u+1013904223u;return float(seed_>>8)/16777216.f;}
 template<size_t N> static void advance(std::array<Particle,N>&pool,float dt,float drag,float buoyancy){
  for(auto&p:pool){if(p.age>=p.life)continue;p.age=std::min(p.life,p.age+dt);p.vx*=1-drag*dt;p.vz*=1-drag*dt;p.vy+=buoyancy*dt;p.x+=p.vx*dt;p.y+=p.vy*dt;p.z+=p.vz*dt;}
 }
 template<size_t N> static void points(const std::array<Particle,N>&pool,std::vector<Point>&out){
  out.clear();for(const auto&p:pool){if(p.age>=p.life||p.life<=0)continue;float f=p.age/p.life;out.push_back({p.x,p.y,p.z,p.s0+(p.s1-p.s0)*f,p.r,p.g,p.b,p.a*std::min(1.f,f*6)*(1-f)});}
 }
public:
 std::vector<Point> smoke,flame,ash;
 float time()const{return time_;}
 void update(float dt,float cx,float cz){
  if(!std::isfinite(dt)||!std::isfinite(cx)||!std::isfinite(cz))return;
  dt=std::clamp(dt,0.f,.05f);if(dt==0)return;time_+=dt;
  if(!initialized_){for(auto&p:ash_)p={cx+(random()-.5f)*50,random()*22,cz+(random()-.5f)*50,.07f,.72f,.69f,.64f,.7f};initialized_=true;}
  for(size_t i=0;i<emitters.size();i++){
   const auto&e=emitters[i];if(!e.far&&std::hypot(e.x-cx,e.z-cz)>170)continue;
   acc_[i]+=dt*(e.fire?22*(e.size/1.5f):e.far?5:3+e.size*.9f);
   while(acc_[i]>1){acc_[i]-=1;Particle p;p.x=e.x+(random()-.5f)*(e.far?8:e.size*(e.fire?.5f:.4f));p.y=e.y+.03f;p.z=e.z+(random()-.5f)*(e.far?8:e.size*(e.fire?.5f:.4f));
    if(e.fire){float hot=random();p.vx=(random()-.5f)*.5f;p.vy=1.2f+random()*1.6f;p.vz=(random()-.5f)*.5f;p.life=.5f+random()*.5f;p.s0=e.size*.9f;p.s1=e.size*.25f;p.r=1;p.g=.45f+hot*.4f;p.b=.1f+hot*.15f;p.a=.8f;flame_[flHead_++%flame_.size()]=p;}
    else{p.vx=e.far?2.2f+random():.9f+random()*.4f;p.vy=e.far?5+random()*2:1.3f+random();p.vz=e.far?.6f+random()*.8f:.3f+random()*.3f;p.life=e.far?20:5+random()*4;p.s0=e.size*(e.far?1:.7f);p.s1=e.size*(e.far?5:3.2f);float g=.14f*(e.far?.9f:.7f+random()*.6f);p.r=g;p.g=g*.97f;p.b=g*.94f;p.a=e.far?.7f:.6f;smoke_[smHead_++%smoke_.size()]=p;}
   }
  }
  advance(smoke_,dt,.12f,.02f);advance(flame_,dt,.4f,.4f);
  for(size_t i=0;i<ash_.size();i++){auto&p=ash_[i];p.x+=(.5f+std::sin(time_*.7f+i)*.4f)*dt;p.y-=(.35f+(i%5)*.08f)*dt;p.z+=std::cos(time_*.5f+i*1.3f)*.3f*dt;if(p.y<0)p.y+=22;while(p.x-cx>25)p.x-=50;while(p.x-cx< -25)p.x+=50;while(p.z-cz>25)p.z-=50;while(p.z-cz< -25)p.z+=50;}
  points(smoke_,smoke);points(flame_,flame);ash.assign(ash_.begin(),ash_.end());
 }
};
}
