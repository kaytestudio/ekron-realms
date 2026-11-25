#pragma once
#include "MeshGeometry.h"
namespace sandbox {
inline void appendObject(std::vector<MeshVertex>&v,const sandbox::Object&o,bool preview=false,bool valid=false){
 size_t begin=v.size();
 auto body=sandbox::World::bounds(o);
 body.material=o.type==3||o.type==4||o.type==5||o.type==6?sandbox::Wood:o.type==7?sandbox::Fabric:body.material;
 (void)preview;(void)valid; // Preview uses identical geometry/materials; hologram is a render pass.
 // Local-space furniture parts rotated by 90 degree increments.
 auto part=[&](float x,float y,float z,float w,float h,float d,float r,float g,float b,int material=-1){
  for(int i=0;i<o.rotation;i++){float old=x;x=z;z=-old;std::swap(w,d);}
  appendCube(v,{o.x+x,y+o.y,o.z+z,w,h,d,r,g,b,material>=0?material:o.type==4||o.type==5?sandbox::Wood:o.type==7?sandbox::Fabric:sandbox::Plain});
 };
 // Small smooth knobs, including normals and spherical UVs, rotated with the whole object.
 auto knob=[&](float x,float y,float z,float radius){
  constexpr int rings=6,sides=12;
  auto vertex=[&](int ring,int side){
   float a=3.14159265f*ring/rings,b=6.2831853f*side/sides;
   float nx=std::sin(a)*std::cos(b),ny=std::cos(a),nz=std::sin(a)*std::sin(b);
   float px=x+radius*nx,pz=z+radius*nz;
   for(int turn=0;turn<o.rotation;turn++){float old=px;px=pz;pz=-old;old=nx;nx=nz;nz=-old;}
   return MeshVertex{o.x+px,o.y+y+radius*ny,o.z+pz,.22f,.24f,.25f,nx,ny,nz,float(side)/sides,float(ring)/rings,float(Plain)};
  };
  for(int r=0;r<rings;r++)for(int s=0;s<sides;s++){
   for(auto vtx:{vertex(r,s),vertex(r+1,s),vertex(r+1,s+1),vertex(r,s),vertex(r+1,s+1),vertex(r,s+1)})v.push_back(vtx);
  }
 };
 auto cyl=[&](float cx,float cy,float cz,float rb,float rt,float halfY,float r,float g,float b,int material){appendFrustum(v,o.x+cx,o.y+cy,o.z+cz,rb,rt,halfY,r,g,b,material);};
 // Oriented box between arbitrary directions (hedgehog beams), rotated by the object's 90deg steps.
 auto beam=[&](float cx,float cy,float cz,float dx,float dy,float dz,float len,float t,float r,float g,float b,int material){
  for(int i=0;i<o.rotation;i++){float old=cx;cx=cz;cz=-old;old=dx;dx=dz;dz=-old;}
  float l=std::sqrt(dx*dx+dy*dy+dz*dz);dx/=l;dy/=l;dz/=l;
  float ax=0,ay=1,az=0;if(std::abs(dy)>.9f){ax=1;ay=0;}
  float sx=dy*az-dz*ay,sy=dz*ax-dx*az,sz=dx*ay-dy*ax;float sl=std::sqrt(sx*sx+sy*sy+sz*sz);sx/=sl;sy/=sl;sz/=sl;
  float tx=sy*dz-sz*dy,ty=sz*dx-sx*dz,tz=sx*dy-sy*dx;
  float hl=len/2,ht=t/2,px[8],py[8],pz[8];int k=0;
  for(int a:{-1,1})for(int b2:{-1,1})for(int c:{-1,1}){px[k]=o.x+cx+a*hl*dx+b2*ht*sx+c*ht*tx;py[k]=o.y+cy+a*hl*dy+b2*ht*sy+c*ht*ty;pz[k]=o.z+cz+a*hl*dz+b2*ht*sz+c*ht*tz;k++;}
  const int faces[6][4]={{4,5,7,6},{0,2,3,1},{2,6,7,3},{0,1,5,4},{1,3,7,5},{0,4,6,2}};
  const float normals[6][3]={{dx,dy,dz},{-dx,-dy,-dz},{sx,sy,sz},{-sx,-sy,-sz},{tx,ty,tz},{-tx,-ty,-tz}};
  for(int f=0;f<6;f++){const auto&n=normals[f];
   for(int idx:{0,1,2,0,2,3}){int i=faces[f][idx];v.push_back({px[i],py[i],pz[i],r,g,b,n[0],n[1],n[2],0,0,float(material)});}}
 };
  if(o.type==3){
  part(0,1.15f,0,1.1f,2.3f,.045f,1,1,1,Wood);
  for(int side:{-1,1}){part(.4f,1.05f,side*.035f,.075f,.14f,.025f,.22f,.24f,.25f,Plain);knob(.4f,1.05f,side*.046f,.024f);}
 }else if(o.type==4||o.type==5){bool table=o.type==4;float w=table?1.8f:.6f,d=table?1.f:.65f,top=table?.82f:.46f;
  part(0,top,0,w,.15f,d,.54f,.4f,.27f);
  for(int a:{-1,1})for(int b:{-1,1})part(a*(w/2-.07f),top/2,b*(d/2-.07f),.1f,top,.1f,.43f,.31f,.2f);
  if(!table)part(0,.79f,-.27f,w,.6f,.12f,.54f,.4f,.27f);
 }else if(o.type==6){
  // Deep carcass with an open upper shelving bay and lower cupboard doors.
  part(0,.06f,0,1.4f,.12f,.8f,1,1,1,Wood);part(0,2.04f,0,1.4f,.12f,.8f,1,1,1,Wood);
  for(int side:{-1,1}){
   part(side*.65f,1.05f,0,.1f,1.86f,.8f,1,1,1,Wood);
   part(side*.315f,.53f,.35f,.61f,.8f,.06f,1,1,1,Wood);
   knob(side*.09f,.62f,.39f,.025f);
  }
  part(0,1.05f,-.37f,1.2f,1.86f,.06f,1,1,1,Wood);
  for(float y:{.96f,1.33f,1.69f})part(0,y,0,1.2f,.055f,.73f,1,1,1,Wood);
 }else if(o.type==2){
  for(int side:{-1,1}){part(side*.96f,1.2f,0,.08f,2.4f,.22f,.68f,.69f,.63f,sandbox::Wood);part(0,1.2f+side*1.16f,0,2,.08f,.22f,.68f,.69f,.63f,sandbox::Wood);}part(0,1.2f,0,.06f,2.4f,.2f,.68f,.69f,.63f,sandbox::Wood);part(0,1.2f,0,1.84f,2.24f,.05f,1,1,1,sandbox::Glass);
  }else if(o.type==7){part(0,.32f,0,2.3f,.6f,.95f,.33f,.4f,.36f);part(0,.78f,-.38f,2.3f,.44f,.2f,.33f,.4f,.36f);for(int a:{-1,1})part(a*1.04f,.65f,0,.22f,.4f,.95f,.33f,.4f,.36f);
  }else if(o.type==13){ // wooden shipping crate with visible slats
   part(0,.5f,0,.92f,.92f,.92f,.52f,.31f,.14f,sandbox::Wood);for(float p:{-.28f,0.f,.28f}){part(p,.5f,-.466f,.055f,.82f,.025f,.22f,.12f,.06f,sandbox::Plain);part(-.466f,.5f,p,.025f,.82f,.055f,.22f,.12f,.06f,sandbox::Plain);}
  }else if(o.type==14){ // metal barrel: rounded ribbed drum
   cyl(0,.57f,0,.36f,.30f,.55f,.20f,.25f,.27f,sandbox::Rust);
   cyl(0,.055f,0,.37f,.37f,.05f,.18f,.23f,.25f,sandbox::Rust);
   cyl(0,1.085f,0,.31f,.31f,.05f,.18f,.23f,.25f,sandbox::Rust);
  }else if(o.type==15){ // wooden pallet
   for(float x:{-.48f,0.f,.48f})part(x,.12f,0,.16f,.22f,1.05f,.48f,.28f,.12f,sandbox::Wood);
   for(float z:{-.48f,0.f,.48f})part(0,.28f,z,1.08f,.14f,.14f,.58f,.36f,.16f,sandbox::Wood);
  }else if(o.type==16){ // steel pipe bundle
   for(float y:{.06f,.16f,.26f})part(0,y,0,1.85f,.07f,.07f,.22f,.25f,.27f,sandbox::Rust);
  }else if(o.type==17){ // timber plank stack
   for(int i=0;i<3;i++)part(0,.08f+i*.12f,0,1.85f,.1f,.34f,.48f,.28f,.12f,sandbox::Wood);
  }else if(o.type==18){ // cement bag
   part(0,.35f,0,1.42f,.58f,.34f,.78f,.78f,.7f,sandbox::Concrete);part(0,.66f,0,1.28f,.08f,.3f,.68f,.68f,.6f,sandbox::Plain);
  }else if(o.type==19){ // scaffold frame
   for(float x:{-.72f,.72f})for(float z:{-.16f,.16f})part(x,.42f,z,.08f,.76f,.08f,.35f,.37f,.38f,sandbox::Rust);
   for(float y:{.08f,.76f})part(0,y,0,1.55f,.08f,.42f,.35f,.37f,.38f,sandbox::Rust);
  }else if(o.type==20){ // traffic cone: conical body + round base
   cyl(0,.06f,0,.36f,.36f,.06f,.95f,.38f,.07f,sandbox::Rust);
   cyl(0,.58f,0,.35f,0.f,.50f,1.f,.42f,.08f,sandbox::Rust);
  }else if(o.type==21){ // public waste bin (green)
   part(0,.72f,0,.62f,1.35f,.62f,.06f,.40f,.19f,sandbox::Rust);part(0,1.43f,0,.7f,.12f,.7f,.05f,.14f,.09f,sandbox::Plain);
  }else if(o.type==22){ // street bench
   part(0,.72f,0,1.65f,.14f,.45f,.42f,.25f,.1f,sandbox::Wood);part(0,1.12f,-.18f,1.65f,.65f,.12f,.42f,.25f,.1f,sandbox::Wood);for(float x:{-.62f,.62f})part(x,.35f,0,.12f,.7f,.35f,.25f,.27f,.28f,sandbox::Rust);
  }else if(o.type==23){ // manhole cover
   part(0,.08f,0,.68f,.16f,.68f,.22f,.25f,.27f,sandbox::Rust);part(0,.17f,0,.58f,.04f,.58f,.3f,.33f,.34f,sandbox::Plain);
  }else if(o.type==24){ // concrete road barrier
   part(0,.75f,0,1.72f,1.45f,.4f,.55f,.56f,.53f,sandbox::Concrete);part(0,1.05f,-.21f,1.45f,.12f,.025f,.9f,.75f,.15f,sandbox::Plain);
  }else if(o.type==25){ // loose tire: round torus (solid hub)
   cyl(0,.18f,0,.34f,.34f,.16f,.07f,.075f,.08f,sandbox::Rust);
   cyl(0,.18f,0,.20f,.20f,.165f,.05f,.05f,.055f,sandbox::Plain);
  }else if(o.type==26){ // fence post
   part(0,1.2f,0,.14f,2.3f,.14f,.3f,.32f,.33f,sandbox::Rust);
  }else if(o.type==27){ // steel beam
   part(0,.15f,0,1.9f,.25f,.25f,.25f,.28f,.3f,sandbox::Rust);part(0,.15f,-.13f,1.9f,.12f,.04f,.45f,.47f,.48f,sandbox::Plain);
  }else if(o.type==28){ // outdoor dumpster (green body, dark lid)
   part(0,.35f,0,1.05f,.7f,.7f,.07f,.44f,.22f,sandbox::Rust);part(0,.76f,0,1.15f,.12f,.78f,.06f,.18f,.12f,sandbox::Plain);
  }else if(o.type==29){ // explosive barrel: round red drum with ribs
   cyl(0,.57f,0,.34f,.30f,.55f,.82f,.04f,.02f,sandbox::Plain);
   cyl(0,.06f,0,.35f,.35f,.055f,.15f,.15f,.14f,sandbox::Plain);
   cyl(0,1.08f,0,.31f,.31f,.055f,.15f,.15f,.14f,sandbox::Plain);
  }else if(o.type==36){ // sandbag stack: three staggered rows
   for(int row=0;row<3;row++){int n=row==1?2:3;for(int i=0;i<n;i++)part((i-(n-1)/2.f)*.4f,.14f+row*.24f,0,.38f,.22f,.5f,.62f,.55f,.4f,sandbox::Fabric);}
  }else if(o.type==37){ // razor wire coil: square loops along X
   for(int i=0;i<6;i++){float x=-.7f+i*.28f,h=i%2?.24f:.28f;
    part(x,.33f-h,0,.035f,.035f,h*2,.35f,.37f,.38f,sandbox::Rust);part(x,.33f+h,0,.035f,.035f,h*2,.35f,.37f,.38f,sandbox::Rust);
    part(x,.33f,-h,.035f,h*2,.035f,.35f,.37f,.38f,sandbox::Rust);part(x,.33f,h,.035f,h*2,.035f,.35f,.37f,.38f,sandbox::Rust);}
  }else if(o.type==38){ // anti-tank hedgehog: three crossed steel beams
   beam(0,.75f,0,1,1,0,2.1f,.14f,.3f,.33f,.35f,sandbox::Rust);
   beam(0,.75f,0,-1,1,0,2.1f,.14f,.3f,.33f,.35f,sandbox::Rust);
   beam(0,.75f,0,0,.3f,1,1.8f,.14f,.35f,.38f,.4f,sandbox::Rust);
  }else if(o.type==39){ // street lamp: pole, arm, warm head
   cyl(0,2.2f,0,.09f,.07f,2.2f,.28f,.3f,.32f,sandbox::Rust);
   part(0,4.45f,-.15f,.12f,.1f,.4f,.28f,.3f,.32f,sandbox::Rust);
   part(0,4.4f,-.3f,.3f,.14f,.4f,.9f,.85f,.6f,sandbox::Plain);
  }else if(o.type==40){ // road sign: pole + white plate with blue field
   cyl(0,1.05f,0,.045f,.045f,1.05f,.3f,.32f,.33f,sandbox::Rust);
   part(0,1.85f,0,.7f,.7f,.05f,.85f,.85f,.88f,sandbox::Plain);
   part(0,1.85f,-.03f,.56f,.56f,.02f,.15f,.35f,.7f,sandbox::Plain);
  }else if(o.type==41){ // bricks on a pallet
   for(float x:{-.45f,0.f,.45f})part(x,.07f,0,.14f,.14f,1.f,.48f,.28f,.12f,sandbox::Wood);
   part(0,.17f,0,1.05f,.06f,1.05f,.58f,.36f,.16f,sandbox::Wood);
   for(int row=0;row<4;row++)for(int i=0;i<3;i++)for(int j=0;j<3;j++)part((i-1)*.34f,.28f+row*.14f,(j-1)*.34f,.3f,.12f,.3f,.62f,.3f,.22f,sandbox::Brick);
  }else if(o.type==42){ // gas cylinder: red body, shoulder, valve
   cyl(0,.04f,0,.23f,.23f,.04f,.3f,.32f,.33f,sandbox::Rust);
   cyl(0,.55f,0,.21f,.21f,.5f,.7f,.1f,.08f,sandbox::Plain);
   cyl(0,1.08f,0,.21f,.1f,.08f,.7f,.1f,.08f,sandbox::Plain);
   cyl(0,1.2f,0,.06f,.06f,.06f,.3f,.32f,.33f,sandbox::Rust);
  }else if(o.type==43){ // wooden fence section
   for(float x:{-1.1f,1.1f})part(x,.8f,0,.14f,1.6f,.14f,.4f,.26f,.12f,sandbox::Wood);
   for(int i=0;i<7;i++)part(-.9f+i*.3f,.75f,0,.22f,1.3f,.05f,.52f,.34f,.16f,sandbox::Wood);
   part(0,.45f,-.06f,2.2f,.1f,.04f,.4f,.26f,.12f,sandbox::Wood);part(0,1.15f,-.06f,2.2f,.1f,.04f,.4f,.26f,.12f,sandbox::Wood);
  }else if(o.type==44){ // metal fence section: frame + bars
   for(float x:{-1.15f,1.15f})part(x,.9f,0,.1f,1.8f,.1f,.3f,.33f,.35f,sandbox::Rust);
   part(0,1.72f,0,2.3f,.08f,.06f,.3f,.33f,.35f,sandbox::Rust);part(0,.12f,0,2.3f,.08f,.06f,.3f,.33f,.35f,sandbox::Rust);
   for(int i=0;i<11;i++)part(-1.f+i*.2f,.9f,0,.045f,1.6f,.045f,.35f,.38f,.4f,sandbox::Rust);
  }else if(o.type==45){ // cable spool: wooden flanges, dark wound cable
   cyl(0,.1f,0,.58f,.58f,.1f,.5f,.33f,.15f,sandbox::Wood);
   cyl(0,.575f,0,.32f,.32f,.375f,.12f,.12f,.13f,sandbox::Plain);
   cyl(0,1.05f,0,.58f,.58f,.1f,.5f,.33f,.15f,sandbox::Wood);
  }else if(o.type==46){ // water tank: blue-grey drum with lid and hatch
   cyl(0,.8f,0,.72f,.72f,.75f,.25f,.4f,.5f,sandbox::Rust);
   cyl(0,1.6f,0,.74f,.74f,.05f,.2f,.33f,.42f,sandbox::Rust);
   cyl(0,1.68f,0,.2f,.2f,.04f,.3f,.32f,.33f,sandbox::Plain);
  }else if(o.type==47){ // red toolbox with handle
   part(0,.18f,0,.8f,.32f,.42f,.72f,.12f,.1f,sandbox::Plain);
   part(0,.36f,0,.82f,.06f,.44f,.55f,.1f,.08f,sandbox::Plain);
   part(0,.42f,0,.3f,.05f,.05f,.2f,.2f,.22f,sandbox::Plain);
  }else appendCube(v,body);
 int surface=World::fixedSurface(o.type);
 if(surface>=0)for(size_t i=begin;i<v.size();i++){if(v[i].layer!=Plain||(o.type!=3&&o.type!=6)){v[i].layer=float(surface);if(o.type<13||o.type>=30)v[i].r=v[i].g=v[i].b=1;}}
}
}
