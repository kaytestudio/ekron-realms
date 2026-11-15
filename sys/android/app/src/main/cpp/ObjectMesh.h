#pragma once
#include "MeshGeometry.h"
namespace sandbox {
inline void appendObject(std::vector<MeshVertex>&v,const sandbox::Object&o,bool preview=false,bool valid=false){
 size_t begin=v.size();
 auto body=sandbox::World::bounds(o);
 body.material=o.type==3||o.type==4||o.type==5||o.type==6?sandbox::Wood:o.type==7?sandbox::Fabric:body.material;
 if(preview){body.r=valid?.25f:.9f;body.g=valid?.8f:.25f;body.b=.4f;body.material=sandbox::Plain;appendCube(v,body);return;}
 // Local-space furniture parts rotated by 90 degree increments.
 auto part=[&](float x,float y,float z,float w,float h,float d,float r,float g,float b,int material=-1){
  for(int i=0;i<o.rotation;i++){float old=x;x=z;z=-old;std::swap(w,d);}
  appendCube(v,{o.x+x,y+o.y,o.z+z,w,h,d,r,g,b,material>=0?material:o.type==4||o.type==5?sandbox::Wood:o.type==7?sandbox::Fabric:sandbox::Plain});
 };
 if(o.type==4||o.type==5){bool table=o.type==4;float w=table?1.8f:.6f,d=table?1.f:.65f,top=table?.82f:.46f;
  part(0,top,0,w,.15f,d,.54f,.4f,.27f);
  for(int a:{-1,1})for(int b:{-1,1})part(a*(w/2-.07f),top/2,b*(d/2-.07f),.1f,top,.1f,.43f,.31f,.2f);
  if(!table)part(0,.79f,-.27f,w,.6f,.12f,.54f,.4f,.27f);
 }else if(o.type==6){
  // Cabinet carcass and separate inset doors instead of one solid cuboid.
  part(0,.08f,0,1.4f,.16f,.65f,1,1,1,Wood);part(0,2.02f,0,1.4f,.16f,.65f,1,1,1,Wood);
  for(int side:{-1,1}){part(side*.65f,1.05f,0,.1f,1.8f,.65f,1,1,1,Wood);part(side*.34f,1.05f,.29f,.65f,1.76f,.07f,1,1,1,Wood);}
  part(0,1.05f,-.29f,1.2f,1.8f,.07f,1,1,1,Wood);
 }else if(o.type==2){
  for(int side:{-1,1}){part(side*.96f,1.2f,0,.08f,2.4f,.22f,.68f,.69f,.63f,sandbox::Wood);part(0,1.2f+side*1.16f,0,2,.08f,.22f,.68f,.69f,.63f,sandbox::Wood);}part(0,1.2f,0,.06f,2.4f,.2f,.68f,.69f,.63f,sandbox::Wood);part(0,1.2f,0,1.84f,2.24f,.05f,1,1,1,sandbox::Glass);
 }else if(o.type==7){part(0,.32f,0,2.3f,.6f,.95f,.33f,.4f,.36f);part(0,.78f,-.38f,2.3f,.44f,.2f,.33f,.4f,.36f);for(int a:{-1,1})part(a*1.04f,.65f,0,.22f,.4f,.95f,.33f,.4f,.36f);
 }else appendCube(v,body);
 int surface=World::fixedSurface(o.type);
 if(surface>=0)for(size_t i=begin;i<v.size();i++){v[i].layer=float(surface);v[i].r=v[i].g=v[i].b=1;}
}
}
