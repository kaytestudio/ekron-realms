#include "../../sys/android/app/src/main/cpp/FpvDrone.h"
#include <cassert>
#include <iostream>
int main(){
 physics::CollisionMesh city,built;city.addBox({-20,-.2f,-20},{20,0,20});city.addBox({1,0,-10},{1.1f,6,10});city.build();built.build();
 sandbox::FpvDrone d;
 assert(d.take({0,1.7f,0},0,city,built));assert(d.mode==d.InHands);
 assert(d.place(city,built));assert(d.mode==d.Ground);
 auto body=physics::Vec3{0,1.7f,0};float pitch=std::atan2(d.position.y-body.y,.75f);
 assert(d.connect(body,0,pitch,city,built));assert(d.piloting());
 auto start=d.camera();assert(physics::length(start-body)<.001f);
 for(int i=0;i<100;i++)d.update(.01f,0,0,0,0,0,city,built);
 assert(d.mode==d.Flying);assert(physics::length(d.bodyEye-body)<.001f);
 for(int i=0;i<200;i++)d.update(.01f,1,0,1,0,0,city,built);
 assert(d.position.x<.79f);assert(d.position.y>2);
 d.leave();assert(d.mode==d.Leaving);
 for(int i=0;i<100;i++)d.update(.01f,0,0,0,0,0,city,built);
 assert(d.mode==d.Ground&&!d.piloting());
 for(int i=0;i<500;i++)d.update(.01f,0,0,0,0,0,city,built);
 assert(d.position.y>=.075f&&d.position.y<.11f);
 assert(!d.take({1.05f,1.7f,0},0,city,built));
 std::cout<<"Drone hand/ground spawn, aim connect, camera transition, locked body, swept wall flight, exit and landing OK\n";
}
