#pragma once
#include <array>
#include <functional>
#include <string>
#include <vector>
#include "SandboxWorld.h"

namespace city {
using ReadFile=std::function<bool(const std::string&,std::vector<unsigned char>&)>;
struct Vertex { float x,y,z,nx,ny,nz,u,v; };
struct Image { int width=0,height=0;std::vector<unsigned char> rgba; };
struct Material {
    std::array<float,4> color{1,1,1,1};
    int image=-1,wrapS=10497,wrapT=10497;
    bool doubleSided=false;
    int alphaMode=0; // 0 opaque, 1 mask, 2 blend
    float cutoff=.5f;
};
struct Batch { std::vector<Vertex> vertices;int material=-1; };
struct Scene {
    std::vector<Image> images;
    std::vector<Material> materials;
    std::vector<Batch> batches;
    std::vector<sandbox::Box> colliders;
    float spawnX=0,spawnZ=0;
    std::array<float,3> minimum{},maximum{};
    // Reads both GLB and JSON glTF from assets; external paths resolve relative to the scene.
    bool load(const std::string &path,const ReadFile &read,std::string &diagnostics);
};
}
