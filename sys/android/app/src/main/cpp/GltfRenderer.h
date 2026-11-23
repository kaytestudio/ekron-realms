#pragma once
#include "GltfScene.h"
#include <GLES3/gl3.h>
#include <array>
#include <functional>
struct AAssetManager;
namespace city {
class GltfRenderer {
public:
    static bool prepare(AAssetManager *assets,const std::string &path,Scene &scene,std::string &log,const LoadOptions &options={});
    void beginUpload(Scene scene);
    bool uploadStep(std::string &log);
    bool uploading()const{return uploading_;}
    float uploadProgress()const{size_t total=scene_.materials.size()+scene_.batches.size();return total?float(nextMaterial_+nextBatch_)/total:1.f;}
    bool load(AAssetManager *assets,const std::string &path,std::string &log,const LoadOptions &options={});
    // modelTransform (glTF maps): for batches of inline model n > 0, the
    // objectTransform to draw them with; false skips them (hidden). The
    // caller's objectTransform is restored to identity afterwards.
    using ModelTransform=std::function<bool(int model,std::array<float,16>&out)>;
    void draw(GLuint program,float eyeX,float eyeZ,bool hologram=false,const ModelTransform *modelTransform=nullptr)const;
    void release(); // Call while owning context is current.
    bool ready()const{return !uploading_&&!meshes_.empty();}
    const Scene &scene()const{return scene_;}
private:
    struct Mesh {GLuint vao=0,vbo=0;GLsizei count=0;int material=-1;int model=0;float x=0,z=0;};
    Scene scene_;
    bool uploading_=false;
    size_t nextMaterial_=0,nextBatch_=0;
    std::vector<Mesh> meshes_;
    std::vector<GLuint> textures_;
};
}
