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
 assert(d.mode==d.Hover&&!d.piloting());
 float hoverHeight=d.position.y;
 for(int i=0;i<500;i++)d.update(.01f,0,0,0,0,0,city,built);
 assert(std::abs(d.position.y-hoverHeight)<.01f);
 assert(d.connect({-2,1.7f,0},0,0,city,built));
 d.leave();for(int i=0;i<100;i++)d.update(.01f,0,0,0,0,0,city,built);
 assert(d.returnHome({0,1.7f,0},0,city,built));
 for(int i=0;i<2500;i++)d.update(.01f,0,0,0,0,0,city,built);
 assert(d.mode==d.Hover&&physics::length(d.position-physics::Vec3{0,1.25f,-.9f})<.1f);
 assert(!d.take({1.05f,1.7f,0},0,city,built));

 // The two sticks have independent axes: yaw/throttle never become strafe.
 sandbox::FpvDrone radio;radio.mode=radio.Flying;radio.position={-5,3,-5};
 for(int i=0;i<100;i++)radio.update(.01f,0,0,1,1,0,city,built);
 assert(radio.position.y>4&&radio.yaw< -1&&std::abs(radio.position.x+5)<.01f&&std::abs(radio.position.z+5)<.01f);
 radio.position={-5,3,-5};radio.velocity={};radio.yaw=0;
 for(int i=0;i<100;i++)radio.update(.01f,0,1,0,0,0,city,built);
 assert(radio.pitch<-.3f&&radio.position.z< -5.5f);
 radio.position={-5,3,-5};radio.velocity={};radio.pitch=0;
 for(int i=0;i<100;i++)radio.update(.01f,1,0,0,0,0,city,built);
 assert(radio.roll>.3f&&radio.position.x> -4.5f);
 radio.bodyEye={-5,1.7f,-5};radio.leave();auto exitPosition=radio.position;
 for(int i=0;i<600;i++)radio.update(.01f,0,0,0,0,0,city,built);
 assert(radio.mode==radio.Hover&&physics::length(radio.position-exitPosition)<.01f);
 // Enclosed return must stop safely, not tunnel through a roof.
 physics::CollisionMesh enclosure;enclosure.addBox({-6,3.3f,-6},{-3,3.5f,-3});enclosure.build();
 radio.position={-5,3,-5};radio.pitch=radio.roll=0;
 assert(radio.returnHome({-4,1.7f,-5},0,city,enclosure));
 for(int i=0;i<2000;i++)radio.update(.01f,0,0,0,0,0,city,enclosure);
 assert(radio.mode==radio.Hover&&!radio.blocked(radio.position,city,enclosure));
 std::cout<<"Drone hand/ground spawn, aim connect, camera transition, locked body, swept wall flight, two-stage exit, linked reconnection and return-to-home hover OK\n";
}
