#include "Renderer.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>
#include <android/log.h>
#include <mutex>
#include <vector>
#include <optional>
#include <cstddef>
#include "MeshGeometry.h"
#include "LevelPlacement.h"
#include "DebugPlayer.h"
#include <atomic>
#include <fstream>
#include <android/asset_manager.h>

namespace {
std::mutex commandMutex;
std::vector<int> commands;
float lookDx=0,lookDy=0;
// Retain transient controls and camera when Android recreates only the EGL surface.
// Accessed exclusively by the native game thread, never the Java UI thread.
std::optional<sandbox::World> retainedWorld;
sandbox::HandTool retainedTool=sandbox::HandTool::HAND_EMPTY;
bool retainedFlying=false;
std::atomic<int> loadStage{0},debugNotice{0};
std::atomic<float> verticalInput{0};
std::atomic<float> stickRight{0},stickForward{0};
std::atomic<bool> gameplayReady{false};
std::atomic<int> currentTool{0};
std::atomic<bool> holdingObject{false};
std::mutex snapshotMutex;
std::array<float,4> snapshot{0,1.7f,0,0};
struct LoadGuard {bool complete=false;~LoadGuard(){if(!complete)loadStage=-1;}};
city::SpawnConfig readSpawnConfig(android_app *app){
 city::SpawnConfig config;std::string text,error;
 std::ifstream file(std::string(app->activity->internalDataPath)+"/spawn.json",std::ios::binary|std::ios::ate);
 if(file&&file.tellg()>0&&file.tellg()<=65536){file.seekg(0);text.assign(std::istreambuf_iterator<char>(file),{});}
 else if(auto *asset=AAssetManager_open(app->activity->assetManager,"config/spawn.json",AASSET_MODE_BUFFER)){
  auto size=AAsset_getLength(asset);if(size>0&&size<=65536){text.resize(size);if(AAsset_read(asset,text.data(),size)!=size)text.clear();}AAsset_close(asset);
 }
 if(!text.empty()&&!city::parseSpawnConfig(text,config,error))__android_log_print(ANDROID_LOG_WARN,"Ekron","Invalid spawn config; automatic spawn: %s",error.c_str());
 return config;
}
const char *vertex=R"(#version 300 es
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 uv;
layout(location=4) in float material;
uniform vec3 eye;
uniform vec2 angles;
uniform float aspect;
out vec3 tint;
out vec3 worldNormal;
out vec2 texUV;
flat out int layer;
out vec3 toEye;
void main(){
 vec3 p=position-eye;
 float cy=cos(angles.x),sy=sin(angles.x),cp=cos(angles.y),sp=sin(angles.y);
 vec3 q=vec3(cy*p.x-sy*p.z,p.y,sy*p.x+cy*p.z);
 q=vec3(q.x,cp*q.y+sp*q.z,-sp*q.y+cp*q.z);
 float f=1.37638;
 gl_Position=vec4(q.x*f/aspect,q.y*f,-1.001334*q.z-.200134,-q.z);
 tint=color;worldNormal=normal;texUV=uv;layer=int(material+.5);toEye=p;
})";
const char *fragment=R"(#version 300 es
precision highp float;
precision highp sampler2DArray;
precision highp sampler2D;
precision highp int;
in vec3 tint;
in vec3 worldNormal;
in vec2 texUV;
flat in int layer;
in vec3 toEye;
uniform sampler2DArray albedo;
uniform bool texturesReady;
uniform bool modelPass;
uniform bool modelTextured;
uniform sampler2D modelAlbedo;
uniform vec4 baseColorFactor;
uniform int alphaMode;
uniform float alphaCutoff;
uniform vec3 lightDirection;
uniform vec3 lightColor;
uniform vec3 ambientColor;
out vec4 outColor;
void main(){
 vec3 base=pow(tint,vec3(2.2));
 if(texturesReady)base*=texture(albedo,vec3(texUV,float(layer))).rgb;
 float alpha=1.0;
 if(modelPass){
  vec4 sampleColor=modelTextured?texture(modelAlbedo,texUV):vec4(1.0);
  base=baseColorFactor.rgb*sampleColor.rgb;alpha=baseColorFactor.a*sampleColor.a;
  if(alphaMode==1&&alpha<alphaCutoff)discard;
  if(alphaMode!=2)alpha=1.0;
 }
 vec3 n=normalize(worldNormal);
 if(modelPass&&!gl_FrontFacing)n=-n;
 float diffuse=max(dot(n,normalize(-lightDirection)),0.0);
 // Broad, cool ambient light keeps overcast facades readable without hard shadows.
 float sky=.78+.22*max(n.y,0.0);
 vec3 lit=base*(ambientColor*sky+lightColor*diffuse);
 float d=max(length(toEye)-22.0,0.0);
 float fog=1.0-exp(-d*d*.00022);
 vec3 fogColor=pow(vec3(.56,.61,.63),vec3(2.2));
 outColor=vec4(pow(max(mix(lit,fogColor,fog),vec3(0.0)),vec3(1.0/2.2)),alpha);
})";
const char *skyVertex=R"(#version 300 es
out vec2 screen;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));screen=p*2.0-1.0;gl_Position=vec4(screen,0.0,1.0);})";
const char *skyFragment=R"(#version 300 es
precision highp float;
in vec2 screen;
uniform float aspect;
uniform vec2 angles;
out vec4 outColor;
void main(){
 vec3 ray=normalize(vec3(screen.x*aspect/1.37638,screen.y/1.37638,-1.0));
 float altitude=cos(angles.y)*ray.y-sin(angles.y)*ray.z;
 float blend=smoothstep(0.0,.85,max(altitude,0.0));
 outColor=vec4(mix(vec3(.56,.61,.63),vec3(.32,.41,.51),blend),1.0);
})";
GLuint compile(GLenum type,const char*text){GLuint s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[1024];glGetShaderInfoLog(s,sizeof(log),nullptr,log);__android_log_print(ANDROID_LOG_ERROR,"Ekron","Shader: %s",log);glDeleteShader(s);return 0;}return s;}
GLuint link(const char *vsText,const char *fsText){
 const auto vs=compile(GL_VERTEX_SHADER,vsText),fs=compile(GL_FRAGMENT_SHADER,fsText);
 if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return 0;}
 GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);glDeleteShader(vs);glDeleteShader(fs);
 GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[1024];glGetProgramInfoLog(p,sizeof(log),nullptr,log);__android_log_print(ANDROID_LOG_ERROR,"Ekron","Program: %s",log);glDeleteProgram(p);return 0;}return p;
}
using Vertex=sandbox::MeshVertex;
void cube(std::vector<Vertex>&v,const sandbox::Box&b){sandbox::appendCube(v,b);}
void object(std::vector<Vertex>&v,const sandbox::Object&o,bool preview=false,bool valid=false){
 auto body=sandbox::World::bounds(o);
 if(preview){body.r=valid?.25f:.9f;body.g=valid?.8f:.25f;body.b=.4f;body.material=sandbox::Plain;cube(v,body);return;}
 // Local-space furniture parts rotated by 90 degree increments.
 auto part=[&](float x,float y,float z,float w,float h,float d,float r,float g,float b){
  for(int i=0;i<o.rotation;i++){float old=x;x=z;z=-old;std::swap(w,d);}
  cube(v,{o.x+x,y+o.y,o.z+z,w,h,d,r,g,b,sandbox::Plain});
 };
 if(o.type==4||o.type==5){bool table=o.type==4;float w=table?1.8f:.6f,d=table?1.f:.65f,top=table?.82f:.46f;
  part(0,top,0,w,.15f,d,.54f,.4f,.27f);
  for(int a:{-1,1})for(int b:{-1,1})part(a*(w/2-.07f),top/2,b*(d/2-.07f),.1f,top,.1f,.43f,.31f,.2f);
  if(!table)part(0,.79f,-.27f,w,.6f,.12f,.54f,.4f,.27f);
 }else if(o.type==2){
  for(int side:{-1,1}){part(side*.96f,1.2f,0,.08f,2.4f,.22f,.68f,.69f,.63f);part(0,1.2f+side*1.16f,0,2,.08f,.22f,.68f,.69f,.63f);}part(0,1.2f,0,.06f,2.4f,.2f,.68f,.69f,.63f);part(0,1.2f,0,1.84f,2.24f,.05f,.29f,.44f,.5f);
 }else if(o.type==7){part(0,.32f,0,2.3f,.6f,.95f,.33f,.4f,.36f);part(0,.78f,-.38f,2.3f,.44f,.2f,.33f,.4f,.36f);for(int a:{-1,1})part(a*1.04f,.65f,0,.22f,.4f,.95f,.33f,.4f,.36f);
 }else cube(v,body);
}
}

