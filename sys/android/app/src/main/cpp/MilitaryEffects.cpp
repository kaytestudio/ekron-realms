#include "MilitaryEffects.h"
#include <cstddef>
#include <numeric>
namespace military {
namespace {
const char* particleVertex=R"(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in float size;
layout(location=2) in vec4 color;
uniform vec3 eye;
uniform vec2 angles;
uniform float cameraRoll,aspect,pixelScale,maxPoint;
out vec4 tint;
void main(){
 vec3 p=position-eye;float cy=cos(angles.x),sy=sin(angles.x),cp=cos(angles.y),sp=sin(angles.y);
 vec3 q=vec3(cy*p.x-sy*p.z,p.y,sy*p.x+cy*p.z);q=vec3(q.x,cp*q.y+sp*q.z,-sp*q.y+cp*q.z);
 q.xy=mat2(cos(cameraRoll),sin(cameraRoll),-sin(cameraRoll),cos(cameraRoll))*q.xy;
 gl_Position=vec4(q.x*1.37638/aspect,q.y*1.37638,-1.000100*q.z-.200010,-q.z);
 gl_PointSize=q.z<-.1?clamp(size*pixelScale/(-q.z),1.,maxPoint):0.;tint=color;
})";
const char* particleFragment=R"(#version 300 es
precision highp float;
in vec4 tint;
uniform int kind;
out vec4 outColor;
void main(){vec2 p=gl_PointCoord*2.-1.;float r=length(p);if(r>1.)discard;
 float a=kind==1?pow(max(0.,1.-r),1.7):kind==2?1.-smoothstep(.3,1.,r):pow(max(0.,1.-r*r),2.);
 if(kind==0)a*=.8+.2*sin(p.x*13.+sin(p.y*11.));
 outColor=vec4(tint.rgb,tint.a*a);
})";
GLuint compile(GLenum type,const char*text){GLuint s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(s);return 0;}return s;}
}
void Effects::lights(GLuint program,float x,float z){
 std::array<float,24> data{};std::vector<size_t> order;
 for(size_t i=0;i<emitters.size();i++)if(emitters[i].fire)order.push_back(i);
 auto distance=[&](size_t i){return std::hypot(emitters[i].x-x,emitters[i].z-z);};
 std::partial_sort(order.begin(),order.begin()+std::min(size_t(6),order.size()),order.end(),[&](size_t a,size_t b){return distance(a)<distance(b);});
 for(size_t i=0;i<std::min(size_t(6),order.size());i++){const auto&e=emitters[order[i]];if(distance(order[i])>=90)continue;data[i*4]=e.x;data[i*4+1]=e.y+1.03f;data[i*4+2]=e.z;data[i*4+3]=.75f+std::sin(particles_.time()*13+i*5)*.12f+std::sin(particles_.time()*29+i)*.08f;}
 glUniform4fv(glGetUniformLocation(program,"fireLights"),6,data.data());
}
void Effects::draw(float x,float y,float z,float yaw,float pitch,float roll,int width,int height){
 if(!program_){GLuint vs=compile(GL_VERTEX_SHADER,particleVertex),fs=compile(GL_FRAGMENT_SHADER,particleFragment);if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return;}program_=glCreateProgram();glAttachShader(program_,vs);glAttachShader(program_,fs);glLinkProgram(program_);glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);if(!ok){release();return;}
 glGenVertexArrays(1,&vao_);glGenBuffers(1,&vbo_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);
 for(GLuint a:{0u,1u,2u})glEnableVertexAttribArray(a);
 glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Point),nullptr);glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,sizeof(Point),reinterpret_cast<void*>(offsetof(Point,size)));glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Point),reinterpret_cast<void*>(offsetof(Point,r)));}
 glUseProgram(program_);glUniform3f(glGetUniformLocation(program_,"eye"),x,y,z);glUniform2f(glGetUniformLocation(program_,"angles"),yaw,pitch);glUniform1f(glGetUniformLocation(program_,"cameraRoll"),roll);glUniform1f(glGetUniformLocation(program_,"aspect"),float(width)/std::max(1,height));glUniform1f(glGetUniformLocation(program_,"pixelScale"),height*.68819f);float range[2];glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE,range);glUniform1f(glGetUniformLocation(program_,"maxPoint"),std::min(700.f,range[1]));
 glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);glEnable(GL_BLEND);glDepthMask(GL_FALSE);glEnable(GL_DEPTH_TEST);
 auto draw=[&](const std::vector<Point>&points,int kind){glUniform1i(glGetUniformLocation(program_,"kind"),kind);glBlendFunc(GL_SRC_ALPHA,kind==1?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);glBufferData(GL_ARRAY_BUFFER,points.size()*sizeof(Point),points.data(),GL_STREAM_DRAW);glDrawArrays(GL_POINTS,0,GLsizei(points.size()));};
 // Sort only the transparent draw list, never the simulation pools.
 auto smoke=particles_.smoke;std::sort(smoke.begin(),smoke.end(),[&](const Point&a,const Point&b){auto d=[&](const Point&p){return (p.x-x)*(p.x-x)+(p.y-y)*(p.y-y)+(p.z-z)*(p.z-z);};return d(a)>d(b);});
 draw(smoke,0);draw(particles_.flame,1);draw(particles_.ash,2);glDepthMask(GL_TRUE);glDisable(GL_BLEND);
}
void Effects::release(){if(vbo_)glDeleteBuffers(1,&vbo_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);vbo_=vao_=program_=0;}
}
