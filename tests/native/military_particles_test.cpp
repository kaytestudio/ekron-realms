#include "../../sys/android/app/src/main/cpp/MilitaryParticles.h"
#include <cassert>
#include <iostream>
int main(){
 military::Particles p;assert(p.smoke.empty());
 for(int i=0;i<300;i++)p.update(1.f/60,24,104);
 assert(!p.smoke.empty()&&!p.flame.empty()&&p.ash.size()==900);
 assert(p.smoke.size()<=3600&&p.flame.size()<=900);
 auto before=p.ash[0];float t=p.time();p.update(0,24,104);assert(p.time()==t);
 p.update(.05f,24,104);assert(p.ash[0].y!=before.y);
 for(const auto*pool:{&p.smoke,&p.flame,&p.ash})for(const auto&v:*pool){assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));assert(v.a>=0&&v.a<=1&&v.size>0);}
 for(int i=0;i<1200;i++)p.update(.05f,-72,24);
 assert(p.smoke.size()<=3600&&p.flame.size()<=900&&p.ash.size()==900);
 p.update(.05f,400,400);for(const auto&v:p.ash){assert(std::abs(v.x-400)<=25&&std::abs(v.z-400)<=25);}
 std::cout<<"Military animation: smoke/flames/ash, finite particles, bounded pools, pause and camera relocation OK\n";
}
