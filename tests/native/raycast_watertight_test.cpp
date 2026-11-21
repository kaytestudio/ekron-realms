// Rays through the edge shared by two triangles must hit the mesh.
// Regression: the diagonal of a box face let aim rays slip through on
// Apple Silicon (FMA + Apple libm rounding), so iOS build previews could
// not stack on top of a block (indoor_placement_test aborted in iOS CI).
#include "../../sys/android/app/src/main/cpp/CollisionMesh.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace physics;
int main(){
 CollisionMesh box;box.addBox({-1.5f,0,-4.5f},{1.5f,.18f,-1.5f});box.build();
 const Vec3 eye{0,1.7f,0};int misses=0,rays=0;
 for(int diagonal=0;diagonal<2;diagonal++)for(int i=-1000;i<=1000;i++){
  const float t=i*.0014f;const Vec3 target{t,.18f,-3+(diagonal?t:-t)};
  CollisionMesh::RayHit hit{};const bool found=box.raycast(eye,target-eye,20,hit);rays++;
  if(!found||std::abs(hit.position.y-.18f)>1e-3f||hit.normal.y<.99f)misses++;
 }
 // Every edge of every face, from several viewpoints.
 for(const Vec3 from:{Vec3{0,1.7f,0},Vec3{4,2,-3},Vec3{-4,.09f,-6},Vec3{.3f,-2,-3}})for(int i=0;i<=400;i++){
  const float t=i/400.f;const Vec3 corners[4]={{-1.5f,.18f,-4.5f},{1.5f,.18f,-4.5f},{1.5f,.18f,-1.5f},{-1.5f,.18f,-1.5f}};
  for(int c=0;c<4;c++){
   const Vec3 a=corners[c],b=corners[(c+1)%4];const Vec3 onEdge=a+(b-a)*t;
   const Vec3 inward=Vec3{-onEdge.x,0,-3-onEdge.z}*.001f;const Vec3 target=onEdge+inward+Vec3{0,-.09f,0};
   CollisionMesh::RayHit hit{};if(!box.raycast(from,target-from,20,hit))misses++;rays++;
  }
 }
 // A miss must still be a miss: tolerance only closes cracks, it does not grow the box.
 CollisionMesh::RayHit hit{};
 assert(!box.raycast(eye,Vec3{1.52f,.18f,-3}-eye,20,hit));
 assert(!box.raycast(eye,Vec3{0,.18f,-1.48f}-eye+Vec3{0,0,.05f},20,hit)||hit.position.z<=-1.5f+1e-3f);
 std::cout<<"raycast watertight: "<<misses<<" misses of "<<rays<<" rays"<<std::endl;
 assert(misses==0);
}
