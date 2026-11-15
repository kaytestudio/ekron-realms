#include "../../sys/android/app/src/main/cpp/ObjectDynamics.h"
#include "../../sys/android/app/src/main/cpp/ToolInteraction.h"
#include <cassert>
#include <iostream>
int main(){using namespace sandbox;using namespace physics;
 CollisionMesh floor;floor.addBox({-20,-.1f,-20},{20,0,20});floor.build();std::array<float,3>lo{-20,-3,-20},hi{20,15,20};World w;ObjectDynamics sim;
 w.objects={{3,0,0,0}};w.objects[0].y=2;w.objects[0].tiltX=.4f;
 for(int i=0;i<1400;i++){sim.update(w,floor,1.f/120,-1,lo,hi);}
 assert(w.objects.size()==1);assert(std::abs(std::cos(w.objects[0].tiltX))<.02f);assert(!w.objects[0].awake);
 w.x=0;w.y=1.7f;w.z=2;w.pitch=-.65f;ToolInteraction tool;tool.select(w,HandTool::GRAVITY_GUN);assert(tool.grab(w,&floor));tool.cancel(w);assert(w.remove()&&w.objects.empty());
 w.objects={{8,0,0,-3}};w.y=1.7f;w.z=0;w.pitch=0;assert(tool.grab(w,&floor));float start=tool.display.y;for(int i=0;i<30;i++)tool.pull(w,1.f/120,&floor);assert(tool.display.y>start+.1f);tool.cancel(w);
 w.objects[0].frozen=true;assert(w.remove()&&w.objects.empty());
 w.objects={{8,0,0,0}};w.objects[0].y=.03f;for(int i=0;i<1200;i++)sim.update(w,floor,1.f/120,-1,lo,hi);auto car=w.objects[0];for(int i=0;i<6000;i++)sim.update(w,floor,1.f/120,-1,lo,hi);assert(w.objects[0].x==car.x&&w.objects[0].z==car.z&&w.objects[0].y==car.y);
 std::cout<<"Door broad-face settling, tilted/frozen deletion, UAZ ground pickup and idle stability OK\n";
}