extern "C" JNIEXPORT void JNICALL Java_com_example_realms_MainActivity_nativeReady(JNIEnv*,jobject,jboolean ready){gameplayReady=ready;if(!ready){stickRight=0;stickForward=0;verticalInput=0;}}
extern "C" JNIEXPORT void JNICALL Java_com_example_realms_MainActivity_nativeLook(JNIEnv*,jobject,jfloat dx,jfloat dy){
 if(!gameplayReady||!std::isfinite(dx)||!std::isfinite(dy))return;
 std::lock_guard<std::mutex> lock(commandMutex);lookDx=std::clamp(lookDx+dx,-400.f,400.f);lookDy=std::clamp(lookDy+dy,-400.f,400.f);
}
extern "C" JNIEXPORT void JNICALL Java_com_example_realms_MainActivity_nativeMove(JNIEnv*,jobject,jfloat right,jfloat forward){
 stickRight=gameplayReady&&std::isfinite(right)?std::clamp(right,-1.f,1.f):0;
 stickForward=gameplayReady&&std::isfinite(forward)?std::clamp(forward,-1.f,1.f):0;
}
extern "C" JNIEXPORT void JNICALL Java_com_example_realms_MainActivity_nativeCommand(JNIEnv*,jobject,jint command){
 std::lock_guard<std::mutex> lock(commandMutex);if(commands.size()<64)commands.push_back(command);
}

