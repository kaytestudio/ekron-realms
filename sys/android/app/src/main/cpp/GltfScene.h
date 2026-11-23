#pragma once
#include <array>
#include <functional>
#include <string>
#include <vector>
#include "SandboxWorld.h"
#include "CollisionMesh.h"
#include <map>
#include <memory>

namespace city {
using ReadFile=std::function<bool(const std::string&,std::vector<unsigned char>&)>;
struct Vertex { float x,y,z,nx,ny,nz,u,v; };
struct Image { int width=0,height=0;std::vector<unsigned char> rgba; };
struct Material {
    std::array<float,4> color{1,1,1,1};
    int image=-1,wrapS=10497,wrapT=10497;
    bool doubleSided=false;
    bool playerFence=false; // Explicit city-asset fence material, never inferred from transparency alone.
    int alphaMode=0; // 0 opaque, 1 mask, 2 blend
    float cutoff=.5f;
    float roughness=.9f;
    bool worldSpaceUV=false;
};
// model: the Realms inline model (door, platform) the node belongs to, 0 = world.
struct Batch { std::vector<Vertex> vertices;int material=-1;bool decorativeWire=false;int model=0; };
struct LoadOptions {
    bool level=true;
    bool buildPhysics=true;
    // Keep the file's own coordinates (node transforms applied, no
    // recentring/scaling): used for Realms glTF maps, whose collision lives
    // in the same space (realms::MeshMap).
    bool keepSourceSpace=false;
    float targetExtent=80.f;
    // glTF node index -> Realms inline model for brush entity nodes (see
    // realms::MeshMapModel); their children inherit it. A node used twice
    // gets it the first time only, as in the collision import.
    std::map<int,int> nodeModels;
    float x=0,y=0,z=0,yaw=0;
};
struct Scene {
    std::vector<Image> images;
    std::vector<Material> materials;
    std::vector<Batch> batches;
    std::vector<sandbox::Box> colliders;
    std::vector<sandbox::WallSegment> wallSegments;
    std::shared_ptr<const physics::CollisionMesh> collision;
    std::shared_ptr<const physics::CollisionMesh> fenceBarriers;
    float spawnX=0,spawnZ=0;
    std::array<float,3> minimum{},maximum{};
    // How vertices relate to the file's own (source) space: when sourceMapped,
    // vertex = (source - sourceOffset) * sourceScale. Set for keepSourceSpace
    // (identity) and level scenes; placed (rotated) scenes leave it false.
    bool sourceMapped=false;
    float sourceScale=1;
    std::array<float,3> sourceOffset{};
    // Reads both GLB and JSON glTF from assets; external paths resolve relative to the scene.
    bool load(const std::string &path,const ReadFile &read,std::string &diagnostics,const LoadOptions &options={});
};
}
