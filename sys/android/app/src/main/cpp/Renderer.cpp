#include "Renderer.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>
#include <android/log.h>
#include <mutex>
#include <vector>
#include <optional>

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
uniform vec3 eye;
uniform vec2 angles;
uniform float aspect;
out vec3 tint;
out float distanceToEye;
void main(){
 vec3 p=position-eye;
 float cy=cos(angles.x),sy=sin(angles.x),cp=cos(angles.y),sp=sin(angles.y);
 vec3 q=vec3(cy*p.x-sy*p.z,p.y,sy*p.x+cy*p.z);
 q=vec3(q.x,cp*q.y+sp*q.z,-sp*q.y+cp*q.z);
 float f=1.37638;
 gl_Position=vec4(q.x*f/aspect,q.y*f,-1.001334*q.z-.200134,-q.z);
 tint=color;distanceToEye=length(p);
})";
const char *fragment=R"(#version 300 es
precision mediump float;
in vec3 tint;
in float distanceToEye;
out vec4 outColor;
void main(){float fog=clamp((distanceToEye-35.0)/100.0,0.0,1.0);outColor=vec4(mix(tint,vec3(.56,.61,.63),fog),1.0);})";
GLuint compile(GLenum type,const char*text){GLuint s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[1024];glGetShaderInfoLog(s,sizeof(log),nullptr,log);__android_log_print(ANDROID_LOG_ERROR,"Ekron","Shader: %s",log);glDeleteShader(s);return 0;}return s;}
struct Vertex{float x,y,z,r,g,b;};
void cube(std::vector<Vertex>&v,const sandbox::Box&b){
 static const int faces[6][6]={{0,1,2,0,2,3},{5,4,7,5,7,6},{4,0,3,4,3,7},{1,5,6,1,6,2},{3,2,6,3,6,7},{4,5,1,4,1,0}};
 static const float corners[8][3]={{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}};
 static const float light[6]={.84f,.65f,.72f,.92f,1.f,.55f};
 for(int face=0;face<6;face++)for(int index:faces[face]){auto c=corners[index];float k=light[face];v.push_back({b.x+c[0]*b.w/2,b.y+c[1]*b.h/2,b.z+c[2]*b.d/2,b.r*k,b.g*k,b.b*k});}
}
void object(std::vector<Vertex>&v,const sandbox::Object&o,bool preview=false,bool valid=false){
 auto body=sandbox::World::bounds(o);
 if(preview){body.r=valid?.25f:.9f;body.g=valid?.8f:.25f;body.b=.4f;cube(v,body);return;}
 // Local-space furniture parts rotated by 90 degree increments.
 auto part=[&](float x,float y,float z,float w,float h,float d,float r,float g,float b){
  for(int i=0;i<o.rotation;i++){float old=x;x=z;z=-old;std::swap(w,d);}
  cube(v,{o.x+x,y,o.z+z,w,h,d,r,g,b});
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
 const auto vs=compile(GL_VERTEX_SHADER,vertex),fs=compile(GL_FRAGMENT_SHADER,fragment);if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return;}
 program_=glCreateProgram();glAttachShader(program_,vs);glAttachShader(program_,fs);glLinkProgram(program_);glDeleteShader(vs);glDeleteShader(fs);
 GLint linked=0;glGetProgramiv(program_,GL_LINK_STATUS,&linked);if(!linked){glDeleteProgram(program_);program_=0;return;}
 glGenVertexArrays(1,&vao_);glGenBuffers(1,&buffer_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);
 glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
 glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(3*sizeof(float)));
 glEnable(GL_DEPTH_TEST);eglSwapInterval(display_,1);
}
Renderer::~Renderer(){
 retainedWorld=world_;retainedBuilding=building_;
 if(display_!=EGL_NO_DISPLAY){if(context_!=EGL_NO_CONTEXT&&surface_!=EGL_NO_SURFACE&&eglMakeCurrent(display_,surface_,surface_,context_)){if(buffer_)glDeleteBuffers(1,&buffer_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);}eglMakeCurrent(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context_!=EGL_NO_CONTEXT)eglDestroyContext(display_,context_);if(surface_!=EGL_NO_SURFACE)eglDestroySurface(display_,surface_);eglTerminate(display_);}
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
 glViewport(0,0,width_,height_);glClearColor(.56f,.61f,.63f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glUseProgram(program_);
 glUniform3f(glGetUniformLocation(program_,"eye"),world_.x,1.7f,world_.z);glUniform2f(glGetUniformLocation(program_,"angles"),world_.yaw,world_.pitch);glUniform1f(glGetUniformLocation(program_,"aspect"),float(width_)/height_);
 glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
 eglSwapBuffers(display_,surface_);
}
