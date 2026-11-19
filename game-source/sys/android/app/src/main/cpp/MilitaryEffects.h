#pragma once
#include "MilitaryParticles.h"
#include <GLES3/gl3.h>
namespace military {
class Effects {
 GLuint program_=0,vao_=0,vbo_=0;
 Particles particles_;
public:
 void update(float dt,float x,float z){particles_.update(dt,x,z);}
 void lights(GLuint worldProgram,float x,float z);
 void draw(float x,float y,float z,float yaw,float pitch,float roll,int width,int height);
 void release();
};
}
