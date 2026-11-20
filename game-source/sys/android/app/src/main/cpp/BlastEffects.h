#pragma once
#include "CollisionMesh.h"
#include "MeshGeometry.h"
namespace sandbox {
struct Fragment {physics::Vec3 p,v;float life=0;};
struct Flash {physics::Vec3 p;float age=0;};
class BlastEffects {
 std::vector<Fragment> fragments_;
 std::vector<Flash> flashes_;
public:
 std::array<float,4> light()const{if(flashes_.empty())return {};const auto&f=flashes_.back();return {f.p.x,f.p.y,f.p.z,std::max(0.f,1-f.age/.4f)};}
 size_t fragmentCount()const{return fragments_.size();}
 void explode(World&w,physics::Vec3 point){
  if(flashes_.size()>=16)flashes_.erase(flashes_.begin());
  flashes_.push_back({point,0});
  for(int i=0;i<8;i++){float a=i*6.2831853f/8;Fragment f{point,{std::sin(a)*(3+i*.25f),3+float(i%3),std::cos(a)*(3+i*.25f)},4};if(fragments_.size()>=128)fragments_.erase(fragments_.begin());fragments_.push_back(f);}
  for(auto&o:w.objects){if(o.destroyed||o.frozen||o.anchored)continue;auto b=World::bounds(o);auto d=physics::Vec3{b.x,b.y,b.z}-point;float distance=physics::length(d);if(distance>6)continue;
   auto direction=physics::normalized(d+physics::Vec3{0,.4f,0});float speed=std::min(12.f,120/std::sqrt(World::mass(o)))*(1-distance/6);o.vx+=direction.x*speed;o.vy+=direction.y*speed+speed*.25f;o.vz+=direction.z*speed;o.awake=true;o.restTime=0;}
 }
 void update(float dt,const physics::CollisionMesh&city,const physics::CollisionMesh&built){
  dt=std::clamp(dt,0.f,.05f);
  for(auto&f:flashes_)f.age+=dt;
  flashes_.erase(std::remove_if(flashes_.begin(),flashes_.end(),[](const Flash&f){return f.age>.4f;}),flashes_.end());
  for(auto&f:fragments_){f.life-=dt;f.v.y-=18*dt;auto delta=f.v*dt;int steps=std::max(1,int(std::ceil(physics::length(delta)/.025f)));
   for(int i=0;i<steps;i++){auto next=f.p+delta*(1.f/steps);if(city.intersectsBox(next,{.055f,.055f,.055f})||built.intersectsBox(next,{.055f,.055f,.055f})){f.v=f.v*-.18f;break;}f.p=next;}}
  fragments_.erase(std::remove_if(fragments_.begin(),fragments_.end(),[](const Fragment&f){return f.life<=0;}),fragments_.end());
 }
 void append(std::vector<MeshVertex>&v)const{
  for(const auto&f:fragments_)appendCube(v,{f.p.x,f.p.y,f.p.z,.11f,.11f,.11f,.25f,.22f,.18f,Plain});
  for(const auto&f:flashes_){float s=.2f+std::sin(std::min(1.f,f.age/.4f)*3.14159265f)*1.8f;appendCube(v,{f.p.x,f.p.y,f.p.z,s,s,s,1,.65f,.1f,Plain});}
 }
};
}
