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

namespace {
std::mutex commandMutex;
std::vector<int> commands;
// Retain transient controls and camera when Android recreates only the EGL surface.
// Accessed exclusively by the native game thread, never the Java UI thread.
std::optional<sandbox::World> retainedWorld;
bool retainedBuilding = false;
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
  cube(v,{o.x+x,y,o.z+z,w,h,d,r,g,b,sandbox::Plain});
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

extern "C" JNIEXPORT void JNICALL Java_com_example_realms_MainActivity_nativeCommand(JNIEnv*,jobject,jint command){
 std::lock_guard<std::mutex> lock(commandMutex);if(commands.size()<64)commands.push_back(command);
}

Renderer::Renderer(android_app *app):app_(app),last_(std::chrono::steady_clock::now()){
 savePath_=std::string(app->activity->internalDataPath)+"/sandbox-v1.txt";
 if(retainedWorld)world_=*retainedWorld;else world_.load(savePath_);
 building_=retainedBuilding;
 display_=eglGetDisplay(EGL_DEFAULT_DISPLAY);
 if(display_==EGL_NO_DISPLAY||!eglInitialize(display_,nullptr,nullptr))return;
 const EGLint attributes[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
 EGLConfig config;EGLint count=0;if(!eglChooseConfig(display_,attributes,&config,1,&count)||count<1)return;
 EGLint format;eglGetConfigAttrib(display_,config,EGL_NATIVE_VISUAL_ID,&format);ANativeWindow_setBuffersGeometry(app_->window,0,0,format);
 surface_=eglCreateWindowSurface(display_,config,app->window,nullptr);
 const EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};context_=eglCreateContext(display_,config,EGL_NO_CONTEXT,contextAttributes);
 if(surface_==EGL_NO_SURFACE||context_==EGL_NO_CONTEXT||!eglMakeCurrent(display_,surface_,surface_,context_))return;
 program_=link(vertex,fragment);skyProgram_=link(skyVertex,skyFragment);if(!program_)return;
 texturesReady_=textures_.load(app_->activity->assetManager);
 std::string cityLog;
 if(city_.load(app_->activity->assetManager,"models/abandoned_city_street_for_games.glb",cityLog)){
  world_.scenery.clear();world_.walls=city_.scene().colliders;
  // Keep prototype saves separate: their coordinates refer to a different level.
  savePath_=std::string(app->activity->internalDataPath)+"/city-glb-v1.txt";
  if(!retainedWorld){world_.objects.clear();world_.load(savePath_);}
  // Select an open main-road anchor before parking the vehicle.
  city::SpawnPoint anchor;
  if(city::findRoadSpawn(world_,city_.scene().minimum,city_.scene().maximum,0,28,anchor)){
   float parkingX=0,parkingZ=0;
   // Keep the car six metres away from the new spawn, even on a cold start.
   float oldX=world_.x,oldZ=world_.z;world_.x=anchor.x;world_.z=anchor.z;
   bool parking=city::findCarParking(world_,city_.scene().minimum,city_.scene().maximum,anchor.x+3,anchor.z-9,parkingX,parkingZ);
   world_.x=oldX;world_.z=oldZ;
   if(parking){
    std::string carLog;city::LoadOptions carOptions;carOptions.level=false;carOptions.targetExtent=4.3f;
    carOptions.x=parkingX;carOptions.z=parkingZ;carOptions.yaw=1.57079632679f;
    if(car_.load(app_->activity->assetManager,"models/crushed_sports_car.glb",carLog,carOptions)){
     const auto &colliders=car_.scene().colliders;world_.walls.insert(world_.walls.end(),colliders.begin(),colliders.end());
    }
    __android_log_print(car_.ready()?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,"Ekron","Car at %.2f %.2f: %s",parkingX,parkingZ,carLog.c_str());
   }
  }
  // Final startup validation includes the city, car AND saved player-built objects.
  city::SpawnPoint safe;
  if(city::findRoadSpawn(world_,city_.scene().minimum,city_.scene().maximum,0,28,safe)){
   if(!retainedWorld||!safe.playerInRegion||world_.blocked({world_.x,0,world_.z,1.6f,2,1.6f,0,0,0})){
    world_.x=safe.x;world_.z=safe.z;world_.yaw=0;world_.pitch=0;
   }
   __android_log_print(ANDROID_LOG_INFO,"Ekron","Safe spawn %.2f %.2f; connected cells %zu",world_.x,world_.z,safe.reachableCells);
  }else __android_log_print(ANDROID_LOG_ERROR,"Ekron","No safe main-road spawn after applying saved constructions");
 }
 __android_log_print(city_.ready()?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,"Ekron","%s",cityLog.c_str());
 glGenVertexArrays(1,&vao_);glGenBuffers(1,&buffer_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);
 glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,x)));
 glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,r)));
 glEnableVertexAttribArray(2);glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,nx)));
 glEnableVertexAttribArray(3);glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,u)));
 glEnableVertexAttribArray(4);glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,layer)));
 glEnable(GL_DEPTH_TEST);eglSwapInterval(display_,1);
}
Renderer::~Renderer(){
 retainedWorld=world_;retainedBuilding=building_;
 if(display_!=EGL_NO_DISPLAY){if(context_!=EGL_NO_CONTEXT&&surface_!=EGL_NO_SURFACE&&eglMakeCurrent(display_,surface_,surface_,context_)){car_.release();city_.release();textures_.release();if(skyProgram_)glDeleteProgram(skyProgram_);if(buffer_)glDeleteBuffers(1,&buffer_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);}eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context_!=EGL_NO_CONTEXT)eglDestroyContext(display_,context_);if(surface_!=EGL_NO_SURFACE)eglDestroySurface(display_,surface_);eglTerminate(display_);}
}
void Renderer::resetInput(){leftId_=rightId_=-1;moveX_=moveY_=0;last_=std::chrono::steady_clock::now();}
void Renderer::handleInput(){
 std::vector<int> pending;{std::lock_guard<std::mutex> lock(commandMutex);pending.swap(commands);}
 for(int command:pending){bool changed=false;if(command==0)building_=!building_;else if(command==1)world_.selected=(world_.selected+1)%8;else if(command==2)world_.rotation=(world_.rotation+1)%4;else if(command==3&&building_)changed=world_.place();else if(command==4)changed=world_.remove();if(changed&&!world_.save(savePath_))__android_log_print(ANDROID_LOG_ERROR,"Ekron","Failed to save sandbox");}
 auto *buffer=android_app_swap_input_buffers(app_);if(!buffer)return;
 for(uint64_t i=0;i<buffer->motionEventsCount;i++){
  const auto &event=buffer->motionEvents[i];const int action=event.action&AMOTION_EVENT_ACTION_MASK;
  const int selected=(event.action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
  if(action==AMOTION_EVENT_ACTION_CANCEL){resetInput();continue;}
  if(action==AMOTION_EVENT_ACTION_DOWN||action==AMOTION_EVENT_ACTION_POINTER_DOWN){
   if(selected<0||selected>=event.pointerCount)continue;const auto &p=event.pointers[selected];float x=GameActivityPointerAxes_getX(&p),y=GameActivityPointerAxes_getY(&p);
   if(x<width_*.42f&&leftId_<0){leftId_=p.id;originX_=x;originY_=y;}else if(rightId_<0){rightId_=p.id;rightX_=x;rightY_=y;}
  }else if(action==AMOTION_EVENT_ACTION_UP||action==AMOTION_EVENT_ACTION_POINTER_UP){
   if(selected<0||selected>=event.pointerCount)continue;int id=event.pointers[selected].id;if(id==leftId_){leftId_=-1;moveX_=moveY_=0;}if(id==rightId_)rightId_=-1;
  }else if(action==AMOTION_EVENT_ACTION_MOVE){for(uint32_t p=0;p<event.pointerCount;p++){
   const auto &pointer=event.pointers[p];float x=GameActivityPointerAxes_getX(&pointer),y=GameActivityPointerAxes_getY(&pointer);
   if(pointer.id==leftId_){float radius=std::max(40.f,height_*.14f);moveX_=(x-originX_)/radius;moveY_=(originY_-y)/radius;const float length=std::max(1.f,std::hypot(moveX_,moveY_));moveX_/=length;moveY_/=length;}
   if(pointer.id==rightId_){world_.yaw-=(x-rightX_)*.004f;world_.pitch=std::clamp(world_.pitch-(y-rightY_)*.004f,-1.3f,1.3f);rightX_=x;rightY_=y;}
  }}
 }
 android_app_clear_motion_events(buffer);android_app_clear_key_events(buffer);
}
void Renderer::render(){
 if(!program_||surface_==EGL_NO_SURFACE)return;
 eglQuerySurface(display_,surface_,EGL_WIDTH,&width_);eglQuerySurface(display_,surface_,EGL_HEIGHT,&height_);if(width_<1||height_<1)return;
 const auto now=std::chrono::steady_clock::now();float dt=std::clamp(std::chrono::duration<float>(now-last_).count(),0.f,.05f);last_=now;world_.move(moveX_,moveY_,dt);
 std::vector<Vertex> vertices;vertices.reserve((world_.scenery.size()+world_.objects.size()*10+1)*36);
 for(const auto &b:world_.scenery)cube(vertices,b);for(const auto&o:world_.objects)object(vertices,o);
 if(building_){const auto p=world_.preview();object(vertices,p,true,world_.objects.size()<sandbox::World::limit&&world_.canPlace(p));}
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
 glUniform3f(glGetUniformLocation(program_,"eye"),world_.x,1.7f,world_.z);glUniform2f(glGetUniformLocation(program_,"angles"),world_.yaw,world_.pitch);glUniform1f(glGetUniformLocation(program_,"aspect"),float(width_)/height_);
 glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
 city_.draw(program_,world_.x,world_.z);
 car_.draw(program_,world_.x,world_.z);
 eglSwapBuffers(display_,surface_);
}
