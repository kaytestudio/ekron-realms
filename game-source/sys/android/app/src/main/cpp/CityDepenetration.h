#pragma once
#include "BodyContacts.h"
#include "RigidContactMath.h"
namespace sandbox {
struct TriangleContact {physics::Vec3 normal{},point{};float depth=0;};
inline bool bodyTriangleContact(const Object&o,const physics::Triangle&t,TriangleContact&contact,float skin=0){
 physics::Vec3 p[8],centre{};int i=0;
 for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f}){p[i]=bodyContactPoint(o,x,y,z);centre=centre+p[i++];}
 centre=centre*.125f;physics::Vec3 axes[3]={physics::normalized(p[4]-p[0]),physics::normalized(p[2]-p[0]),physics::normalized(p[1]-p[0])};
 physics::Vec3 edges[3]={t.b-t.a,t.c-t.b,t.a-t.c};auto n=physics::normalized(physics::cross(edges[0],edges[1]));
 auto overlaps=[&](physics::Vec3 axis){if(physics::length(axis)<1e-6f)return true;axis=physics::normalized(axis);float low=1e9f,high=-1e9f;
  for(auto point:p){float v=physics::dot(point,axis);low=std::min(low,v);high=std::max(high,v);}
  float a=physics::dot(t.a,axis),b=physics::dot(t.b,axis),c=physics::dot(t.c,axis);return high+skin>=std::min({a,b,c})&&low-skin<=std::max({a,b,c});
 };
 if(!overlaps(n))return false;
 for(auto axis:axes)if(!overlaps(axis))return false;
 for(auto axis:axes)for(auto edge:edges)if(!overlaps(physics::cross(axis,edge)))return false;
 float distance=physics::dot(centre-t.a,n);if(distance<0)n=n*-1;
 float bottom=1e9f;for(auto point:p)bottom=std::min(bottom,physics::dot(point-t.a,n));
 physics::Vec3 support{};int count=0;
 for(auto point:p)if(physics::dot(point-t.a,n)<bottom+.01f){support=support+point;count++;}
 contact={n,physics::closestTriangle(support*(1.f/count),t),-bottom};return skin>0?contact.depth>=-skin:contact.depth>.003f;
}
inline float recoverCityContact(Object&o,const physics::CollisionMesh&mesh){
 float total=0;
 for(int iteration=0;iteration<8&&total<.24f;iteration++){
  physics::Bounds area;for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f})area.add(bodyContactPoint(o,x,y,z));
  TriangleContact deepest;
  mesh.query(area,[&](const physics::Triangle&t){TriangleContact candidate;if(bodyTriangleContact(o,t,candidate)&&candidate.depth>deepest.depth)deepest=candidate;});
  if(deepest.depth<=.003f)break;
  float correction=std::min({deepest.depth+.001f,.08f,.24f-total});
  auto delta=deepest.normal*correction;
  if(deepest.normal.y>.64f)delta={0,correction/deepest.normal.y,0};
  o.x+=delta.x;o.y+=delta.y;o.z+=delta.z;total+=physics::length(delta);
  resolveStaticImpact(o,deepest.point,deepest.normal,0);
 }
 return total;
}
inline bool solveCityContactVelocities(Object&o,const physics::CollisionMesh&mesh,std::vector<physics::Vec3>*support=nullptr){
 physics::Bounds area;for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})for(float z:{-1.f,1.f})area.add(bodyContactPoint(o,x,y,z));
 area.lo=area.lo-physics::Vec3{.008f,.008f,.008f};area.hi=area.hi+physics::Vec3{.008f,.008f,.008f};
 std::vector<TriangleContact>contacts;bool wall=false;
 mesh.query(area,[&](const physics::Triangle&t){
  TriangleContact contact;if(!bodyTriangleContact(o,t,contact,.008f))return;
  if(std::abs(contact.normal.y)<.64f)wall=true;
  // Clip the triangle against all six box planes. A single projected face
  // centre is not a contact manifold: near a pit seam it can lie outside the
  // actual overlap and inject a fresh rocking impulse every step.
  auto origin=bodyContactPoint(o,-1,-1,-1);
  physics::Vec3 edges[]={bodyContactPoint(o,1,-1,-1)-origin,bodyContactPoint(o,-1,1,-1)-origin,bodyContactPoint(o,-1,-1,1)-origin};
  std::vector<physics::Vec3> polygon{t.a,t.b,t.c};
  for(auto edge:edges){float extent=physics::length(edge);auto axis=edge*(1/extent);
   for(int side:{-1,1}){std::vector<physics::Vec3> next;
    auto distance=[&](physics::Vec3 p){float value=physics::dot(p-origin,axis);return side<0?-value-.008f:value-extent-.008f;};
    for(size_t i=0;i<polygon.size();i++){auto a=polygon[i],b=polygon[(i+1)%polygon.size()];float da=distance(a),db=distance(b);
     if(da<=0)next.push_back(a);
     if((da<0)!=(db<0))next.push_back(a+(b-a)*(da/(da-db)));
    }polygon=std::move(next);
   }
  }
  for(auto point:polygon){auto c=contact;c.point=point;contacts.push_back(c);}
 });
 if(support)for(const auto&c:contacts)if(c.normal.y>.64f)support->push_back(c.point);
 auto bounds=World::bounds(o);ContactInertia inertia(o);auto material=contactMaterial(o);
 for(int pass=0;pass<24;pass++)for(size_t k=0;k<contacts.size();k++){
  auto&contact=contacts[pass%2?contacts.size()-1-k:k];float normal=resolveStaticImpact(o,contact.point,contact.normal,0);
  if(normal<=0)continue;
  auto r=contact.point-physics::Vec3{bounds.x,bounds.y,bounds.z};auto v=inertia.velocity(o,r);auto tangent=v-contact.normal*physics::dot(v,contact.normal);float speed=physics::length(tangent);
  if(speed<1e-7f)continue;
  auto direction=tangent*(1/speed);float impulse=std::min(speed/inertia.denominator(r,direction),material.staticFriction*normal);
  inertia.apply(o,r,direction*(-impulse));
 }
 return wall;
}

}
