#include "../../sys/android/app/src/main/cpp/PlayerPhysics.h"
#include <cassert>
#include <iostream>
using namespace physics;
void floorQuad(CollisionMesh&m,float y,float x0=-20,float x1=20,float z0=-20,float z1=20){m.add({{x0,y,z0},{x1,y,z1},{x1,y,z0}});m.add({{x0,y,z0},{x0,y,z1},{x1,y,z1}});}
int main(){
 CollisionMesh scene;floorQuad(scene,0);floorQuad(scene,5,-3,3,-3,3);scene.build();
 PlayerController p;float x=0,y=9,z=0;p.reset(x,y,z);
 float old=y;p.update(scene,0,0,0,.016f,false,x,y,z);assert(x==0&&z==0&&y<old&&y>8.9f); // No landing teleport.
 for(int i=0;i<300;i++)p.update(scene,0,0,0,1.f/60,false,x,y,z);
 assert(std::abs(y-6.7f)<.02f&&p.grounded()); // Remain on roof, not ground.
 for(int i=0;i<130;i++)p.update(scene,1,0,0,1.f/60,false,x,y,z);
 for(int i=0;i<200;i++)p.update(scene,0,0,0,1.f/60,false,x,y,z);
 assert(std::abs(y-1.7f)<.02f); // Walk off roof and fall.
 p.jump();for(int i=0;i<15;i++)p.update(scene,0,0,0,1.f/60,false,x,y,z);assert(y>2);
 CollisionMesh pit;floorQuad(pit,-2);floorQuad(pit,0,-20,-2);floorQuad(pit,0,2,20);pit.build();
 x=0;y=1.7f;z=0;p.reset(x,y,z);for(int i=0;i<300;i++)p.update(pit,0,0,0,1.f/60,false,x,y,z);assert(std::abs(y+.3f)<.02f); // Real pit bottom.
 CollisionMesh slope;slope.add({{-10,-2,-10},{10,2,10},{10,2,-10}});slope.add({{-10,-2,-10},{-10,-2,10},{10,2,10}});slope.build();
 x=-5;y=3;z=0;p.reset(x,y,z);for(int i=0;i<300;i++)p.update(slope,0,0,0,1.f/60,false,x,y,z);
 float stable=y;for(int i=0;i<120;i++)p.update(slope,0,0,0,1.f/60,false,x,y,z);assert(std::abs(y-stable)<.005f);
 CollisionMesh window;floorQuad(window,0);window.addBox({-3,0,-.1f},{-1,3,.1f});window.addBox({1,0,-.1f},{3,3,.1f});window.addBox({-1,0,-.1f},{1,.65f,.1f});window.addBox({-1,2,-.1f},{1,3,.1f});window.build();
 // Short capsule fits the actual opening; standing capsule cannot fit through its lintel.
 // Hold a crouched capsule above the solid sill while crossing the empty opening.
 PlayerController opening;x=0;y=2.4f;z=.36f;opening.reset(x,y,z);
 for(int i=0;i<65;i++)opening.update(window,0,1,0,1.f/120,true,x,y,z);
 assert(z<-.3f);
 x=2;y=1.7f;z=1;p.reset(x,y,z);for(int i=0;i<100;i++)p.update(window,0,1,0,1.f/120,false,x,y,z);assert(z>.3f);
 std::cout<<"Gravity, roof landing, pits, stable slopes, jump/crouch window and concrete frame OK\n";
}