extern "C" JNIEXPORT jfloatArray JNICALL Java_com_example_realms_MainActivity_nativeStatus(JNIEnv *env,jobject){
 std::array<float,8> values{};{std::lock_guard<std::mutex> lock(snapshotMutex);std::copy(snapshot.begin(),snapshot.end(),values.begin());}
 values[6]=holdingObject?1.f:0.f;values[7]=float(currentTool.load());
 values[4]=float(loadStage.load());values[5]=float(debugNotice.load());
 auto result=env->NewFloatArray(values.size());if(result)env->SetFloatArrayRegion(result,0,values.size(),values.data());return result;
}
extern "C" JNIEXPORT void JNICALL Java_com_example_realms_MainActivity_nativeVertical(JNIEnv*,jobject,jfloat value){
 verticalInput=std::isfinite(value)?std::clamp(value,-1.f,1.f):0.f;
}

Renderer::Renderer(android_app *app):app_(app),last_(std::chrono::steady_clock::now()){
 loadStage=0;debugNotice=0;currentTool=0;holdingObject=false;LoadGuard loadGuard;flying_=retainedWorld?retainedFlying:false;
 savePath_=std::string(app->activity->internalDataPath)+"/sandbox-v1.txt";
 if(retainedWorld)world_=*retainedWorld;else world_.load(savePath_);
 tools_.tool=retainedWorld?retainedTool:sandbox::HandTool::HAND_EMPTY;
 building_=tools_.tool==sandbox::HandTool::SPAWN_GUN;currentTool=int(tools_.tool);
 display_=eglGetDisplay(EGL_DEFAULT_DISPLAY);
 if(display_==EGL_NO_DISPLAY||!eglInitialize(display_,nullptr,nullptr))return;
 const EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
 EGLConfig config;EGLint count=0;if(!eglChooseConfig(display_,attributes,&config,1,&count)||count<1)return;
 EGLint format;eglGetConfigAttrib(display_,config,EGL_NATIVE_VISUAL_ID,&format);ANativeWindow_setBuffersGeometry(app_->window,0,0,format);
 surface_=eglCreateWindowSurface(display_,config,app->window,nullptr);
 const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,contextAttributes);
 if(surface_==EGL_NO_SURFACE||context_==EGL_NO_CONTEXT||!eglMakeCurrent(display_,surface_,surface_,context_))return;
 program_=link(vertex,fragment);skyProgram_=link(skyVertex,skyFragment);if(!program_)return;
 loadStage=1;
 texturesReady_=textures_.load(app_->activity->assetManager);
 loadStage=2;
 std::string cityLog;
 if(city_.load(app_->activity->assetManager,"models/abandoned_city_street_for_games.glb",cityLog)){
  world_.scenery.clear();world_.walls=city_.scene().colliders;world_.wallSegments=city_.scene().wallSegments;
  // Keep prototype saves separate: their coordinates refer to a different level.
  savePath_=std::string(app->activity->internalDataPath)+"/city-glb-v1.txt";
  if(!retainedWorld){world_.objects.clear();world_.load(savePath_);}
  loadStage=3;
  // Select an open main-road anchor before parking the vehicle.
  city::SpawnPoint anchor;
  if(city::findRoadSpawn(world_,city_.scene().minimum,city_.scene().maximum,-3.4f,32.7f,anchor)){
   float parkingX=0,parkingZ=0;
   // Keep the car six metres away from the new spawn, even on a cold start.
   float oldX=world_.x,oldZ=world_.z;world_.x=anchor.x;world_.z=anchor.z;
   bool parking=city::findCarParking(world_,city_.scene().minimum,city_.scene().maximum,anchor.x+3,anchor.z-9,parkingX,parkingZ);
   world_.x=oldX;world_.z=oldZ;
   if(parking){
    std::string carLog;city::LoadOptions carOptions;carOptions.level=false;carOptions.targetExtent=4.025f;
    float ground=city_.scene().collision->groundHeight(parkingX,parkingZ,2,-10);
    if(std::isfinite(ground)){
     // Sample the upright car footprint, excluding overhead roofs.
     float support=ground;
     for(float dx:{-1.8f,1.8f})for(float dz:{-.7f,.7f}){
      float h=city_.scene().collision->groundHeight(parkingX+dx,parkingZ+dz,ground+.7f,ground-2);
      if(std::isfinite(h))support=std::max(support,h);
     }
     carOptions.y=support;
    }
    carOptions.x=parkingX;carOptions.z=parkingZ;carOptions.yaw=1.57079632679f;
    if(car_.load(app_->activity->assetManager,"models/uaz_469_1973_classic.glb",carLog,carOptions)){
     // Decoration-only collision policy: the car remains visible but does not block walking.
    }
    __android_log_print(car_.ready()?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,"Ekron","Car at %.2f %.2f: %s",parkingX,parkingZ,carLog.c_str());
   }
  }
  loadStage=4;
  // Final startup validation includes the city, car AND saved player-built objects.
  const auto config=readSpawnConfig(app_);
  if(!retainedWorld&&config.enabled){
   world_.x=config.x;world_.z=config.z;world_.yaw=config.yawDegrees*.01745329252f;world_.pitch=0;
   world_.y=config.y;flying_=config.startFlying;
  }
  player_.setBoundary(city_.scene().minimum[0],city_.scene().maximum[0],city_.scene().minimum[2],city_.scene().maximum[2]);
  player_.reset(world_.x,world_.y,world_.z);
  rebuildBuiltCollision();
  __android_log_print(ANDROID_LOG_INFO,"Ekron","Spawn XYZ %.3f %.3f %.3f flight=%d",world_.x,world_.y,world_.z,flying_);
 }
 __android_log_print(city_.ready()?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,"Ekron","%s",cityLog.c_str());
 glGenVertexArrays(1,&vao_);glGenBuffers(1,&buffer_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);
 glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,x)));
 glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,r)));
 glEnableVertexAttribArray(2);glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,nx)));
 glEnableVertexAttribArray(3);glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,u)));
 glEnableVertexAttribArray(4);glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,layer)));
 glEnable(GL_DEPTH_TEST);eglSwapInterval(display_,1);
 loadStage=5;loadGuard.complete=true;
}
Renderer::~Renderer(){
 tools_.cancel(world_);
 if(saveJob_.valid())saveJob_.get();
 if(saveDirty_)world_.save(savePath_);
 retainedWorld=world_;retainedTool=tools_.tool;retainedFlying=flying_;
 if(display_!=EGL_NO_DISPLAY){if(context_!=EGL_NO_CONTEXT&&surface_!=EGL_NO_SURFACE&&eglMakeCurrent(display_,surface_,surface_,context_)){car_.release();city_.release();textures_.release();if(skyProgram_)glDeleteProgram(skyProgram_);if(buffer_)glDeleteBuffers(1,&buffer_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);}eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context_!=EGL_NO_CONTEXT)eglDestroyContext(display_,context_);if(surface_!=EGL_NO_SURFACE)eglDestroySurface(display_,surface_);eglTerminate(display_);}
}
void Renderer::resetInput(){verticalInput=0;leftId_=rightId_=-1;moveX_=moveY_=0;last_=std::chrono::steady_clock::now();}
void Renderer::handleInput(){
 std::vector<int> pending;{std::lock_guard<std::mutex> lock(commandMutex);pending.swap(commands);if(gameplayReady){world_.yaw-=lookDx*.004f;world_.pitch=std::clamp(world_.pitch-lookDy*.004f,-1.3f,1.3f);}lookDx=lookDy=0;}
 if(!gameplayReady){pending.clear();resetInput();}
 if(roadJob_.valid()&&roadJob_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  auto point=roadJob_.get();if(point){world_.x=(*point)[0];world_.z=(*point)[1];
   float ground=city_.scene().collision->groundHeight(world_.x,world_.z,2,-10);
   world_.y=std::isfinite(ground)?ground+1.7f:1.7f;world_.yaw=world_.pitch=0;flying_=false;player_.reset(world_.x,world_.y,world_.z);resetInput();}
 }
 for(int command:pending){bool changed=false;
  if(command>=20&&command<=23){tools_.select(world_,static_cast<sandbox::HandTool>(command-20));building_=tools_.tool==sandbox::HandTool::SPAWN_GUN;rebuildBuiltCollision();}
  else if(command==31&&tools_.tool==sandbox::HandTool::SPAWN_GUN)world_.rotation=(world_.rotation+3)%4;
  else if(command==32&&tools_.tool==sandbox::HandTool::GRAVITY_GUN){
   if(tools_.holding())changed=tools_.release(world_);
   else tools_.grab(world_,city_.scene().collision.get());
   rebuildBuiltCollision();
  }
  else if(command==33&&tools_.tool==sandbox::HandTool::GRAVITY_GUN)tools_.rotate(world_);
  else if(command==1&&building_)world_.selected=(world_.selected+1)%8;
  else if(command==2&&building_)world_.rotation=(world_.rotation+1)%4;
  else if(command==3&&building_&&city_.ready()){
   auto point=sandbox::RaycastPlacement::preview(world_,*city_.scene().collision,builtCollision_,city_.scene().minimum,city_.scene().maximum);
   if(point){world_.objects.push_back(*point);changed=true;}
  }
  else if(command==4&&tools_.tool==sandbox::HandTool::DELETE_TOOL)changed=world_.remove();
  else if(command==6){flying_=!flying_;verticalInput=0;player_.reset(world_.x,world_.y,world_.z);}
  else if(command==8)player_.jump();
  else if(command==9){crouched_=!crouched_;prone_=false;}
  else if(command==10){prone_=!prone_;crouched_=false;}
  else if(command==7&&!roadJob_.valid()&&city_.ready()){
   // Copy immutable navigation data once. BFS and allocations run only on the worker.
   auto navigation=std::make_shared<sandbox::World>();navigation->walls=world_.walls;navigation->wallSegments=world_.wallSegments;navigation->objects=world_.objects;
   auto lo=city_.scene().minimum,hi=city_.scene().maximum;
   roadJob_=std::async(std::launch::async,[navigation,lo,hi]()->std::optional<std::array<float,2>>{city::SpawnPoint p;if(city::findRoadSpawn(*navigation,lo,hi,-3.4f,32.7f,p))return std::array<float,2>{p.x,p.z};return {};});
  }
  if(changed){saveDirty_=true;rebuildBuiltCollision();}
 }
 currentTool=int(tools_.tool);holdingObject=tools_.holding();
 if(saveJob_.valid()&&saveJob_.wait_for(std::chrono::seconds(0))==std::future_status::ready){if(!saveJob_.get())__android_log_print(ANDROID_LOG_ERROR,"Ekron","Save failed");}
 if(saveDirty_&&!saveJob_.valid()){
  sandbox::World saved;saved.objects=world_.objects;if(tools_.holding())saved.objects[tools_.held]=tools_.original;auto path=savePath_;
  saveJob_=std::async(std::launch::async,[saved=std::move(saved),path](){return saved.save(path);});saveDirty_=false;
 }
 auto *buffer=android_app_swap_input_buffers(app_);if(!buffer)return;
 // Both movement and look are handled by sibling Android views with split pointer IDs.
 // Drain the old GameActivity motion stream: processing it too would rotate twice.
 android_app_clear_motion_events(buffer);android_app_clear_key_events(buffer);
}
void Renderer::render(){
 if(!program_||surface_==EGL_NO_SURFACE)return;
 eglQuerySurface(display_,surface_,EGL_WIDTH,&width_);eglQuerySurface(display_,surface_,EGL_HEIGHT,&height_);if(width_<1||height_<1)return;
 const auto now=std::chrono::steady_clock::now();float dt=std::clamp(std::chrono::duration<float>(now-last_).count(),0.f,.05f);last_=now;if(gameplayReady){if(flying_)city::fly(world_,stickRight.load(),stickForward.load(),verticalInput.load(),dt);
 else if(city_.ready())player_.update(*city_.scene().collision,stickRight.load(),stickForward.load(),world_.yaw,dt,crouched_,world_.x,world_.y,world_.z,&builtCollision_,prone_);
 else world_.move(stickRight.load(),stickForward.load(),dt);}
 if(city_.ready()){
  const auto &lo=city_.scene().minimum,&hi=city_.scene().maximum;
  world_.x=std::clamp(world_.x,lo[0]+.25f,hi[0]-.25f);world_.z=std::clamp(world_.z,lo[2]+.25f,hi[2]-.25f);
 }
 if(gameplayReady)tools_.pull(world_,dt);
 std::vector<Vertex> vertices;vertices.reserve((world_.scenery.size()+world_.objects.size()*10+1)*36);
 for(const auto &b:world_.scenery)cube(vertices,b);for(size_t i=0;i<world_.objects.size();i++){
  if(int(i)==tools_.held){auto visual=world_.objects[i];visual.x=tools_.display.x;visual.z=tools_.display.z;size_t begin=vertices.size();object(vertices,visual);float dy=tools_.display.y-sandbox::World::bounds(visual).y;for(size_t j=begin;j<vertices.size();j++)vertices[j].y+=dy;}
  else object(vertices,world_.objects[i]);
 }
 if(building_&&city_.ready()){
  auto point=sandbox::RaycastPlacement::preview(world_,*city_.scene().collision,builtCollision_,city_.scene().minimum,city_.scene().maximum);
  if(point)object(vertices,*point,true,true);
 }
 glViewport(0,0,width_,height_);glClearColor(.56f,.61f,.63f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glBindVertexArray(vao_);
 if(skyProgram_){glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glUseProgram(skyProgram_);
  glUniform1f(glGetUniformLocation(skyProgram_,"aspect"),float(width_)/height_);
  glUniform2f(glGetUniformLocation(skyProgram_,"angles"),world_.yaw,world_.pitch);
  glDrawArrays(GL_TRIANGLES,0,3);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);
 }
 glUseProgram(program_);textures_.bind();glUniform1i(glGetUniformLocation(program_,"albedo"),0);
 glUniform1i(glGetUniformLocation(program_,"texturesReady"),texturesReady_?1:0);
 glUniform1i(glGetUniformLocation(program_,"modelPass"),0);
 glUniform1i(glGetUniformLocation(program_,"modelAlbedo"),1);
 glUniform3f(glGetUniformLocation(program_,"lightDirection"),-.45f,-1.f,-.25f);
 glUniform3f(glGetUniformLocation(program_,"lightColor"),.55f,.52f,.48f);
 glUniform3f(glGetUniformLocation(program_,"ambientColor"),.55f,.59f,.65f);
 glUniform3f(glGetUniformLocation(program_,"eye"),world_.x,world_.y,world_.z);glUniform2f(glGetUniformLocation(program_,"angles"),world_.yaw,world_.pitch);glUniform1f(glGetUniformLocation(program_,"aspect"),float(width_)/height_);
 glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
 city_.draw(program_,world_.x,world_.z);
 car_.draw(program_,world_.x,world_.z);
 if(eglSwapBuffers(display_,surface_)==EGL_TRUE){
  {std::lock_guard<std::mutex> lock(snapshotMutex);snapshot={world_.x,world_.y,world_.z,flying_?1.f:0.f};}
  loadStage=6;
 }else loadStage=-1;
}

void Renderer::rebuildBuiltCollision(){
 builtCollision_=physics::CollisionMesh{};
 for(size_t i=0;i<world_.objects.size();i++){if(int(i)==tools_.held)continue;const auto&o=world_.objects[i];auto b=sandbox::World::bounds(o);builtCollision_.addBox({b.x-b.w/2,b.y-b.h/2,b.z-b.d/2},{b.x+b.w/2,b.y+b.h/2,b.z+b.d/2});}
 builtCollision_.build();
}
