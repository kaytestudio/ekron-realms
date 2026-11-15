#pragma once
#include "GltfScene.h"
#include <GLES3/gl3.h>
struct AAssetManager;
namespace city {
class GltfRenderer {
public:
    bool load(AAssetManager *assets,const std::string &path,std::string &log);
    void draw(GLuint program,float eyeX,float eyeZ);
    void release(); // Call while owning context is current.
    bool ready()const{return !meshes_.empty();}
    const Scene &scene()const{return scene_;}
private:
    struct Mesh {GLuint vao=0,vbo=0;GLsizei count=0;int material=-1;float x=0,z=0;};
    Scene scene_;
    std::vector<Mesh> meshes_;
    std::vector<GLuint> textures_;
};
}
