//
//  SandboxBridge.mm
//  realms iOS
//

#import "SandboxBridge.h"
#import <AVFoundation/AVFoundation.h>
#import "../realms Shared/ShaderTypes.h"

#include "SandboxWorld.h"
#include "ToolInteraction.h"
#include "FpvDrone.h"
#include "ObjectDynamics.h"
#include "WorldRecovery.h"
#include "Interaction.h"
#include "RaycastPlacement.h"
#include "PlayerPhysics.h"
#include "MeshGeometry.h"
#include "ObjectMesh.h"
#include "CityGraffiti.h"
#include "GravityBeam.h"
#include "OnlinePresence.h"
#include "LevelPlacement.h"
#include "DebugPlayer.h"
#include "GltfScene.h"
#include "MapSession.h"
#include "Quake2View.h"
#include "GameSoundPlayer.h"
#include "SoundSnapshot.h"
#include "SoundSystem.h"
#include "realms/Engine.h"
#include "realms/GameServer.h"
#include "realms/GameView.h"
#include "realms/SandboxBoot.h"

#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#include "third_party/stb_image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

static_assert(sizeof(sandbox::MeshVertex) == 48, "MeshVertex layout must match PackedSandboxVertex in Shaders.metal");
static_assert(sizeof(city::Vertex) == 32, "city::Vertex layout must match PackedModelVertex in Shaders.metal");

namespace {

constexpr int kTextureSize = 512;
constexpr int kTextureLayers = 8;
constexpr int kMaxBuffersInFlight = 3;
constexpr int kMonitorWidth = 480;
constexpr int kMonitorHeight = 270;

using PreparedTextures = std::array<std::vector<unsigned char>, kTextureLayers>;

bool readFileFromRoot(const std::string &root, const std::string &relativePath, std::vector<unsigned char> &bytes) {
    if (relativePath.empty() || relativePath[0] == '/' ||
        relativePath.find("..") != std::string::npos ||
        relativePath.find(':') != std::string::npos) {
        return false;
    }
    const std::string fullPath = root + "/" + relativePath;
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const auto endPos = file.tellg();
    if (endPos <= 0 || endPos > static_cast<std::streamoff>(128 * 1024 * 1024)) {
        return false;
    }
    bytes.resize(static_cast<size_t>(endPos));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), endPos)) {
        bytes.clear();
        return false;
    }
    return true;
}

std::vector<unsigned char> fallbackTextureLayer(int layer) {
    constexpr unsigned char colors[8][3] = {
        {48, 50, 53}, {137, 135, 129}, {136, 72, 47}, {124, 68, 35},
        {255, 255, 255}, {135, 91, 49}, {43, 56, 48}, {142, 177, 182}
    };
    std::vector<unsigned char> out(size_t(kTextureSize) * kTextureSize * 4);
    for (int y = 0; y < kTextureSize; ++y) {
        for (int x = 0; x < kTextureSize; ++x) {
            const int shade = layer == 4 ? 0 : ((x / 32 + y / 32) % 2) * 7;
            const size_t p = (size_t(y) * kTextureSize + size_t(x)) * 4;
            for (int c = 0; c < 3; ++c) {
                out[p + size_t(c)] = static_cast<unsigned char>(colors[layer][c] + shade);
            }
            out[p + 3] = 255;
        }
    }
    return out;
}

std::vector<unsigned char> decodeTextureLayer(const std::string &bundleRoot, const char *path, int layer) {
    std::vector<unsigned char> bytes;
    if (!readFileFromRoot(bundleRoot, path, bytes) || bytes.empty() || bytes.size() > 32 * 1024 * 1024) {
        return fallbackTextureLayer(layer);
    }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels) ||
        w < 1 || h < 1 || w > 4096 || h > 4096) {
        return fallbackTextureLayer(layer);
    }
    unsigned char *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) {
        return fallbackTextureLayer(layer);
    }
    std::vector<unsigned char> out(size_t(kTextureSize) * kTextureSize * 4);
    for (int y = 0; y < kTextureSize; ++y) {
        for (int x = 0; x < kTextureSize; ++x) {
            const int sx = x * w / kTextureSize;
            const int sy = y * h / kTextureSize;
            std::copy_n(pixels + (size_t(sy) * size_t(w) + size_t(sx)) * 4,
                        4,
                        out.data() + (size_t(y) * kTextureSize + size_t(x)) * 4);
        }
    }
    stbi_image_free(pixels);
    return out;
}

PreparedTextures prepareAllTextures(const std::string &bundleRoot) {
    PreparedTextures images;
    const std::array<const char *, 4> names = {
        "textures/asphalt.png",
        "textures/concrete.png",
        "textures/brick.png",
        "textures/rust.png"
    };
    for (int layer = 0; layer < kTextureLayers; ++layer) {
        if (layer < 4) {
            images[size_t(layer)] = decodeTextureLayer(bundleRoot, names[size_t(layer)], layer);
        } else if (layer == 5) {
            images[size_t(layer)] = decodeTextureLayer(bundleRoot, "textures/wood.png", layer);
        } else if (layer == 6) {
            images[size_t(layer)] = decodeTextureLayer(bundleRoot, "textures/fabric.png", layer);
        } else if (layer == 7) {
            images[size_t(layer)] = decodeTextureLayer(bundleRoot, "textures/glass.png", layer);
        } else {
            images[size_t(layer)] = fallbackTextureLayer(layer);
        }
    }
    return images;
}

city::SpawnConfig readSpawnConfig(const std::string &supportDir, const std::string &bundleRoot) {
    city::SpawnConfig config;
    std::string text;
    std::string error;
    std::ifstream customFile(supportDir + "/spawn.json", std::ios::binary | std::ios::ate);
    if (customFile && customFile.tellg() > 0 && customFile.tellg() <= 65536) {
        customFile.seekg(0);
        text.assign(std::istreambuf_iterator<char>(customFile), {});
    } else {
        std::vector<unsigned char> assetBytes;
        if (readFileFromRoot(bundleRoot, "config/spawn.json", assetBytes) && assetBytes.size() <= 65536) {
            text.assign(reinterpret_cast<const char *>(assetBytes.data()), assetBytes.size());
        }
    }
    if (!text.empty()) {
        city::parseSpawnConfig(text, config, error);
    }
    return config;
}

int wrapIndexForMode(int gltfWrap) {
    if (gltfWrap == 33071) return 1; // CLAMP_TO_EDGE
    if (gltfWrap == 33648) return 2; // MIRRORED_REPEAT
    return 0;                        // REPEAT (10497)
}

NSUInteger mipCountForDimensions(NSUInteger width, NSUInteger height) {
    NSUInteger maxDim = std::max(width, height);
    NSUInteger levels = 1;
    while (maxDim > 1) {
        maxDim >>= 1;
        ++levels;
    }
    return levels;
}

matrix_float4x4 makeColumnMatrix(const float m[16]) {
    return matrix_float4x4{{
        simd_make_float4(m[0],  m[1],  m[2],  m[3]),
        simd_make_float4(m[4],  m[5],  m[6],  m[7]),
        simd_make_float4(m[8],  m[9],  m[10], m[11]),
        simd_make_float4(m[12], m[13], m[14], m[15])
    }};
}

struct UploadedMeshBatch {
    id<MTLBuffer> vbo = nil;
    NSUInteger count = 0;
    int material = -1;
    float x = 0.0f;
    float z = 0.0f;
};

class MetalGltfModel {
public:
    static bool prepare(const std::string &bundleRoot,
                        const std::string &path,
                        city::Scene &outScene,
                        std::string &log,
                        const city::LoadOptions &options = {}) {
        return outScene.load(path, [&](const std::string &p, std::vector<unsigned char> &b) {
            return readFileFromRoot(bundleRoot, p, b);
        }, log, options);
    }

    void beginUpload(city::Scene nextScene) {
        release();
        scene_ = std::move(nextScene);
        nextMaterial_ = 0;
        nextBatch_ = 0;
        uploading_ = true;
    }

    bool uploading() const { return uploading_; }

    float uploadProgress() const {
        const size_t total = scene_.materials.size() + scene_.batches.size();
        return total ? float(nextMaterial_ + nextBatch_) / float(total) : 1.0f;
    }

    bool ready() const { return !uploading_ && !meshes_.empty(); }

    const city::Scene &scene() const { return scene_; }

    bool uploadStep(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLTexture> whiteTexture) {
        if (!uploading_) return ready();
        if (nextMaterial_ < scene_.materials.size()) {
            const auto &mat = scene_.materials[nextMaterial_++];
            const city::Image *img = nullptr;
            if (mat.image >= 0 && size_t(mat.image) < scene_.images.size()) {
                img = &scene_.images[size_t(mat.image)];
            }
            const bool valid = img && img->width > 0 && img->height > 0 &&
                               img->rgba.size() == size_t(img->width) * size_t(img->height) * 4;
            if (!valid) {
                textures_.push_back(whiteTexture);
            } else {
                const NSUInteger w = static_cast<NSUInteger>(img->width);
                const NSUInteger h = static_cast<NSUInteger>(img->height);
                const NSUInteger mips = mipCountForDimensions(w, h);
                MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                                                                width:w
                                                                                               height:h
                                                                                            mipmapped:(mips > 1)];
                desc.usage = MTLTextureUsageShaderRead;
                id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
                if (tex) {
                    [tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
                           mipmapLevel:0
                             withBytes:img->rgba.data()
                           bytesPerRow:w * 4];
                    if (mips > 1) {
                        id<MTLCommandBuffer> cmd = [queue commandBuffer];
                        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
                        [blit generateMipmapsForTexture:tex];
                        [blit endEncoding];
                        [cmd commit];
                    }
                    textures_.push_back(tex);
                } else {
                    textures_.push_back(whiteTexture);
                }
            }
        } else if (nextBatch_ < scene_.batches.size()) {
            const auto &batch = scene_.batches[nextBatch_++];
            if (!batch.vertices.empty()) {
                UploadedMeshBatch mesh;
                mesh.count = static_cast<NSUInteger>(batch.vertices.size());
                mesh.material = batch.material;
                const float invCount = 1.0f / float(batch.vertices.size());
                for (const auto &v : batch.vertices) {
                    mesh.x += v.x * invCount;
                    mesh.z += v.z * invCount;
                }
                mesh.vbo = [device newBufferWithBytes:batch.vertices.data()
                                               length:batch.vertices.size() * sizeof(city::Vertex)
                                              options:MTLResourceStorageModeShared];
                if (mesh.vbo) {
                    meshes_.push_back(mesh);
                }
            }
        }

        if (nextMaterial_ < scene_.materials.size() || nextBatch_ < scene_.batches.size()) {
            return false;
        }
        uploading_ = false;
        for (auto &img : scene_.images) {
            std::vector<unsigned char>().swap(img.rgba);
        }
        for (auto &b : scene_.batches) {
            std::vector<city::Vertex>().swap(b.vertices);
        }
        return ready();
    }

    void draw(id<MTLRenderCommandEncoder> encoder,
              id<MTLRenderPipelineState> opaquePipeline,
              id<MTLRenderPipelineState> blendedPipeline,
              id<MTLDepthStencilState> depthWriteState,
              id<MTLDepthStencilState> depthReadState,
              id<MTLSamplerState> const samplers[3][3],
              id<MTLTexture> whiteTexture,
              const matrix_float4x4 &objectTransform,
              float eyeX,
              float eyeZ,
              bool hologram) const {
        if (!ready()) return;

        std::vector<size_t> order(meshes_.size());
        std::iota(order.begin(), order.end(), 0);
        auto isBlended = [&](const UploadedMeshBatch &m) {
            return m.material >= 0 && size_t(m.material) < scene_.materials.size() &&
                   scene_.materials[size_t(m.material)].alphaMode == 2;
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const auto &ma = meshes_[a];
            const auto &mb = meshes_[b];
            const bool ba = isBlended(ma);
            const bool bb = isBlended(mb);
            if (ba != bb) return !ba;
            return ba && std::hypot(ma.x - eyeX, ma.z - eyeZ) > std::hypot(mb.x - eyeX, mb.z - eyeZ);
        });

        for (size_t idx : order) {
            const auto &mesh = meshes_[idx];
            if (!mesh.vbo || mesh.count == 0) continue;
            city::Material fallback;
            const auto &mat = (mesh.material >= 0 && size_t(mesh.material) < scene_.materials.size())
                ? scene_.materials[size_t(mesh.material)]
                : fallback;

            const bool useBlend = hologram || mat.alphaMode == 2;
            [encoder setRenderPipelineState:(useBlend ? blendedPipeline : opaquePipeline)];
            [encoder setDepthStencilState:((useBlend && !hologram) ? depthReadState : depthWriteState)];

            const bool hasTex = mat.image >= 0 && mesh.material >= 0 && size_t(mesh.material) < textures_.size();
            id<MTLTexture> tex = hasTex ? textures_[size_t(mesh.material)] : whiteTexture;
            [encoder setFragmentTexture:tex atIndex:1];

            const int ws = wrapIndexForMode(mat.wrapS);
            const int wt = wrapIndexForMode(mat.wrapT);
            [encoder setFragmentSamplerState:samplers[ws][wt] atIndex:0];

            SandboxDrawUniforms draw{};
            draw.objectTransform = objectTransform;
            draw.baseColorFactor = simd_make_float4(mat.color[0], mat.color[1], mat.color[2], mat.color[3]);
            draw.materialParams = simd_make_float4(mat.roughness, mat.cutoff, 0.0f, 0.0f);
            draw.drawFlags = simd_make_int4(1, hologram ? 1 : 0, hasTex ? 1 : 0, mat.alphaMode);

            [encoder setVertexBuffer:mesh.vbo offset:0 atIndex:0];
            [encoder setVertexBytes:&draw length:sizeof(draw) atIndex:2];
            [encoder setFragmentBytes:&draw length:sizeof(draw) atIndex:2];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:mesh.count];
        }
    }

    void release() {
        meshes_.clear();
        textures_.clear();
        scene_ = city::Scene{};
        uploading_ = false;
        nextMaterial_ = 0;
        nextBatch_ = 0;
    }

private:
    city::Scene scene_;
    bool uploading_ = false;
    size_t nextMaterial_ = 0;
    size_t nextBatch_ = 0;
    std::vector<UploadedMeshBatch> meshes_;
    std::vector<id<MTLTexture>> textures_;
};

struct PreparedAssets {
    city::Scene city;
    city::Scene car;
    city::Scene remote;
    city::Scene drone;
    PreparedTextures textures;
    std::string log;
    float parkingX = 0.0f;
    float parkingZ = 0.0f;
    float parkingY = 0.0f;
    bool valid = false;
    bool carValid = false;
    bool remoteValid = false;
    bool droneValid = false;
};

// ---- Realms map mode ---------------------------------------------------
//
// Metal version of AndroidMapMode, on top of mapview::Quake2View. Device
// builds link the game module statically; the simulator only walks maps.

constexpr const char *kCityAsset = "models/abandoned_city_street_for_games.glb";

// Map changes reported by the engine (boot hook, any thread that runs the
// command buffer); the bridge may be recreated, the engine is not.
std::mutex gMapChangeMutex;
bool gMapChanged = false;
std::string gMapChange;

void noteMapChange(const std::string &path) {
    std::lock_guard<std::mutex> lock(gMapChangeMutex);
    gMapChanged = true;
    gMapChange = path;
}

bool takeMapChange(std::string &path) {
    std::lock_guard<std::mutex> lock(gMapChangeMutex);
    if (!gMapChanged) return false;
    gMapChanged = false;
    path = gMapChange;
    return true;
}

// Walker state kept when the bridge is recreated while a map is loaded; a
// running Quake 2 game is kept whole and carries on.
struct RetainedMap {
    std::string path;
    realms::MapWalker walker;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool crouch = false;
    std::unique_ptr<mapview::Quake2View> game;
};
std::optional<RetainedMap> gRetainedMap;

static_assert(sizeof(mapview::EntityVertex) == 32, "EntityVertex layout must match PackedModelVertex in Shaders.metal");
static_assert(sizeof(mapview::EffectVertex) == 40, "EffectVertex layout must match PackedEffectVertex in Shaders.metal");

// Pipelines and states the map mode draws with (built by the bridge).
struct MetalMapPipelines {
    id<MTLRenderPipelineState> sandbox = nil;      // sandboxVertex / sandboxFragment
    id<MTLRenderPipelineState> modelOpaque = nil;  // modelVertex / sandboxFragment
    id<MTLRenderPipelineState> modelBlended = nil;
    id<MTLRenderPipelineState> quakeMap = nil;     // quakeMapVertex / quakeMapFragment
    id<MTLRenderPipelineState> quakeEntityOpaque = nil; // modelVertex / quakeMapFragment
    id<MTLRenderPipelineState> quakeEntityBlended = nil;
    id<MTLRenderPipelineState> quakeEffect = nil;  // quakeEffectVertex / quakeEffectFragment
    id<MTLDepthStencilState> depthWrite = nil;
    id<MTLDepthStencilState> depthRead = nil;
    id<MTLTexture> white = nil;
};

class MetalMapMode {
public:
    MetalMapMode() : view_(std::make_unique<mapview::Quake2View>()) {}
    MetalMapMode(const MetalMapMode &) = delete;
    MetalMapMode &operator=(const MetalMapMode &) = delete;
    ~MetalMapMode() {
        if (prepare_.valid()) prepare_.wait();
    }

    // Opens the engine's loaded map: mapview::Quake2View (the walker, or the
    // Quake 2 game when the map has one and the game module is linked).
    // `retained` is a game kept from an earlier bridge; it carries on when it
    // runs the same map. `roots` are the engine's search roots (writable
    // support directory first, then the bundle), read directly by the
    // worker - never through the engine, which belongs to the render thread.
    bool open(realms::Engine &engine,
              id<MTLDevice> device,
              const MetalGltfModel &sandboxCity,
              const std::string &sandboxCityPath,
              const std::vector<std::string> &roots,
              std::string &error,
              std::unique_ptr<mapview::Quake2View> retained = nullptr) {
        close();
        engine.registerCvar("sv_game", "1", REALMS_CVAR_ARCHIVE);
        engine.registerCvar("cl_gun", "1", REALMS_CVAR_ARCHIVE);
        engine.registerCvar("r_lightmaps", "1", REALMS_CVAR_ARCHIVE);
        realms::MapGeometry geometry;
        fresh_ = true;
        if (retained && retained->active() && retained->gameRunning() &&
            retained->session().path() == engine.mapPath()) {
            std::string geometryError;
            if (realms::loadMapGeometry(engine, geometry, geometryError)) {
                view_ = std::move(retained); // the game carries on
                fresh_ = false;
            } else {
                geometry = {};
            }
        }
        if (retained) retained->close();
        if (fresh_) {
            // Device builds link the game (GameServer::setLinkedGame); without
            // it the map is walked.
            mapview::Quake2ViewOptions options;
            options.startGame = engine.cvarBool("sv_game", true);
            if (!view_->open(engine, options, geometry, error)) return false;
        }

        const city::Scene &cityScene = sandboxCity.scene();
        if (session().mesh() && session().path() == sandboxCityPath && sandboxCity.ready() &&
            cityScene.sourceMapped && cityScene.sourceScale > 0.0f) {
            reuseCity_ = true;
            cityScale_ = cityScene.sourceScale;
            cityOffset_ = cityScene.sourceOffset;
            view_->disableLightmaps();
            if (fresh_) session().summary() += ", textured";
            return true;
        }

        std::vector<sandbox::MeshVertex> vertices;
        std::vector<float> lightUvs;
        view_->triangles(geometry, vertices, ranges_, lightUvs);
        if (!vertices.empty()) {
            triangles_ = [device newBufferWithBytes:vertices.data()
                                             length:vertices.size() * sizeof(sandbox::MeshVertex)
                                            options:MTLResourceStorageModeShared];
            count_ = triangles_ ? static_cast<NSUInteger>(vertices.size()) : 0;
            // BSP lightmaps: atlas coordinates per vertex and the atlas.
            // quakeMapVertex always reads buffer 3, so maps without lightmaps
            // get -1 (none) for every vertex.
            const realms::Quake2Module &q2 = view_->module();
            const bool lightmapped = !session().mesh() && q2.lightmapped() && lightUvs.size() == vertices.size() * 2;
            if (!lightmapped) {
                view_->disableLightmaps();
                lightUvs.assign(vertices.size() * 2, -1.0f);
            }
            lightUvs_ = [device newBufferWithBytes:lightUvs.data()
                                            length:lightUvs.size() * sizeof(float)
                                           options:MTLResourceStorageModeShared];
            if (lightmapped) {
                MTLTextureDescriptor *desc =
                    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                       width:NSUInteger(q2.lighting().width())
                                                                      height:NSUInteger(q2.lighting().height())
                                                                   mipmapped:NO];
                desc.usage = MTLTextureUsageShaderRead;
                lightmap_ = [device newTextureWithDescriptor:desc];
                uploadLightmap();
                if (lightmap_ && fresh_) {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), ", lightmaps (%zu faces, %dx%d)", q2.lighting().litFaces(),
                                  q2.lighting().width(), q2.lighting().height());
                    session().summary() += buf;
                }
            }
            if (!triangles_ || !lightUvs_) {
                triangles_ = nil;
                lightUvs_ = nil;
                count_ = 0;
            }
        }

        if (session().mesh()) {
            const std::string path = session().path();
            prepare_ = std::async(std::launch::async, [path, roots]() -> std::optional<city::Scene> {
                try {
                    city::Scene scene;
                    city::LoadOptions options;
                    options.level = false;
                    options.buildPhysics = false;
                    options.keepSourceSpace = true;
                    std::string log;
                    auto reader = [&roots](const std::string &p, std::vector<unsigned char> &bytes) {
                        for (const std::string &root : roots) {
                            if (readFileFromRoot(root, p, bytes)) return true;
                        }
                        return false;
                    };
                    if (scene.load(path, reader, log, options)) return scene;
                    NSLog(@"[Realms] map: %s: no textured model (%s)", path.c_str(), log.c_str());
                } catch (const std::exception &e) {
                    NSLog(@"[Realms] map: %s: %s", path.c_str(), e.what());
                }
                return std::nullopt;
            });
        }
        return true;
    }

    void close() {
        releaseGpu();
        view_->close();
    }

    // Hands the view (and a running game) over, e.g. to gRetainedMap, and
    // leaves this map mode closed.
    std::unique_ptr<mapview::Quake2View> releaseView() {
        releaseGpu();
        std::unique_ptr<mapview::Quake2View> out = std::move(view_);
        view_ = std::make_unique<mapview::Quake2View>();
        return out;
    }

    bool active() const { return view_->active(); }
    bool gameRunning() const { return view_->gameRunning(); }
    // The textured model is still being prepared / uploaded.
    bool texturedPending() const { return prepare_.valid() || model_.uploading(); }

    // Continues preparing / uploading the textured model (~4 ms per frame).
    void advance(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLTexture> whiteTexture) {
        if (prepare_.valid() && prepare_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::optional<city::Scene> scene;
            try {
                scene = prepare_.get();
            } catch (const std::exception &) {
            }
            prepare_ = {};
            if (scene) model_.beginUpload(std::move(*scene));
        }
        if (!model_.uploading()) return;
        const auto start = std::chrono::steady_clock::now();
        while (model_.uploading() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(4)) {
            model_.uploadStep(device, queue, whiteTexture);
        }
        if (model_.ready()) {
            // the textured model replaces the plain triangles
            triangles_ = nil;
            lightUvs_ = nil;
            lightmap_ = nil;
            count_ = 0;
            ranges_.clear();
            view_->disableLightmaps();
            session().summary() += ", textured";
        }
    }

    // One frame: the game with this input (attack fires), or the walker.
    void update(realms::Engine &engine, const realms::WalkInput &input, float renderYaw, float renderPitch,
                float dt, bool attack) {
        view_->update(engine, input, renderYaw, renderPitch, dt, attack);
    }
    bool respawn(realms::Engine &engine) { return view_->respawn(engine); }
    void toggleNoclip() { view_->toggleNoclip(); }
    float spawnRenderYaw() const { return view_->spawnRenderYaw(); }
    realms::Quake2Module::PlayerStats stats() const { return view_->module().stats(); }
    std::vector<std::string> takeMessages() { return view_->takeMessages(); }

    // The camera in render space (the game's player, or the walker).
    physics::Vec3 eye() const { return view_->eye(); }
    // Camera eye in the space draw() renders in.
    physics::Vec3 drawEye() const {
        const physics::Vec3 e = view_->eye();
        return reuseCity_ ? mapview::toLevelScene(e, cityScale_, cityOffset_) : e;
    }

    // Draws the map, then the game's entities and effects; the scene
    // uniforms, sky and material array are already set on `encoder` (see
    // encodeSceneSetup).
    void draw(id<MTLRenderCommandEncoder> encoder,
              const MetalMapPipelines &p,
              id<MTLSamplerState> const samplers[3][3],
              const MetalGltfModel &sandboxCity,
              realms::Engine &engine,
              id<MTLDevice> device,
              id<MTLCommandQueue> queue,
              float renderYaw,
              float renderPitch,
              CGSize drawableSize) {
        const physics::Vec3 eye = drawEye();
        const matrix_float4x4 identity = matrix_identity_float4x4;
        if (reuseCity_) {
            sandboxCity.draw(encoder, p.modelOpaque, p.modelBlended, p.depthWrite, p.depthRead, samplers, p.white,
                             identity, eye.x, eye.z, false);
            return;
        }
        // effects first: their dynamic lights light the world and the entities
        view_->effects(renderYaw, renderPitch, 8, effectVertices_, lights_);
        SandboxQuakeUniforms quake{};
        const int lightCount = int(std::min<size_t>(lights_.size(), 8));
        for (int i = 0; i < lightCount; ++i) {
            const mapview::DynamicLight &l = lights_[size_t(i)];
            quake.lights[i] = simd_make_float4(l.x, l.y, l.z, l.radius);
            quake.lightColors[i] = simd_make_float4(l.r, l.g, l.b, 1.0f);
        }
        quake.modeCount = simd_make_int4(0, lightCount, 0, 0);

        if (model_.ready()) {
            model_.draw(encoder, p.modelOpaque, p.modelBlended, p.depthWrite, p.depthRead, samplers, p.white,
                        identity, eye.x, eye.z, false);
        } else if (triangles_ && lightUvs_ && count_ > 0) {
            if (lightmap_ && view_->module().updateLightStyles()) uploadLightmap(); // animated light styles
            quake.modeCount.x = (lightmap_ && engine.cvarBool("r_lightmaps", true)) ? 1 : 0;
            SandboxDrawUniforms draw{};
            draw.objectTransform = identity;
            draw.baseColorFactor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
            draw.materialParams = simd_make_float4(0.94f, 0.5f, 0.0f, 0.0f);
            draw.drawFlags = simd_make_int4(0, 0, 0, 0);
            [encoder setRenderPipelineState:p.quakeMap];
            [encoder setDepthStencilState:p.depthWrite];
            [encoder setVertexBuffer:triangles_ offset:0 atIndex:0];
            [encoder setVertexBuffer:lightUvs_ offset:0 atIndex:3];
            [encoder setFragmentTexture:(lightmap_ ? lightmap_ : p.white) atIndex:2];
            [encoder setFragmentBytes:&quake length:sizeof(quake) atIndex:3];
            if (ranges_.empty()) {
                [encoder setVertexBytes:&draw length:sizeof(draw) atIndex:2];
                [encoder setFragmentBytes:&draw length:sizeof(draw) atIndex:2];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count_];
            } else {
                // brush models where the game has them (doors, platforms), hidden when gone
                const std::vector<mapview::ModelPose> &poses = view_->modelPoses();
                for (const mapview::ModelRange &r : ranges_) {
                    const mapview::ModelPose *pose =
                        r.model > 0 && size_t(r.model) < poses.size() ? &poses[size_t(r.model)] : nullptr;
                    if (pose && !pose->visible) continue;
                    if (r.count <= 0 || NSUInteger(r.first) + NSUInteger(r.count) > count_) continue;
                    draw.objectTransform = pose ? makeColumnMatrix(pose->transform.data()) : identity;
                    [encoder setVertexBytes:&draw length:sizeof(draw) atIndex:2];
                    [encoder setFragmentBytes:&draw length:sizeof(draw) atIndex:2];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                vertexStart:NSUInteger(r.first)
                                vertexCount:NSUInteger(r.count)];
                }
            }
        }
        if (!view_->gameRunning()) return;
        view_->entities(engine, renderYaw, renderPitch, engine.cvarBool("cl_gun", true), entityVertices_,
                        entityBatches_);
        drawEntities(encoder, p, samplers, engine, device, queue, quake, drawableSize);
        drawEffects(encoder, p, device);
    }

    // Quake 2 sounds on the sandbox mixer; silent while paused.
    void playSounds(realms::Engine &engine, ekron::audio::Mixer &mixer, double dt, bool paused) {
        mixer_ = &mixer;
        realms::Quake2Module &q2 = view_->module();
        std::vector<realms::GameEvent> sounds = q2.takeSounds();
        if (paused || !q2.gameRunning()) {
            sound_.stop(mixer);
            return;
        }
        sound_.update(engine, mixer, sounds, q2.loopSounds(), q2.listener(), dt);
    }

    mapview::MapSession &session() { return view_->session(); }
    const mapview::MapSession &session() const { return view_->session(); }

private:
    void releaseGpu() {
        if (prepare_.valid()) prepare_.wait();
        prepare_ = {};
        model_.release();
        triangles_ = nil;
        lightUvs_ = nil;
        lightmap_ = nil;
        count_ = 0;
        ranges_.clear();
        skins_.clear();
        if (mixer_) sound_.stop(*mixer_);
        reuseCity_ = false;
        cityScale_ = 1.0f;
        cityOffset_ = {};
    }

    void uploadLightmap() {
        const realms::Quake2Module &q2 = view_->module();
        const int w = q2.lighting().width(), h = q2.lighting().height();
        if (!lightmap_ || w <= 0 || h <= 0 || q2.lightmap().size() < size_t(w) * size_t(h) * 4) return;
        [lightmap_ replaceRegion:MTLRegionMake2D(0, 0, NSUInteger(w), NSUInteger(h))
                     mipmapLevel:0
                       withBytes:q2.lightmap().data()
                     bytesPerRow:NSUInteger(w) * 4];
    }

    // A skin from the game's files (.pcx / .wal through the VFS), uploaded
    // once; nil when it is missing.
    id<MTLTexture> skin(realms::Engine &engine, id<MTLDevice> device, id<MTLCommandQueue> queue,
                        const std::string &name) {
        if (name.empty()) return nil;
        const auto it = skins_.find(name);
        if (it != skins_.end()) return it->second;
        id<MTLTexture> tex = nil;
        realms::RgbaImage img;
        std::string error;
        if (realms::GameModelCache::loadSkin(engine, name, img, error) && img.width > 0 && img.height > 0 &&
            img.rgba.size() == size_t(img.width) * size_t(img.height) * 4) {
            const NSUInteger w = NSUInteger(img.width), h = NSUInteger(img.height);
            const NSUInteger mips = mipCountForDimensions(w, h);
            MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                                                            width:w
                                                                                           height:h
                                                                                        mipmapped:(mips > 1)];
            desc.usage = MTLTextureUsageShaderRead;
            tex = [device newTextureWithDescriptor:desc];
            if (tex) {
                [tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:img.rgba.data() bytesPerRow:w * 4];
                if (mips > 1) {
                    id<MTLCommandBuffer> cmd = [queue commandBuffer];
                    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
                    [blit generateMipmapsForTexture:tex];
                    [blit endEncoding];
                    [cmd commit];
                }
            }
        } else {
            engine.print("map: skin " + name + ": " + error + "\n", REALMS_PRINT_DEVELOPER);
        }
        skins_[name] = tex;
        return tex;
    }

    // Opaque entities, then translucent ones (shells, BFG / plasma); the view
    // weapon never sinks into walls (ref_gl's depth range 0...0.3).
    void drawEntities(id<MTLRenderCommandEncoder> encoder,
                      const MetalMapPipelines &p,
                      id<MTLSamplerState> const samplers[3][3],
                      realms::Engine &engine,
                      id<MTLDevice> device,
                      id<MTLCommandQueue> queue,
                      SandboxQuakeUniforms quake,
                      CGSize drawableSize) {
        if (entityVertices_.empty() || entityBatches_.empty()) return;
        id<MTLBuffer> vertices = [device newBufferWithBytes:entityVertices_.data()
                                                     length:entityVertices_.size() * sizeof(mapview::EntityVertex)
                                                    options:MTLResourceStorageModeShared];
        if (!vertices) return;
        [encoder setVertexBuffer:vertices offset:0 atIndex:0];
        [encoder setFragmentTexture:p.white atIndex:2];
        [encoder setFragmentSamplerState:samplers[1][1] atIndex:0]; // clamp: skins are atlases
        MTLViewport full = {0.0, 0.0, drawableSize.width, drawableSize.height, 0.0, 1.0};
        MTLViewport weapon = full;
        weapon.zfar = 0.3;
        for (int pass = 0; pass < 2; ++pass) {
            const bool blended = pass == 1;
            [encoder setRenderPipelineState:(blended ? p.quakeEntityBlended : p.quakeEntityOpaque)];
            [encoder setDepthStencilState:(blended ? p.depthRead : p.depthWrite)];
            for (const mapview::EntityBatch &b : entityBatches_) {
                if ((b.color[3] < 0.999f) != blended || b.count <= 0) continue;
                if (size_t(b.first) + size_t(b.count) > entityVertices_.size()) continue;
                id<MTLTexture> tex = skin(engine, device, queue, b.skin);
                std::array<float, 4> tint = b.color;
                // untextured models (skin missing) get a neutral grey, boxes their colour
                if (!tex && !b.fallbackBox) tint = {0.7f * b.color[0], 0.7f * b.color[1], 0.72f * b.color[2], b.color[3]};
                SandboxDrawUniforms draw{};
                draw.objectTransform = makeColumnMatrix(b.transform.data());
                draw.baseColorFactor = simd_make_float4(std::pow(std::max(tint[0], 0.0f), 2.2f),
                                                        std::pow(std::max(tint[1], 0.0f), 2.2f),
                                                        std::pow(std::max(tint[2], 0.0f), 2.2f), tint[3]);
                draw.materialParams = simd_make_float4(0.9f, 0.5f, 0.0f, 0.0f);
                draw.drawFlags = simd_make_int4(1, 0, tex ? 1 : 0, blended ? 2 : 0);
                quake.modeCount.x = b.fullbright ? 3 : (b.lit ? 2 : 0);
                quake.light = simd_make_float4(b.light[0], b.light[1], b.light[2], 0.0f);
                [encoder setFragmentTexture:(tex ? tex : p.white) atIndex:1];
                [encoder setVertexBytes:&draw length:sizeof(draw) atIndex:2];
                [encoder setFragmentBytes:&draw length:sizeof(draw) atIndex:2];
                [encoder setFragmentBytes:&quake length:sizeof(quake) atIndex:3];
                if (b.viewWeapon) [encoder setViewport:weapon];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:NSUInteger(b.first)
                            vertexCount:NSUInteger(b.count)];
                if (b.viewWeapon) [encoder setViewport:full];
            }
        }
        [encoder setFragmentTexture:p.white atIndex:1];
    }

    void drawEffects(id<MTLRenderCommandEncoder> encoder, const MetalMapPipelines &p, id<MTLDevice> device) {
        if (effectVertices_.empty()) return;
        id<MTLBuffer> vertices = [device newBufferWithBytes:effectVertices_.data()
                                                     length:effectVertices_.size() * sizeof(mapview::EffectVertex)
                                                    options:MTLResourceStorageModeShared];
        if (!vertices) return;
        [encoder setRenderPipelineState:p.quakeEffect];
        [encoder setDepthStencilState:p.depthRead];
        [encoder setVertexBuffer:vertices offset:0 atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:effectVertices_.size()];
    }

    std::unique_ptr<mapview::Quake2View> view_;
    bool fresh_ = true;
    bool reuseCity_ = false;
    float cityScale_ = 1.0f;
    std::array<float, 3> cityOffset_{};
    id<MTLBuffer> triangles_ = nil;
    id<MTLBuffer> lightUvs_ = nil;
    id<MTLTexture> lightmap_ = nil;
    NSUInteger count_ = 0;
    std::vector<mapview::ModelRange> ranges_;
    MetalGltfModel model_;
    std::future<std::optional<city::Scene>> prepare_;
    std::map<std::string, id<MTLTexture>> skins_;
    std::vector<mapview::EntityVertex> entityVertices_;
    std::vector<mapview::EntityBatch> entityBatches_;
    std::vector<mapview::EffectVertex> effectVertices_;
    std::vector<mapview::DynamicLight> lights_;
    ekron::audio::GameSoundPlayer sound_;
    ekron::audio::Mixer *mixer_ = nullptr;
};

// ---- Game sound -----------------------------------------------------------
// One SoundSystem for the process (the engine it registers `play`,
// `soundlist`, `stopsound` with is process-wide as well), pulled by an
// AVAudioEngine source node on the audio thread.

ekron::audio::SoundSystem &IosSound() {
    // Never destroyed: the render block may still run while the process exits.
    static auto *sound = new ekron::audio::SoundSystem();
    return *sound;
}

struct IosAudioOutput {
    AVAudioEngine *engine = nil;
    id configObserver = nil;
    id interruptObserver = nil;
    bool started = false;
    bool wanted = true;
};

IosAudioOutput gAudio;
// Sounds start with the first playable sandbox frame (not under the intro).
bool gSoundHeard = false;

void IosAudioStopEngine() {
    if (gAudio.engine) [gAudio.engine stop];
    gAudio.engine = nil;
}

void IosAudioStartEngine() {
    if (gAudio.engine || !gAudio.wanted) return;
    AVAudioEngine *engine = [[AVAudioEngine alloc] init];
    double rate = [engine.outputNode outputFormatForBus:0].sampleRate;
    if (!(rate >= 8000.0)) rate = 48000.0;
    AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:rate channels:2];
    if (!format) return;
    auto &mixer = IosSound().mixer();
    mixer.setOutputRate(int(rate));
    ekron::audio::Mixer *pull = &mixer;
    AVAudioSourceNode *source = [[AVAudioSourceNode alloc]
        initWithFormat:format
           renderBlock:^OSStatus(BOOL *isSilence, const AudioTimeStamp *, AVAudioFrameCount frameCount,
                                 AudioBufferList *buffers) {
               // The mixer renders interleaved stereo; the node wants planar.
               static float scratch[2 * 512];
               float *left = buffers->mNumberBuffers > 0 ? static_cast<float *>(buffers->mBuffers[0].mData) : nullptr;
               float *right = buffers->mNumberBuffers > 1 ? static_cast<float *>(buffers->mBuffers[1].mData) : nullptr;
               if (!left) return noErr;
               AVAudioFrameCount done = 0;
               while (done < frameCount) {
                   const AVAudioFrameCount n = std::min<AVAudioFrameCount>(frameCount - done, 512);
                   pull->render(scratch, int(n));
                   for (AVAudioFrameCount i = 0; i < n; ++i) {
                       if (right) {
                           left[done + i] = scratch[2 * i];
                           right[done + i] = scratch[2 * i + 1];
                       } else {
                           left[done + i] = 0.5f * (scratch[2 * i] + scratch[2 * i + 1]);
                       }
                   }
                   done += n;
               }
               *isSilence = NO;
               return noErr;
           }];
    [engine attachNode:source];
    [engine connect:source to:engine.mainMixerNode format:format];
    [engine prepare];
    NSError *error = nil;
    if (![engine startAndReturnError:&error]) {
        NSLog(@"[Realms] audio: cannot start AVAudioEngine: %@", error);
        return;
    }
    gAudio.engine = engine;
    NSLog(@"[Realms] audio: AVAudioEngine %.0f Hz", rate);
}

void IosSoundStart(const std::string &bundleRoot, realms::Engine &engine) {
    if (gAudio.started) return;
    gAudio.started = true;
    auto &sound = IosSound();
    sound.loadAsync([bundleRoot](const std::string &path, std::vector<uint8_t> &out) {
        std::ifstream in(bundleRoot + "/" + path, std::ios::binary);
        if (!in) return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !out.empty();
    });
    sound.registerCommands(engine);
    sound.syncCvars(engine);

    // Ambient: respects the silent switch and mixes with the user's music.
    AVAudioSession *session = [AVAudioSession sharedInstance];
    [session setCategory:AVAudioSessionCategoryAmbient error:nil];
    [session setActive:YES error:nil];
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    // Route / sample-rate change: rebuild the graph at the new rate.
    gAudio.configObserver = [center addObserverForName:AVAudioEngineConfigurationChangeNotification
                                                object:nil
                                                 queue:[NSOperationQueue mainQueue]
                                            usingBlock:^(NSNotification *) {
                                                IosAudioStopEngine();
                                                IosAudioStartEngine();
                                            }];
    gAudio.interruptObserver = [center
        addObserverForName:AVAudioSessionInterruptionNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
                    const NSUInteger type = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
                    if (type == AVAudioSessionInterruptionTypeBegan) {
                        IosAudioStopEngine();
                    } else {
                        [[AVAudioSession sharedInstance] setActive:YES error:nil];
                        IosAudioStartEngine();
                    }
                }];
    IosAudioStartEngine();
}

} // namespace

@implementation SandboxBridge {
    id<MTLDevice> device_;
    id<MTLCommandQueue> commandQueue_;

    id<MTLRenderPipelineState> skyPipeline_;
    id<MTLRenderPipelineState> sandboxOpaquePipeline_;
    id<MTLRenderPipelineState> sandboxBlendedPipeline_;
    id<MTLRenderPipelineState> modelOpaquePipeline_;
    id<MTLRenderPipelineState> modelBlendedPipeline_;
    id<MTLRenderPipelineState> pipPipeline_;
    // Realms map mode with the Quake 2 stack
    id<MTLRenderPipelineState> quakeMapPipeline_;
    id<MTLRenderPipelineState> quakeEntityOpaquePipeline_;
    id<MTLRenderPipelineState> quakeEntityBlendedPipeline_;
    id<MTLRenderPipelineState> quakeEffectPipeline_;

    id<MTLDepthStencilState> skyDepthState_;
    id<MTLDepthStencilState> depthWriteState_;
    id<MTLDepthStencilState> depthReadOnlyState_;

    id<MTLSamplerState> gltfSamplers_[3][3];

    id<MTLTexture> materialArrayTexture_;
    id<MTLTexture> fallbackArrayTexture_;
    id<MTLTexture> whiteTexture_;
    id<MTLTexture> monitorColorTexture_;
    id<MTLTexture> monitorDepthTexture_;

    id<MTLBuffer> worldVertexBuffers_[kMaxBuffersInFlight];
    NSUInteger worldVertexCapacities_[kMaxBuffersInFlight];
    id<MTLBuffer> ghostVertexBuffers_[kMaxBuffersInFlight];
    NSUInteger ghostVertexCapacities_[kMaxBuffersInFlight];
    NSUInteger frameSlot_;
    dispatch_semaphore_t inFlightSemaphore_;

    MetalGltfModel cityModel_;
    MetalGltfModel carModel_;
    MetalGltfModel remoteModel_;
    MetalGltfModel droneModel_;

    bool texturesReady_;
    bool assetsInstalled_;

    std::string bundleRoot_;
    std::string supportDir_;
    std::string savePath_;

    sandbox::World world_;
    sandbox::ToolInteraction tools_;
    sandbox::ObjectDynamics dynamics_;
    sandbox::FpvDrone drone_;
    physics::PlayerController player_;
    physics::CollisionMesh builtCollision_;

    std::optional<sandbox::Object> placementPreview_;
    bool building_;
    sandbox::HandTool spawnReturnTool_;
    bool flying_;
    bool crouched_;
    bool prone_;

    int seatedCar_;
    float seatTime_;
    physics::Vec3 seatFrom_;
    bool carWasAnchored_;
    float motionSaveTime_;
    sandbox::Object carParking_;
    bool carParkingValid_;

    std::future<PreparedAssets> assetsJob_;
    std::future<bool> saveJob_;
    bool saveDirty_;
    std::future<std::optional<std::array<float, 2>>> roadJob_;

    std::chrono::steady_clock::time_point lastTime_;

    std::mutex commandMutex_;
    std::vector<int32_t> pendingCommands_;
    float lookDx_;
    float lookDy_;

    std::atomic<bool> gameplayReady_;
    std::atomic<float> stickRight_;
    std::atomic<float> stickForward_;
    std::atomic<float> verticalInput_;
    std::atomic<float> droneThrottle_;
    std::atomic<float> droneYawInput_;
    std::atomic<float> dronePitchInput_;
    std::atomic<float> droneRollInput_;

    std::atomic<int> loadStage_;
    std::atomic<int> loadPercent_;
    std::atomic<int> debugNotice_;
    std::atomic<int> currentTool_;
    std::atomic<int> currentSelection_;
    std::atomic<int> currentSurface_;
    std::atomic<int> interactionAction_;
    std::atomic<int> freezeState_;
    std::atomic<int> droneMode_;
    std::atomic<bool> holdingObject_;

    std::mutex snapshotMutex_;
    // Position, flight, stance and camera angles are captured on the render thread.
    std::array<float, 7> snapshot_;
    sandbox::RemotePlayers remotePlayers_;

    // Realms map mode (render thread, except where noted).
    MetalMapMode mapMode_;
    float mapYaw_;
    float mapPitch_;
    float mapJump_;
    bool mapCrouch_;
    std::string pendingMap_;
    bool mapChangePending_;
    std::vector<std::string> pendingMapRequests_; // commandMutex_
    std::atomic<int> mapState_;                   // status slot 17
    bool mapAttack_;                              // fire held (commands 51 / 52)
    std::atomic<int> mapGame_;                    // status slots 18...21: the
    std::atomic<int> mapHealth_;                  // Quake 2 game runs, the
    std::atomic<int> mapArmor_;                   // player's health, armor
    std::atomic<int> mapAmmo_;                    // and ammo
    std::string mapPathShown_;                    // snapshotMutex_
}

#if defined(REALMS_GAME_STATIC)
// The Quake 2 game module (game/, librealms_game.a from
// realms/pascal/build_core.sh ios-arm64): apps can't load their own
// libraries, so realms::GameServer gets the linked entry point.
extern "C" q2game_export_t *GetGameAPI(q2game_import_t *import);
#endif

// Realms Engine (Quake 2 stack: cvars, command buffer, PAK filesystem) shared
// with Desktop and Android. One engine per process; the bridge may be recreated.
// Link librealms_core.a (realms/pascal/build_core.sh ios-arm64) and define
// REALMS_PASCAL_STATIC=1 to run the Free Pascal core; otherwise the C++
// reference core runs. REALMS_GAME_STATIC=1 (with librealms_game.a) lets map
// mode run the Quake 2 game; without it maps are walked.
static realms::Engine &RealmsSharedEngine(const std::string &bundleRoot, const std::string &supportDir) {
    static realms::Engine engine;
    if (engine.initialized()) return engine;
#if defined(REALMS_GAME_STATIC)
    realms::GameServer::setLinkedGame(&GetGameAPI);
#endif
    realms::EngineConfig cfg;
    cfg.searchRoots = {supportDir, bundleRoot};
    cfg.writableRoot = supportDir;
    cfg.printSink = [](int level, const std::string &text) {
        if (level == REALMS_PRINT_DEVELOPER) return;
        NSLog(@"[Realms] %s", text.c_str());
    };
    if (!engine.init(cfg)) {
        NSLog(@"[Realms] engine failed to start");
        return engine;
    }
    realms::sandbox::BootOptions boot;
    boot.platform = "ios";
    boot.fileExists = [bundleRoot, supportDir](const std::string &path) {
        if (path.empty() || path[0] == '/' || path.find("..") != std::string::npos) return false;
        NSFileManager *fm = [NSFileManager defaultManager];
        for (const std::string &root : {supportDir, bundleRoot}) {
            NSString *full = [NSString stringWithUTF8String:(root + "/" + path).c_str()];
            if (full && [fm fileExistsAtPath:full]) return true;
        }
        return false;
    };
    boot.onMapChanged = [](const std::string &path) { noteMapChange(path); };
    realms::sandbox::boot(engine, boot);
    NSLog(@"[Realms] engine ready: core=%s", engine.backendName().c_str());
    std::string where, why;
    if (realms::GameServer::moduleAvailable({}, &where, &why)) {
        NSLog(@"[Realms] Quake 2 game module: %s", where.c_str());
    } else {
        NSLog(@"[Realms] Quake 2 game module unavailable (maps are walked): %s", why.c_str());
    }
    return engine;
}

- (realms::Engine &)realms {
    return RealmsSharedEngine(bundleRoot_, supportDir_);
}

- (nullable instancetype)initWithMetalKitView:(MTKView *)view {
    self = [super init];
    if (!self) return nil;

    device_ = view.device ?: MTLCreateSystemDefaultDevice();
    if (!device_) return nil;
    view.device = device_;

    commandQueue_ = [device_ newCommandQueue];
    if (!commandQueue_) return nil;

    // Shaders already output gamma-encoded color (pow 1/2.2), matching Android EGL.
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    view.sampleCount = 1;
    view.preferredFramesPerSecond = 60;

    inFlightSemaphore_ = dispatch_semaphore_create(kMaxBuffersInFlight);
    frameSlot_ = 0;

    if (![self buildPipelinesForView:view]) {
        return nil;
    }
    [self buildDepthAndSamplers];
    [self buildFallbackAndMonitorTextures];

    NSString *resPath = [[NSBundle mainBundle] resourcePath] ?: @"";
    bundleRoot_ = std::string([resPath UTF8String]);

    NSArray<NSString *> *supportPaths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString *baseSupport = supportPaths.firstObject ?: NSTemporaryDirectory();
    NSString *sandboxDir = [baseSupport stringByAppendingPathComponent:@"EkronSandbox"];
    [[NSFileManager defaultManager] createDirectoryAtPath:sandboxDir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    supportDir_ = std::string([sandboxDir UTF8String]);
    savePath_ = supportDir_ + "/sandbox-v1.txt";

    spawnReturnTool_ = sandbox::HandTool::HAND_EMPTY;
    seatedCar_ = -1;
    carParking_ = sandbox::Object{8, 0, 0, 0};
    carParkingValid_ = false;
    snapshot_ = {0.0f, 1.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    lastTime_ = std::chrono::steady_clock::now();

    world_.load(savePath_);
    tools_.tool = sandbox::HandTool::HAND_EMPTY;
    building_ = false;

    {
        realms::Engine &engine = [self realms];
        IosSoundStart(bundleRoot_, engine);
        __weak SandboxBridge *weakSelf = self;
        engine.addCommand("noclip", [weakSelf](const realms::CommandArgs &) {
            SandboxBridge *strongSelf = weakSelf;
            if (strongSelf) [strongSelf realmsToggleNoclip];
        });
        engine.addCommand("respawn", [weakSelf](const realms::CommandArgs &) {
            SandboxBridge *strongSelf = weakSelf;
            if (strongSelf) [strongSelf respawnPlayer];
        });
        // A map already loaded (by an earlier bridge or a boot script) opens
        // once the sandbox has finished loading.
        std::string ignored;
        while (takeMapChange(ignored)) {
        }
        if (engine.mapLoaded()) {
            pendingMap_ = engine.mapPath();
            mapChangePending_ = true;
        } else {
            gRetainedMap.reset();
        }
    }

    currentSelection_ = world_.selected;
    currentSurface_ = world_.surface;
    currentTool_ = int(tools_.tool);
    loadStage_ = 1;
    loadPercent_ = 2;

    const std::string capturedRoot = bundleRoot_;
    auto navigation = std::make_shared<sandbox::World>(world_);
    std::atomic<int> *pPercent = &loadPercent_;
    std::atomic<int> *pStage = &loadStage_;
    assetsJob_ = std::async(std::launch::async, [pPercent, pStage, capturedRoot, navigation]() {
        PreparedAssets result;
        result.textures = prepareAllTextures(capturedRoot);
        *pPercent = 12;
        *pStage = 2;

        result.valid = MetalGltfModel::prepare(capturedRoot,
                                               "models/abandoned_city_street_for_games.glb",
                                               result.city,
                                               result.log);
        *pPercent = 58;
        if (!result.valid) {
            return result;
        }

        *pStage = 3;
        navigation->walls = result.city.colliders;
        navigation->wallSegments = result.city.wallSegments;

        city::SpawnPoint anchor;
        if (city::findRoadSpawn(*navigation, result.city.minimum, result.city.maximum, -3.4f, 32.7f, anchor)) {
            navigation->x = anchor.x;
            navigation->z = anchor.z;
            if (city::findCarParking(*navigation,
                                     result.city.minimum,
                                     result.city.maximum,
                                     anchor.x + 3.0f,
                                     anchor.z - 9.0f,
                                     result.parkingX,
                                     result.parkingZ)) {
                float ground = result.city.collision->groundHeight(result.parkingX, result.parkingZ, 2.0f, -10.0f);
                result.parkingY = std::isfinite(ground) ? ground : 0.0f;
                city::LoadOptions carOpts;
                carOpts.level = false;
                carOpts.buildPhysics = false;
                carOpts.targetExtent = 4.025f;
                carOpts.yaw = 1.57079632679f;
                result.carValid = MetalGltfModel::prepare(capturedRoot,
                                                          "models/uaz_469_1973_classic.glb",
                                                          result.car,
                                                          result.log,
                                                          carOpts);
            }
        }

        city::LoadOptions remoteOpts;
        remoteOpts.level = false;
        remoteOpts.buildPhysics = false;
        remoteOpts.targetExtent = 0.38f;
        result.remoteValid = MetalGltfModel::prepare(capturedRoot,
                                                     "models/chasing_m2_rc_underwater_drone_remote.glb",
                                                     result.remote,
                                                     result.log,
                                                     remoteOpts);
        remoteOpts.targetExtent = 0.50f;
        result.droneValid = MetalGltfModel::prepare(capturedRoot,
                                                    "models/fpv_drone.glb",
                                                    result.drone,
                                                    result.log,
                                                    remoteOpts);
        if (!result.remoteValid || !result.droneValid) {
            result.valid = false;
        }
        *pPercent = 80;
        return result;
    });

    return self;
}

- (void)dealloc {
    if (assetsJob_.valid()) {
        assetsJob_.wait();
    }
    if (mapMode_.active()) {
        RetainedMap kept{mapMode_.session().path(), mapMode_.session().walker(), mapYaw_, mapPitch_, mapCrouch_, nullptr};
        // a running game is kept whole (the next bridge's map mode resumes it)
        if (mapMode_.gameRunning()) kept.game = mapMode_.releaseView();
        gRetainedMap = std::move(kept);
    }
    mapMode_.close();
    [self flushSave];
    realms::Engine &engine = [self realms];
    engine.removeCommand("noclip");
    engine.removeCommand("respawn");
}

- (void)realmsToggleNoclip {
    if (mapMode_.active()) {
        mapMode_.toggleNoclip();
        verticalInput_ = 0.0f;
        return;
    }
    if (drone_.piloting() || seatedCar_ >= 0) return;
    flying_ = !flying_;
    player_.reset(world_.x, world_.y, world_.z);
}

- (BOOL)buildPipelinesForView:(MTKView *)view {
    id<MTLLibrary> library = [device_ newDefaultLibrary];
    if (!library) {
        NSLog(@"[Ekron] Failed to load default Metal library");
        return NO;
    }

    auto makePipeline = [&](NSString *vsName, NSString *fsName, BOOL blended, BOOL hasDepth) -> id<MTLRenderPipelineState> {
        id<MTLFunction> vs = [library newFunctionWithName:vsName];
        id<MTLFunction> fs = [library newFunctionWithName:fsName];
        if (!vs || !fs) {
            NSLog(@"[Ekron] Missing Metal shader function %@ / %@", vsName, fsName);
            return nil;
        }
        MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.rasterSampleCount = view.sampleCount;
        desc.vertexFunction = vs;
        desc.fragmentFunction = fs;
        desc.colorAttachments[0].pixelFormat = view.colorPixelFormat;
        if (blended) {
            desc.colorAttachments[0].blendingEnabled = YES;
            desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
            desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
            desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
            desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        }
        desc.depthAttachmentPixelFormat = hasDepth ? view.depthStencilPixelFormat : MTLPixelFormatInvalid;

        NSError *error = nil;
        id<MTLRenderPipelineState> state = [device_ newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!state) {
            NSLog(@"[Ekron] Pipeline creation failed (%@/%@): %@", vsName, fsName, error);
        }
        return state;
    };

    skyPipeline_ = makePipeline(@"skyVertex", @"skyFragment", NO, YES);
    sandboxOpaquePipeline_ = makePipeline(@"sandboxVertex", @"sandboxFragment", NO, YES);
    sandboxBlendedPipeline_ = makePipeline(@"sandboxVertex", @"sandboxFragment", YES, YES);
    modelOpaquePipeline_ = makePipeline(@"modelVertex", @"sandboxFragment", NO, YES);
    modelBlendedPipeline_ = makePipeline(@"modelVertex", @"sandboxFragment", YES, YES);
    pipPipeline_ = makePipeline(@"pipVertex", @"pipFragment", NO, YES);
    quakeMapPipeline_ = makePipeline(@"quakeMapVertex", @"quakeMapFragment", NO, YES);
    quakeEntityOpaquePipeline_ = makePipeline(@"modelVertex", @"quakeMapFragment", NO, YES);
    quakeEntityBlendedPipeline_ = makePipeline(@"modelVertex", @"quakeMapFragment", YES, YES);
    quakeEffectPipeline_ = makePipeline(@"quakeEffectVertex", @"quakeEffectFragment", YES, YES);

    return skyPipeline_ && sandboxOpaquePipeline_ && sandboxBlendedPipeline_ &&
           modelOpaquePipeline_ && modelBlendedPipeline_ && pipPipeline_ &&
           quakeMapPipeline_ && quakeEntityOpaquePipeline_ && quakeEntityBlendedPipeline_ && quakeEffectPipeline_;
}

- (void)buildDepthAndSamplers {
    MTLDepthStencilDescriptor *skyDepth = [[MTLDepthStencilDescriptor alloc] init];
    skyDepth.depthCompareFunction = MTLCompareFunctionAlways;
    skyDepth.depthWriteEnabled = NO;
    skyDepthState_ = [device_ newDepthStencilStateWithDescriptor:skyDepth];

    MTLDepthStencilDescriptor *writeDepth = [[MTLDepthStencilDescriptor alloc] init];
    writeDepth.depthCompareFunction = MTLCompareFunctionLessEqual;
    writeDepth.depthWriteEnabled = YES;
    depthWriteState_ = [device_ newDepthStencilStateWithDescriptor:writeDepth];

    MTLDepthStencilDescriptor *readDepth = [[MTLDepthStencilDescriptor alloc] init];
    readDepth.depthCompareFunction = MTLCompareFunctionLessEqual;
    readDepth.depthWriteEnabled = NO;
    depthReadOnlyState_ = [device_ newDepthStencilStateWithDescriptor:readDepth];

    const MTLSamplerAddressMode modes[3] = {
        MTLSamplerAddressModeRepeat,
        MTLSamplerAddressModeClampToEdge,
        MTLSamplerAddressModeMirrorRepeat
    };
    for (int s = 0; s < 3; ++s) {
        for (int t = 0; t < 3; ++t) {
            MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
            sd.sAddressMode = modes[s];
            sd.tAddressMode = modes[t];
            sd.minFilter = MTLSamplerMinMagFilterLinear;
            sd.magFilter = MTLSamplerMinMagFilterLinear;
            sd.mipFilter = MTLSamplerMipFilterLinear;
            gltfSamplers_[s][t] = [device_ newSamplerStateWithDescriptor:sd];
        }
    }
}

- (void)buildFallbackAndMonitorTextures {
    const unsigned char whitePixel[4] = {255, 255, 255, 255};
    MTLTextureDescriptor *whiteDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
                                                                                         width:1
                                                                                        height:1
                                                                                     mipmapped:NO];
    whiteDesc.usage = MTLTextureUsageShaderRead;
    whiteTexture_ = [device_ newTextureWithDescriptor:whiteDesc];
    [whiteTexture_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:whitePixel bytesPerRow:4];

    MTLTextureDescriptor *arrDesc = [[MTLTextureDescriptor alloc] init];
    arrDesc.textureType = MTLTextureType2DArray;
    arrDesc.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
    arrDesc.width = 1;
    arrDesc.height = 1;
    arrDesc.arrayLength = kTextureLayers;
    arrDesc.mipmapLevelCount = 1;
    arrDesc.usage = MTLTextureUsageShaderRead;
    fallbackArrayTexture_ = [device_ newTextureWithDescriptor:arrDesc];
    for (int slice = 0; slice < kTextureLayers; ++slice) {
        [fallbackArrayTexture_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                                 mipmapLevel:0
                                       slice:static_cast<NSUInteger>(slice)
                                   withBytes:whitePixel
                                 bytesPerRow:4
                               bytesPerImage:4];
    }

    MTLTextureDescriptor *monColor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                        width:kMonitorWidth
                                                                                       height:kMonitorHeight
                                                                                    mipmapped:NO];
    monColor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    monColor.storageMode = MTLStorageModePrivate;
    monitorColorTexture_ = [device_ newTextureWithDescriptor:monColor];

    MTLTextureDescriptor *monDepth = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                        width:kMonitorWidth
                                                                                       height:kMonitorHeight
                                                                                    mipmapped:NO];
    monDepth.usage = MTLTextureUsageRenderTarget;
    monDepth.storageMode = MTLStorageModePrivate;
    monitorDepthTexture_ = [device_ newTextureWithDescriptor:monDepth];
}

- (bool)uploadMaterialTextures:(const PreparedTextures &)images {
    const NSUInteger mips = mipCountForDimensions(kTextureSize, kTextureSize);
    MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
    desc.textureType = MTLTextureType2DArray;
    desc.pixelFormat = MTLPixelFormatRGBA8Unorm_sRGB;
    desc.width = kTextureSize;
    desc.height = kTextureSize;
    desc.arrayLength = kTextureLayers;
    desc.mipmapLevelCount = mips;
    desc.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> arrayTex = [device_ newTextureWithDescriptor:desc];
    if (!arrayTex) return false;

    for (int layer = 0; layer < kTextureLayers; ++layer) {
        const auto &rgba = images[size_t(layer)];
        [arrayTex replaceRegion:MTLRegionMake2D(0, 0, kTextureSize, kTextureSize)
                    mipmapLevel:0
                          slice:static_cast<NSUInteger>(layer)
                      withBytes:rgba.data()
                    bytesPerRow:kTextureSize * 4
                  bytesPerImage:kTextureSize * kTextureSize * 4];
    }

    if (mips > 1) {
        id<MTLCommandBuffer> cmd = [commandQueue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit generateMipmapsForTexture:arrayTex];
        [blit endEncoding];
        [cmd commit];
    }
    materialArrayTexture_ = arrayTex;
    return true;
}

- (id<MTLBuffer>)ensureVertexBufferAtSlot:(NSUInteger)slot
                                    ghost:(BOOL)isGhost
                               byteLength:(NSUInteger)byteLength {
    const NSUInteger needed = std::max<NSUInteger>(byteLength, 4096);
    id<MTLBuffer> __strong *bufPtr = isGhost ? &ghostVertexBuffers_[slot] : &worldVertexBuffers_[slot];
    NSUInteger *capPtr = isGhost ? &ghostVertexCapacities_[slot] : &worldVertexCapacities_[slot];

    if (!*bufPtr || *capPtr < needed) {
        NSUInteger nextCap = std::max<NSUInteger>(needed, (*capPtr) * 2);
        nextCap = (nextCap + 4095u) & ~NSUInteger(4095u);
        *bufPtr = [device_ newBufferWithLength:nextCap options:MTLResourceStorageModeShared];
        *capPtr = *bufPtr ? nextCap : 0;
    }
    return *bufPtr;
}

- (void)rebuildBuiltCollision {
    builtCollision_ = physics::CollisionMesh{};
    for (size_t i = 0; i < world_.objects.size(); ++i) {
        if (int(i) == tools_.held) continue;
        sandbox::addObjectCollision(builtCollision_, world_.objects[i]);
    }
    builtCollision_.build();
}

- (void)respawnPlayer {
    if (mapMode_.active()) {
        if (mapMode_.respawn([self realms])) {
            mapYaw_ = mapMode_.spawnRenderYaw();
            mapPitch_ = 0.0f;
            [self resetInput];
        }
        return;
    }
    if (!cityModel_.scene().collision) return;
    tools_.cancel(world_);
    [self rebuildBuiltCollision];
    auto point = sandbox::recoveryPoint(world_, *cityModel_.scene().collision, builtCollision_);
    if (!point) {
        debugNotice_ = 2;
        return;
    }
    world_.x = point->x;
    world_.y = point->y;
    world_.z = point->z;
    world_.yaw = 0.0f;
    world_.pitch = 0.0f;
    flying_ = false;
    player_.reset(world_.x, world_.y, world_.z);
    stickRight_ = 0.0f;
    stickForward_ = 0.0f;
    verticalInput_ = 0.0f;
    [self resetInput];
    debugNotice_ = 4;
}

- (void)advanceLoading {
    if (!assetsInstalled_) {
        if (!assetsJob_.valid() || assetsJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        PreparedAssets data;
        try {
            data = assetsJob_.get();
        } catch (const std::exception &e) {
            NSLog(@"[Ekron] Asset loading exception: %s", e.what());
            loadStage_ = -1;
            return;
        }
        if (!data.valid) {
            NSLog(@"[Ekron] Asset preparation failed: %s", data.log.c_str());
            loadStage_ = -1;
            return;
        }

        texturesReady_ = [self uploadMaterialTextures:data.textures];
        cityModel_.beginUpload(std::move(data.city));
        if (data.carValid) carModel_.beginUpload(std::move(data.car));
        if (data.remoteValid) remoteModel_.beginUpload(std::move(data.remote));
        if (data.droneValid) droneModel_.beginUpload(std::move(data.drone));

        world_.scenery.clear();
        world_.walls = cityModel_.scene().colliders;
        world_.wallSegments = cityModel_.scene().wallSegments;

        savePath_ = supportDir_ + "/city-glb-v1.txt";
        world_.objects.clear();
        const bool restoredWorld = world_.load(savePath_);

        carParking_ = sandbox::Object{8, 0, data.parkingX, data.parkingZ};
        carParking_.y = data.parkingY;
        carParkingValid_ = data.carValid;

        const bool carExists = std::any_of(world_.objects.begin(), world_.objects.end(), [](const sandbox::Object &o) {
            return o.type == 8;
        });
        if (data.carValid && !restoredWorld && !carExists && world_.objects.size() < sandbox::World::limit) {
            sandbox::Object car{8, 0, data.parkingX, data.parkingZ};
            car.y = data.parkingY + 0.5f;
            world_.objects.push_back(car);
        }

        const auto config = readSpawnConfig(supportDir_, bundleRoot_);
        if (config.enabled) {
            world_.x = config.x;
            world_.y = config.y;
            world_.z = config.z;
            world_.yaw = config.yawDegrees * 0.01745329252f;
            world_.pitch = 0.0f;
            flying_ = config.startFlying;
        }

        player_.setBoundary(cityModel_.scene().minimum[0],
                            cityModel_.scene().maximum[0],
                            cityModel_.scene().minimum[2],
                            cityModel_.scene().maximum[2]);
        player_.reset(world_.x, world_.y, world_.z);
        [self rebuildBuiltCollision];
        assetsInstalled_ = true;
        loadStage_ = 4;
    }

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 8; ++i) {
        if (cityModel_.uploading()) cityModel_.uploadStep(device_, commandQueue_, whiteTexture_);
        if (carModel_.uploading()) carModel_.uploadStep(device_, commandQueue_, whiteTexture_);
        if (remoteModel_.uploading()) remoteModel_.uploadStep(device_, commandQueue_, whiteTexture_);
        if (droneModel_.uploading()) droneModel_.uploadStep(device_, commandQueue_, whiteTexture_);
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(4)) {
            break;
        }
    }

    loadPercent_ = 80 + int(19.0f * (cityModel_.uploadProgress() + carModel_.uploadProgress()) * 0.5f);
    if (!cityModel_.uploading() && !carModel_.uploading() &&
        !remoteModel_.uploading() && !droneModel_.uploading()) {
        if (!cityModel_.ready()) {
            loadStage_ = -1;
            return;
        }
        loadPercent_ = 99;
        loadStage_ = 5;
    }
}

- (void)processCommands {
    std::vector<int32_t> pending;
    std::vector<std::string> mapRequests;
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        pending.swap(pendingCommands_);
        mapRequests.swap(pendingMapRequests_);
        if (gameplayReady_) {
            if (mapMode_.active()) {
                realms::Engine &engine = [self realms];
                const float look = 0.004f * realms::sandbox::lookScale(engine);
                mapYaw_ -= lookDx_ * look;
                mapPitch_ = std::clamp(mapPitch_ - realms::sandbox::pitchSign(engine) * lookDy_ * look, -1.5f, 1.5f);
            } else if (drone_.mode == sandbox::FpvDrone::Flying) {
                drone_.look(lookDx_, lookDy_);
            } else if (!drone_.piloting()) {
                realms::Engine &engine = [self realms];
                const float look = 0.004f * realms::sandbox::lookScale(engine);
                world_.yaw -= lookDx_ * look;
                world_.pitch = std::clamp(world_.pitch - realms::sandbox::pitchSign(engine) * lookDy_ * look, -1.3f, 1.3f);
            }
        }
        lookDx_ = 0.0f;
        lookDy_ = 0.0f;
    }

    if (!gameplayReady_) {
        pending.clear();
        [self resetInput];
        drone_.leave();
        droneThrottle_ = 0.0f;
        droneYawInput_ = 0.0f;
        dronePitchInput_ = 0.0f;
        droneRollInput_ = 0.0f;
    }

    {
        realms::Engine &engine = [self realms];
        for (const std::string &name : mapRequests) {
            [self pollMapChange];
            engine.exec("map \"" + name + "\"\n");
            // A failed load reports "" (the previous map is gone too).
            const BOOL changed = [self pollMapChange];
            debugNotice_ = (changed && !pendingMap_.empty()) ? 0 : 7;
        }
        if (std::find(pending.begin(), pending.end(), 50) != pending.end()) {
            engine.exec("killserver\n");
            [self pollMapChange];
        }
        if (mapMode_.active() || mapChangePending_) {
            // Map mode: movement commands only; sandbox tools wait for killserver.
            for (int32_t command : pending) {
                if (command == 6) {
                    mapMode_.toggleNoclip();
                    verticalInput_ = 0.0f;
                } else if (command == 51 || command == 52) {
                    mapAttack_ = command == 51; // fire pressed / released
                } else if (command == 8) {
                    mapJump_ = 0.2f;
                } else if (command == 9) {
                    mapCrouch_ = !mapCrouch_;
                } else if (command == 35) {
                    [self respawnPlayer];
                }
            }
            pending.clear();
        }
    }

    if (roadJob_.valid() && roadJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto point = roadJob_.get();
        if (point) {
            world_.x = (*point)[0];
            world_.z = (*point)[1];
            float ground = cityModel_.scene().collision->groundHeight(world_.x, world_.z, 2.0f, -10.0f);
            world_.y = std::isfinite(ground) ? ground + 1.7f : 1.7f;
            world_.yaw = 0.0f;
            world_.pitch = 0.0f;
            flying_ = false;
            player_.reset(world_.x, world_.y, world_.z);
            [self resetInput];
        }
    }

    for (int32_t command : pending) {
        bool changed = false;

        if (command == 45 && cityModel_.ready()) {
            drone_.toggleView(*cityModel_.scene().collision, builtCollision_);
            continue;
        }
        if (command == 42) {
            if (drone_.piloting()) {
                drone_.leave();
            } else if (tools_.tool == sandbox::HandTool::DRONE_REMOTE) {
                tools_.select(world_, sandbox::HandTool::HAND_EMPTY);
                building_ = false;
            }
            droneThrottle_ = 0.0f;
            droneYawInput_ = 0.0f;
            dronePitchInput_ = 0.0f;
            droneRollInput_ = 0.0f;
            [self resetInput];
            continue;
        }
        if (drone_.piloting()) continue;

        if (command == 44 && cityModel_.ready() && tools_.tool == sandbox::HandTool::DRONE_REMOTE) {
            drone_.returnHome({world_.x, world_.y, world_.z}, world_.yaw, *cityModel_.scene().collision, builtCollision_);
            continue;
        }
        if (command == 40 && cityModel_.ready() && seatedCar_ < 0) {
            if (drone_.take({world_.x, world_.y, world_.z}, world_.yaw, *cityModel_.scene().collision, builtCollision_)) {
                tools_.select(world_, sandbox::HandTool::HAND_EMPTY);
                building_ = false;
                placementPreview_.reset();
                [self rebuildBuiltCollision];
            }
            continue;
        }
        if (command == 41 && cityModel_.ready()) {
            drone_.place(*cityModel_.scene().collision, builtCollision_);
            continue;
        }
        if (command == 43 && cityModel_.ready() && tools_.tool == sandbox::HandTool::DRONE_REMOTE) {
            if (drone_.connect({world_.x, world_.y, world_.z}, world_.yaw, world_.pitch, *cityModel_.scene().collision, builtCollision_)) {
                [self resetInput];
            }
            continue;
        }
        if (command == 36 && cityModel_.ready()) {
            if (seatedCar_ >= 0) {
                auto exit = sandbox::carExit(world_,
                                             seatedCar_,
                                             *cityModel_.scene().collision,
                                             builtCollision_,
                                             cityModel_.scene().minimum,
                                             cityModel_.scene().maximum);
                if (exit) {
                    auto &o = world_.objects[size_t(seatedCar_)];
                    o.anchored = carWasAnchored_;
                    world_.x = exit->x;
                    world_.y = exit->y;
                    world_.z = exit->z;
                    seatedCar_ = -1;
                    player_.reset(world_.x, world_.y, world_.z);
                    [self resetInput];
                } else {
                    debugNotice_ = 5;
                }
            } else if (!tools_.holding()) {
                int target = sandbox::interactionTarget(world_, *cityModel_.scene().collision);
                if (target >= 0) {
                    auto &o = world_.objects[size_t(target)];
                    if (o.type == 3) {
                        o.doorTarget = o.doorTarget > 0.5f ? 0.0f : 1.57079633f;
                    } else {
                        tools_.cancel(world_);
                        seatedCar_ = target;
                        seatFrom_ = {world_.x, world_.y, world_.z};
                        seatTime_ = 0.0f;
                        carWasAnchored_ = o.anchored;
                        o.anchored = true;
                        o.vx = o.vy = o.vz = 0.0f;
                        flying_ = false;
                        world_.yaw = float(o.rotation) * 1.57079633f + (o.type == 7 ? 3.14159265f : -1.57079633f);
                        world_.pitch = 0.0f;
                        [self resetInput];
                    }
                }
            }
            continue;
        }
        if (seatedCar_ >= 0) continue;

        if (command >= 20 && command <= 24) {
            tools_.select(world_, static_cast<sandbox::HandTool>(command - 20));
            building_ = tools_.tool == sandbox::HandTool::SPAWN_GUN;
            [self rebuildBuiltCollision];
        } else if (command == 34 && carParkingValid_) {
            tools_.cancel(world_);
            auto parkingWorld = world_;
            auto previous = std::find_if(parkingWorld.objects.begin(), parkingWorld.objects.end(), [](const sandbox::Object &o) {
                return o.type == 8;
            });
            if (previous != parkingWorld.objects.end()) {
                parkingWorld.objects.erase(previous);
            }
            auto restored = sandbox::carRespawn(parkingWorld,
                                                carParking_,
                                                *cityModel_.scene().collision,
                                                cityModel_.scene().minimum,
                                                cityModel_.scene().maximum);
            if (restored) {
                const bool exists = std::any_of(world_.objects.begin(), world_.objects.end(), [](const sandbox::Object &o) {
                    return o.type == 8;
                });
                if (exists || world_.objects.size() < sandbox::World::limit) {
                    auto first = std::find_if(world_.objects.begin(), world_.objects.end(), [](const sandbox::Object &o) {
                        return o.type == 8;
                    });
                    if (first != world_.objects.end()) {
                        *first = *restored;
                    } else {
                        world_.objects.push_back(*restored);
                    }
                    changed = true;
                    debugNotice_ = 0;
                } else {
                    debugNotice_ = 3;
                }
            } else {
                debugNotice_ = 3;
            }
            [self rebuildBuiltCollision];
        } else if (command == 35) {
            [self respawnPlayer];
        } else if (command >= 300 && command <= 312) {
            if (!building_) {
                spawnReturnTool_ = (tools_.tool == sandbox::HandTool::GRAVITY_GUN || tools_.tool == sandbox::HandTool::DRONE_REMOTE)
                    ? tools_.tool
                    : sandbox::HandTool::HAND_EMPTY;
            }
            tools_.select(world_, sandbox::HandTool::SPAWN_GUN);
            building_ = true;
            world_.selected = command - 300;
            world_.surface = sandbox::World::fixedSurface(world_.selected);
            placementPreview_.reset();
            [self rebuildBuiltCollision];
        } else if (command >= 100 && command <= 112 && building_) {
            world_.selected = command - 100;
            world_.surface = sandbox::World::fixedSurface(world_.selected);
        } else if (command == 31 && tools_.tool == sandbox::HandTool::SPAWN_GUN) {
            world_.rotation = (world_.rotation + 3) % 4;
        } else if (command == 32 && tools_.tool == sandbox::HandTool::GRAVITY_GUN) {
            if (tools_.holding()) {
                changed = tools_.release(world_);
            } else {
                tools_.grab(world_, cityModel_.scene().collision.get());
            }
            [self rebuildBuiltCollision];
        } else if (command == 38 && tools_.tool == sandbox::HandTool::GRAVITY_GUN) {
            changed = tools_.freeze(world_, cityModel_.scene().collision.get());
        } else if (command == 33 && tools_.tool == sandbox::HandTool::GRAVITY_GUN) {
            tools_.rotate(world_, cityModel_.scene().collision.get());
        } else if (command == 1 && building_) {
            world_.selected = (world_.selected + 1) % 8;
        } else if (command == 2 && building_) {
            world_.rotation = (world_.rotation + 1) % 4;
        } else if ((command == 3 || (command >= 200 && command <= 212)) && building_ && cityModel_.ready()) {
            if (command >= 200) {
                world_.selected = command - 200;
                world_.surface = sandbox::World::fixedSurface(world_.selected);
            }
            auto point = sandbox::RaycastPlacement::commitPreview(world_,
                                                                  placementPreview_,
                                                                  *cityModel_.scene().collision,
                                                                  builtCollision_,
                                                                  cityModel_.scene().minimum,
                                                                  cityModel_.scene().maximum);
            if (point && (point->type != 8 || carModel_.ready())) {
                world_.objects.push_back(*point);
                placementPreview_.reset();
                changed = true;
                tools_.select(world_, spawnReturnTool_);
                building_ = false;
            }
        } else if (command == 4 && tools_.tool == sandbox::HandTool::SPAWN_GUN) {
            changed = world_.remove();
        } else if (command == 6) {
            flying_ = !flying_;
            verticalInput_ = 0.0f;
            player_.reset(world_.x, world_.y, world_.z);
        } else if (command == 8) {
            player_.jump();
        } else if (command == 9) {
            crouched_ = !crouched_;
            prone_ = false;
        } else if (command == 10) {
            prone_ = !prone_;
            crouched_ = false;
        } else if (command == 7 && !roadJob_.valid() && cityModel_.ready()) {
            auto navigation = std::make_shared<sandbox::World>();
            navigation->walls = world_.walls;
            navigation->wallSegments = world_.wallSegments;
            navigation->objects = world_.objects;
            const auto lo = cityModel_.scene().minimum;
            const auto hi = cityModel_.scene().maximum;
            roadJob_ = std::async(std::launch::async, [navigation, lo, hi]() -> std::optional<std::array<float, 2>> {
                city::SpawnPoint p;
                if (city::findRoadSpawn(*navigation, lo, hi, -3.4f, 32.7f, p)) {
                    return std::array<float, 2>{p.x, p.z};
                }
                return {};
            });
        }

        if (changed) {
            saveDirty_ = true;
            [self rebuildBuiltCollision];
        }
    }

    currentTool_ = int(tools_.tool);
    holdingObject_ = tools_.holding();
    currentSelection_ = world_.selected;
    currentSurface_ = world_.surface;

    if (saveJob_.valid() && saveJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        if (!saveJob_.get()) {
            NSLog(@"[Ekron] Background world save failed");
        }
    }
    if (saveDirty_ && !saveJob_.valid()) {
        sandbox::World saved;
        saved.objects = world_.objects;
        if (seatedCar_ >= 0 && size_t(seatedCar_) < saved.objects.size()) {
            saved.objects[size_t(seatedCar_)].anchored = carWasAnchored_;
        }
        if (tools_.holding() && tools_.held >= 0 && size_t(tools_.held) < saved.objects.size()) {
            saved.objects[size_t(tools_.held)] = tools_.original;
        }
        const std::string path = savePath_;
        saveJob_ = std::async(std::launch::async, [saved = std::move(saved), path]() {
            return saved.save(path);
        });
        saveDirty_ = false;
    }
}

- (matrix_float4x4)carMatrixForObject:(const sandbox::Object &)o held:(BOOL)held {
    auto b = sandbox::World::bounds(o);
    physics::Vec3 target{b.x, b.y, b.z};
    if (held) target = tools_.display;
    const auto &lo = carModel_.scene().minimum;
    const auto &hi = carModel_.scene().maximum;
    physics::Vec3 source{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};

    const float ax = o.tiltX;
    const float az = o.tiltZ;
    const float ay = float(o.rotation) * 1.57079632679f;
    auto rotate = [&](physics::Vec3 p) {
        float x = std::cos(ay) * p.x + std::sin(ay) * p.z;
        float z = -std::sin(ay) * p.x + std::cos(ay) * p.z;
        float y = std::cos(ax) * p.y - std::sin(ax) * z;
        z = std::sin(ax) * p.y + std::cos(ax) * z;
        return physics::Vec3{std::cos(az) * x - std::sin(az) * y, std::sin(az) * x + std::cos(az) * y, z};
    };
    const auto x = rotate({1, 0, 0});
    const auto y = rotate({0, 1, 0});
    const auto z = rotate({0, 0, 1});
    const auto t = target - rotate(source);
    const float m[16] = {
        x.x, x.y, x.z, 0.0f,
        y.x, y.y, y.z, 0.0f,
        z.x, z.y, z.z, 0.0f,
        t.x, t.y, t.z, 1.0f
    };
    return makeColumnMatrix(m);
}

// Sky, shared scene uniforms, sandbox material array and default sampler.
- (void)encodeSceneSetup:(id<MTLRenderCommandEncoder>)encoder
               cameraEye:(physics::Vec3)eye
                     yaw:(float)yaw
                   pitch:(float)pitch
                    roll:(float)roll
                  aspect:(float)aspect {
    [encoder setCullMode:MTLCullModeNone];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];

    // Sky pass
    SandboxSkyUniforms sky{};
    sky.anglesRollAspect = simd_make_float4(yaw, pitch, roll, aspect);
    [encoder setRenderPipelineState:skyPipeline_];
    [encoder setDepthStencilState:skyDepthState_];
    [encoder setFragmentBytes:&sky length:sizeof(sky) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

    // Shared scene uniforms
    SandboxSceneUniforms scene{};
    scene.eyeAspect = simd_make_float4(eye.x, eye.y, eye.z, aspect);
    scene.anglesRoll = simd_make_float4(yaw, pitch, roll, 0.0f);
    scene.lightDirection = simd_make_float4(-0.45f, -1.0f, -0.25f, 0.0f);
    scene.lightColor = simd_make_float4(0.55f, 0.52f, 0.48f, 0.0f);
    scene.ambientColor = simd_make_float4(0.55f, 0.59f, 0.65f, 0.0f);
    scene.flags = simd_make_int4(texturesReady_ ? 1 : 0, 0, 0, 0);

    [encoder setVertexBytes:&scene length:sizeof(scene) atIndex:1];
    [encoder setFragmentBytes:&scene length:sizeof(scene) atIndex:1];
    [encoder setFragmentTexture:(texturesReady_ && materialArrayTexture_ ? materialArrayTexture_ : fallbackArrayTexture_) atIndex:0];
    [encoder setFragmentTexture:whiteTexture_ atIndex:1];
    [encoder setFragmentSamplerState:gltfSamplers_[0][0] atIndex:0];
}

- (void)encodeWorldView:(id<MTLRenderCommandEncoder>)encoder
            worldBuffer:(id<MTLBuffer>)worldBuf
       worldVertexCount:(NSUInteger)worldCount
              cameraEye:(physics::Vec3)eye
                    yaw:(float)yaw
                  pitch:(float)pitch
                   roll:(float)roll
                 aspect:(float)aspect {
    [self encodeSceneSetup:encoder cameraEye:eye yaw:yaw pitch:pitch roll:roll aspect:aspect];

    const matrix_float4x4 identity = matrix_identity_float4x4;

    if (worldBuf && worldCount > 0) {
        SandboxDrawUniforms draw{};
        draw.objectTransform = identity;
        draw.baseColorFactor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
        draw.materialParams = simd_make_float4(0.94f, 0.5f, 0.0f, 0.0f);
        draw.drawFlags = simd_make_int4(0, 0, 0, 0);

        [encoder setRenderPipelineState:sandboxOpaquePipeline_];
        [encoder setDepthStencilState:depthWriteState_];
        [encoder setVertexBuffer:worldBuf offset:0 atIndex:0];
        [encoder setVertexBytes:&draw length:sizeof(draw) atIndex:2];
        [encoder setFragmentBytes:&draw length:sizeof(draw) atIndex:2];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:worldCount];
    }

    cityModel_.draw(encoder,
                    modelOpaquePipeline_,
                    modelBlendedPipeline_,
                    depthWriteState_,
                    depthReadOnlyState_,
                    gltfSamplers_,
                    whiteTexture_,
                    identity,
                    eye.x,
                    eye.z,
                    false);

    if (carModel_.ready()) {
        for (size_t i = 0; i < world_.objects.size(); ++i) {
            if (world_.objects[i].type == 8) {
                const matrix_float4x4 carMat = [self carMatrixForObject:world_.objects[i] held:(int(i) == tools_.held)];
                carModel_.draw(encoder,
                               modelOpaquePipeline_,
                               modelBlendedPipeline_,
                               depthWriteState_,
                               depthReadOnlyState_,
                               gltfSamplers_,
                               whiteTexture_,
                               carMat,
                               eye.x,
                               eye.z,
                               false);
            }
        }
    }
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

- (void)drawInMTKView:(MTKView *)view {
    const CGSize drawSize = view.drawableSize;
    if (drawSize.width < 1.0 || drawSize.height < 1.0) return;

    dispatch_semaphore_wait(inFlightSemaphore_, DISPATCH_TIME_FOREVER);
    const NSUInteger slot = frameSlot_;
    frameSlot_ = (frameSlot_ + 1) % kMaxBuffersInFlight;

    [self processCommands];

    if (loadStage_ >= 0 && loadStage_ < 5) {
        [self advanceLoading];
    }

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - lastTime_).count(), 0.0f, 0.05f);
    lastTime_ = now;
    [self realms].frame(dt);

    // Realms map mode replaces the sandbox frame while a map is loaded.
    [self pollMapChange];
    if (mapChangePending_ && loadStage_ == 6) {
        [self handleMapChange];
    }
    if (mapMode_.active()) {
        [self renderMapInView:view dt:dt];
        return;
    }

    if (gameplayReady_ && cityModel_.ready()) {
        const size_t countBefore = world_.objects.size();
        const bool moving = dynamics_.update(world_,
                                             *cityModel_.scene().collision,
                                             dt,
                                             tools_.held,
                                             cityModel_.scene().minimum,
                                             cityModel_.scene().maximum,
                                             &tools_.held,
                                             &seatedCar_);
        if (world_.objects.size() != countBefore) {
            saveDirty_ = true;
        }
        if (moving) {
            [self rebuildBuiltCollision];
            motionSaveTime_ += dt;
            if (motionSaveTime_ > 0.75f) {
                saveDirty_ = true;
                motionSaveTime_ = 0.0f;
            }
        } else if (motionSaveTime_ > 0.0f) {
            saveDirty_ = true;
            motionSaveTime_ = 0.0f;
        }
    }

    if (gameplayReady_) {
        bool doorsMoved = false;
        for (auto &o : world_.objects) {
            if (o.type == 3 && !o.frozen && std::abs(o.doorAngle - o.doorTarget) > 0.0001f) {
                const float oldAngle = o.doorAngle;
                o.doorAngle += std::clamp(o.doorTarget - o.doorAngle, -dt * 2.2f, dt * 2.2f);
                physics::CollisionMesh sweep;
                sandbox::addObjectCollision(sweep, o);
                sweep.build();
                if (player_.overlaps(sweep, world_.x, world_.y, world_.z)) {
                    o.doorAngle = oldAngle;
                    o.doorTarget = oldAngle;
                } else {
                    doorsMoved = true;
                }
            }
        }
        if (doorsMoved) {
            [self rebuildBuiltCollision];
        }
        if (seatedCar_ >= 0 && size_t(seatedCar_) < world_.objects.size()) {
            const auto seat = sandbox::driverSeat(world_.objects[size_t(seatedCar_)]);
            seatTime_ = std::min(1.0f, seatTime_ + dt / 0.65f);
            const float t = seatTime_ * seatTime_ * (3.0f - 2.0f * seatTime_);
            const auto p = seatFrom_ + (seat - seatFrom_) * t;
            world_.x = p.x;
            world_.y = p.y;
            world_.z = p.z;
        }
    }

    if (gameplayReady_ && seatedCar_ < 0 && !drone_.piloting()) {
        if (flying_) {
            city::fly(world_, stickRight_.load(), stickForward_.load(), verticalInput_.load(), dt);
        } else if (cityModel_.ready()) {
            player_.update(*cityModel_.scene().collision,
                           stickRight_.load(),
                           stickForward_.load(),
                           world_.yaw,
                           dt,
                           crouched_,
                           world_.x,
                           world_.y,
                           world_.z,
                           &builtCollision_,
                           prone_,
                           cityModel_.scene().fenceBarriers.get());
        } else {
            world_.move(stickRight_.load(), stickForward_.load(), dt);
        }
    }

    if (cityModel_.ready()) {
        if (seatedCar_ < 0 && !flying_ &&
            sandbox::needsPlayerRecovery(world_, cityModel_.scene().minimum, cityModel_.scene().maximum)) {
            [self respawnPlayer];
        }
        const auto &lo = cityModel_.scene().minimum;
        const auto &hi = cityModel_.scene().maximum;
        world_.x = std::clamp(world_.x, lo[0] + 0.25f, hi[0] - 0.25f);
        world_.z = std::clamp(world_.z, lo[2] + 0.25f, hi[2] - 0.25f);
    }

    interactionAction_ = 0;
    if (seatedCar_ >= 0) {
        interactionAction_ = 4;
    } else if (cityModel_.ready() && !tools_.holding()) {
        const int target = sandbox::interactionTarget(world_, *cityModel_.scene().collision);
        if (target >= 0) {
            const auto &o = world_.objects[size_t(target)];
            interactionAction_ = (o.type == 8 || o.type == 7) ? 3 : (o.doorTarget > 0.5f ? 2 : 1);
        }
    }

    if (cityModel_.ready()) {
        if (drone_.mode == sandbox::FpvDrone::InHands && gameplayReady_) {
            if (tools_.tool != sandbox::HandTool::HAND_EMPTY) {
                drone_.mode = sandbox::FpvDrone::Absent;
            } else {
                const auto p = physics::Vec3{
                    world_.x - std::sin(world_.yaw) * 0.75f,
                    world_.y - 0.35f,
                    world_.z - std::cos(world_.yaw) * 0.75f
                };
                if (!drone_.blocked(p, *cityModel_.scene().collision, builtCollision_)) {
                    drone_.position = p;
                    drone_.yaw = world_.yaw;
                }
            }
        }
        if (gameplayReady_) {
            drone_.update(dt,
                          droneRollInput_.load(),
                          dronePitchInput_.load(),
                          droneThrottle_.load(),
                          droneYawInput_.load(),
                          0.0f,
                          *cityModel_.scene().collision,
                          builtCollision_);
        }
        if (drone_.piloting() || drone_.mode == sandbox::FpvDrone::Hover || drone_.mode == sandbox::FpvDrone::Returning) {
            drone_.position.x = std::clamp(drone_.position.x, cityModel_.scene().minimum[0] + 0.25f, cityModel_.scene().maximum[0] - 0.25f);
            drone_.position.z = std::clamp(drone_.position.z, cityModel_.scene().minimum[2] + 0.25f, cityModel_.scene().maximum[2] - 0.25f);
            drone_.position.y = std::min(drone_.position.y, cityModel_.scene().maximum[1] + 15.0f);
        }
        if (drone_.position.y < cityModel_.scene().minimum[1] - 5.0f) {
            if (drone_.piloting()) {
                drone_.leave();
            } else if (drone_.mode == sandbox::FpvDrone::Ground) {
                drone_.mode = sandbox::FpvDrone::Absent;
            }
        }
        if (tools_.tool == sandbox::HandTool::DRONE_REMOTE && !drone_.piloting() &&
            ((drone_.linked && (drone_.mode == sandbox::FpvDrone::Hover ||
                                drone_.mode == sandbox::FpvDrone::Ground ||
                                drone_.mode == sandbox::FpvDrone::Returning)) ||
             drone_.aimed({world_.x, world_.y, world_.z},
                          world_.yaw,
                          world_.pitch,
                          *cityModel_.scene().collision,
                          builtCollision_))) {
            interactionAction_ = 6;
        }
    }

    if (cityModel_.ready() && drone_.outsideView() && drone_.piloting()) {
        drone_.updateChase(*cityModel_.scene().collision, builtCollision_);
    }
    droneMode_ = int(drone_.mode);

    const physics::Vec3 cameraEye = drone_.piloting() ? drone_.camera() : physics::Vec3{world_.x, world_.y, world_.z};
    const float cameraYaw = drone_.piloting() ? drone_.viewYaw() : world_.yaw;
    const float cameraPitch = drone_.piloting() ? drone_.viewPitch() : world_.pitch;
    const float cameraRoll = drone_.piloting() ? drone_.viewRoll() : 0.0f;

    // Game sound: the listener is the camera (the drone when piloting).
    if (gameplayReady_ && cityModel_.ready()) gSoundHeard = true;
    if (gSoundHeard) {
        auto &sound = IosSound();
        sound.syncCvars([self realms]);
        ekron::audio::Listener ear;
        ear.x = cameraEye.x;
        ear.y = cameraEye.y;
        ear.z = cameraEye.z;
        ear.yaw = cameraYaw;
        sound.director().sandbox(ekron::audio::sandboxFrame(gameplayReady_ && cityModel_.ready(), ear, world_, tools_,
                                                            drone_, player_, seatedCar_, flying_,
                                                            droneThrottle_.load()),
                                 dt, sound);
    }

    if (gameplayReady_) {
        tools_.pull(world_, dt, cityModel_.scene().collision.get());
    }

    // Build dynamic world vertex stream
    std::vector<sandbox::MeshVertex> vertices;
    vertices.reserve((world_.scenery.size() + world_.objects.size() * 10 + 1) * 36);
    sandbox::appendRemotePlayers(vertices, remotePlayers_.snapshot());
    if (cityModel_.ready()) {
        sandbox::appendCityGraffiti(vertices, *cityModel_.scene().collision);
    }
    for (const auto &b : world_.scenery) {
        sandbox::appendCube(vertices, b);
    }
    for (size_t i = 0; i < world_.objects.size(); ++i) {
        if (world_.objects[i].type == 8) continue;
        const size_t objectBegin = vertices.size();
        if (int(i) == tools_.held) {
            auto visual = world_.objects[i];
            visual.x = tools_.display.x;
            visual.z = tools_.display.z;
            const size_t begin = vertices.size();
            sandbox::appendObject(vertices, visual);
            const float dy = tools_.display.y - sandbox::World::bounds(visual).y;
            for (size_t j = begin; j < vertices.size(); ++j) {
                vertices[j].y += dy;
            }
        } else {
            sandbox::appendObject(vertices, world_.objects[i]);
        }
        sandbox::animateDoorMesh(world_.objects[i], vertices, objectBegin);
        {
            const auto &o = world_.objects[i];
            auto b = sandbox::World::bounds(o);
            if (int(i) == tools_.held) {
                b.x = tools_.display.x;
                b.y = tools_.display.y;
                b.z = tools_.display.z;
            }
            const float cx = std::cos(o.tiltX), sx = std::sin(o.tiltX);
            const float cz = std::cos(o.tiltZ), sz = std::sin(o.tiltZ);
            for (size_t j = objectBegin; j < vertices.size(); ++j) {
                auto &v = vertices[j];
                const float x = v.x - b.x, y = v.y - b.y, z = v.z - b.z;
                const float py = cx * y - sx * z, pz = sx * y + cx * z;
                v.x = b.x + cz * x - sz * py;
                v.y = b.y + sz * x + cz * py;
                v.z = b.z + pz;
                const float ny = cx * v.ny - sx * v.nz, nz = sx * v.ny + cx * v.nz;
                const float nx = cz * v.nx - sz * ny;
                v.ny = sz * v.nx + cz * ny;
                v.nx = nx;
                v.nz = nz;
            }
        }
    }

    const int highlight = tools_.holding() ? tools_.held : tools_.target(world_, cityModel_.scene().collision.get());
    freezeState_ = (highlight >= 0 && size_t(highlight) < world_.objects.size() && world_.objects[size_t(highlight)].frozen) ? 1 : 0;
    if (tools_.holding() && highlight >= 0 && tools_.tool == sandbox::HandTool::GRAVITY_GUN) {
        sandbox::appendGravityBeam(vertices, world_, tools_.display);
    }

    placementPreview_.reset();
    if (building_ && seatedCar_ < 0 && cityModel_.ready()) {
        placementPreview_ = sandbox::RaycastPlacement::preview(world_,
                                                               *cityModel_.scene().collision,
                                                               builtCollision_,
                                                               cityModel_.scene().minimum,
                                                               cityModel_.scene().maximum);
        if (placementPreview_ && placementPreview_->type == 8 && !carModel_.ready()) {
            placementPreview_.reset();
        }
    }

    id<MTLBuffer> worldBuf = nil;
    if (!vertices.empty()) {
        const NSUInteger byteLen = static_cast<NSUInteger>(vertices.size() * sizeof(sandbox::MeshVertex));
        worldBuf = [self ensureVertexBufferAtSlot:slot ghost:NO byteLength:byteLen];
        if (worldBuf) {
            std::memcpy(worldBuf.contents, vertices.data(), byteLen);
        }
    }

    MTLRenderPassDescriptor *mainPassDesc = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    id<MTLCommandBuffer> commandBuffer = [commandQueue_ commandBuffer];

    if (!commandBuffer || !mainPassDesc || !drawable) {
        dispatch_semaphore_signal(inFlightSemaphore_);
        return;
    }

    dispatch_semaphore_t sema = inFlightSemaphore_;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
        dispatch_semaphore_signal(sema);
    }];

    const BOOL showRemoteMonitor = gameplayReady_ &&
                                   tools_.tool == sandbox::HandTool::DRONE_REMOTE &&
                                   !drone_.piloting() &&
                                   seatedCar_ < 0 &&
                                   drone_.mode != sandbox::FpvDrone::Absent &&
                                   drone_.mode != sandbox::FpvDrone::InHands &&
                                   monitorColorTexture_ != nil &&
                                   monitorDepthTexture_ != nil;

    // 1. Offscreen drone camera feed pass for the controller's live monitor
    if (showRemoteMonitor) {
        MTLRenderPassDescriptor *monPass = [MTLRenderPassDescriptor renderPassDescriptor];
        monPass.colorAttachments[0].texture = monitorColorTexture_;
        monPass.colorAttachments[0].loadAction = MTLLoadActionClear;
        monPass.colorAttachments[0].storeAction = MTLStoreActionStore;
        monPass.colorAttachments[0].clearColor = MTLClearColorMake(0.56, 0.61, 0.63, 1.0);
        monPass.depthAttachment.texture = monitorDepthTexture_;
        monPass.depthAttachment.loadAction = MTLLoadActionClear;
        monPass.depthAttachment.storeAction = MTLStoreActionDontCare;
        monPass.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> monEnc = [commandBuffer renderCommandEncoderWithDescriptor:monPass];
        if (monEnc) {
            [self encodeWorldView:monEnc
                      worldBuffer:worldBuf
                 worldVertexCount:static_cast<NSUInteger>(vertices.size())
                        cameraEye:drone_.feedEye()
                              yaw:drone_.feedYaw()
                            pitch:drone_.feedPitch()
                             roll:drone_.roll
                           aspect:float(kMonitorWidth) / float(kMonitorHeight)];
            [monEnc endEncoding];
        }
    }

    // 2. Main 3D world pass
    const float mainAspect = float(drawSize.width / drawSize.height);
    mainPassDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    mainPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    mainPassDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.56, 0.61, 0.63, 1.0);
    mainPassDesc.depthAttachment.loadAction = MTLLoadActionClear;
    mainPassDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
    mainPassDesc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:mainPassDesc];
    if (encoder) {
        [self encodeWorldView:encoder
                  worldBuffer:worldBuf
             worldVertexCount:static_cast<NSUInteger>(vertices.size())
                    cameraEye:cameraEye
                          yaw:cameraYaw
                        pitch:cameraPitch
                         roll:cameraRoll
                       aspect:mainAspect];

        // Placement hologram preview
        if (placementPreview_) {
            if (placementPreview_->type == 8 && carModel_.ready()) {
                const matrix_float4x4 carMat = [self carMatrixForObject:*placementPreview_ held:NO];
                carModel_.draw(encoder,
                               modelOpaquePipeline_,
                               modelBlendedPipeline_,
                               depthWriteState_,
                               depthReadOnlyState_,
                               gltfSamplers_,
                               whiteTexture_,
                               carMat,
                               cameraEye.x,
                               cameraEye.z,
                               true);
            } else {
                std::vector<sandbox::MeshVertex> ghost;
                sandbox::appendObject(ghost, *placementPreview_, true, true);
                if (!ghost.empty()) {
                    const NSUInteger ghostBytes = static_cast<NSUInteger>(ghost.size() * sizeof(sandbox::MeshVertex));
                    id<MTLBuffer> ghostBuf = [self ensureVertexBufferAtSlot:slot ghost:YES byteLength:ghostBytes];
                    if (ghostBuf) {
                        std::memcpy(ghostBuf.contents, ghost.data(), ghostBytes);
                        SandboxDrawUniforms ghostDraw{};
                        ghostDraw.objectTransform = matrix_identity_float4x4;
                        ghostDraw.baseColorFactor = simd_make_float4(1.0f, 1.0f, 1.0f, 1.0f);
                        ghostDraw.materialParams = simd_make_float4(0.94f, 0.5f, 0.0f, 0.0f);
                        ghostDraw.drawFlags = simd_make_int4(0, 1, 0, 0);

                        [encoder setRenderPipelineState:sandboxBlendedPipeline_];
                        [encoder setDepthStencilState:depthWriteState_];
                        [encoder setFragmentTexture:(texturesReady_ && materialArrayTexture_ ? materialArrayTexture_ : fallbackArrayTexture_) atIndex:0];
                        [encoder setVertexBuffer:ghostBuf offset:0 atIndex:0];
                        [encoder setVertexBytes:&ghostDraw length:sizeof(ghostDraw) atIndex:2];
                        [encoder setFragmentBytes:&ghostDraw length:sizeof(ghostDraw) atIndex:2];
                        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(ghost.size())];
                    }
                }
            }
        }

        // 3D FPV drone mesh in world
        if (drone_.mode != sandbox::FpvDrone::Absent &&
            (drone_.mode != sandbox::FpvDrone::Flying || drone_.viewWeight() > 0.15f) &&
            droneModel_.ready()) {
            const auto &lo = droneModel_.scene().minimum;
            const auto &hi = droneModel_.scene().maximum;
            const physics::Vec3 center{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
            auto rotate = [&](physics::Vec3 p) {
                const float cr = std::cos(drone_.roll), sr = std::sin(drone_.roll);
                const float cp = std::cos(drone_.pitch), sp = std::sin(drone_.pitch);
                const float cy = std::cos(drone_.yaw), sy = std::sin(drone_.yaw);
                float x = cr * p.x + sr * p.y;
                float y = -sr * p.x + cr * p.y;
                float z = sp * y + cp * p.z;
                y = cp * y - sp * p.z;
                return physics::Vec3{cy * x + sy * z, y, -sy * x + cy * z};
            };
            const auto x = rotate({1, 0, 0});
            const auto y = rotate({0, 1, 0});
            const auto z = rotate({0, 0, 1});
            const auto t = drone_.position - rotate(center);
            const float m[16] = {
                x.x, x.y, x.z, 0.0f,
                y.x, y.y, y.z, 0.0f,
                z.x, z.y, z.z, 0.0f,
                t.x, t.y, t.z, 1.0f
            };
            droneModel_.draw(encoder,
                             modelOpaquePipeline_,
                             modelBlendedPipeline_,
                             depthWriteState_,
                             depthReadOnlyState_,
                             gltfSamplers_,
                             whiteTexture_,
                             makeColumnMatrix(m),
                             cameraEye.x,
                             cameraEye.z,
                             false);
        }

        [encoder endEncoding];
    }

    // 3. Viewmodel & live FPV monitor overlay pass (depth cleared so controller never clips into walls)
    const BOOL showRemoteViewmodel = gameplayReady_ &&
                                     tools_.tool == sandbox::HandTool::DRONE_REMOTE &&
                                     remoteModel_.ready() &&
                                     !drone_.piloting() &&
                                     seatedCar_ < 0;

    if (showRemoteViewmodel || showRemoteMonitor) {
        MTLRenderPassDescriptor *overlayPass = [MTLRenderPassDescriptor renderPassDescriptor];
        overlayPass.colorAttachments[0].texture = drawable.texture;
        overlayPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        overlayPass.colorAttachments[0].storeAction = MTLStoreActionStore;
        overlayPass.depthAttachment.texture = mainPassDesc.depthAttachment.texture;
        overlayPass.depthAttachment.loadAction = MTLLoadActionClear;
        overlayPass.depthAttachment.storeAction = MTLStoreActionDontCare;
        overlayPass.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> overlayEnc = [commandBuffer renderCommandEncoderWithDescriptor:overlayPass];
        if (overlayEnc) {
            [overlayEnc setCullMode:MTLCullModeNone];
            [overlayEnc setFrontFacingWinding:MTLWindingCounterClockwise];

            if (showRemoteViewmodel) {
                SandboxSceneUniforms vmScene{};
                vmScene.eyeAspect = simd_make_float4(0.0f, 0.0f, 0.0f, mainAspect);
                vmScene.anglesRoll = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);
                vmScene.lightDirection = simd_make_float4(-0.45f, -1.0f, -0.25f, 0.0f);
                vmScene.lightColor = simd_make_float4(0.55f, 0.52f, 0.48f, 0.0f);
                vmScene.ambientColor = simd_make_float4(0.55f, 0.59f, 0.65f, 0.0f);
                vmScene.flags = simd_make_int4(texturesReady_ ? 1 : 0, 0, 0, 0);

                [overlayEnc setVertexBytes:&vmScene length:sizeof(vmScene) atIndex:1];
                [overlayEnc setFragmentBytes:&vmScene length:sizeof(vmScene) atIndex:1];
                [overlayEnc setFragmentTexture:(texturesReady_ && materialArrayTexture_ ? materialArrayTexture_ : fallbackArrayTexture_) atIndex:0];

                const auto &lo = remoteModel_.scene().minimum;
                const auto &hi = remoteModel_.scene().maximum;
                const float rx = -(lo[0] + hi[0]) * 0.5f;
                const float ry = -0.18f - (lo[1] + hi[1]) * 0.5f;
                const float rz = -0.55f - (lo[2] + hi[2]) * 0.5f;
                const float m[16] = {
                    1, 0, 0, 0,
                    0, 1, 0, 0,
                    0, 0, 1, 0,
                    rx, ry, rz, 1
                };
                remoteModel_.draw(overlayEnc,
                                  modelOpaquePipeline_,
                                  modelBlendedPipeline_,
                                  depthWriteState_,
                                  depthReadOnlyState_,
                                  gltfSamplers_,
                                  whiteTexture_,
                                  makeColumnMatrix(m),
                                  0.0f,
                                  0.0f,
                                  false);
            }

            if (showRemoteMonitor) {
                const float w = float(drawSize.width);
                const float h = float(drawSize.height);
                const float monW = std::max(32.0f, std::min(w * 0.22f, h * 0.46f));
                const float monH = monW * (9.0f / 16.0f);
                const float pad = std::max(3.0f, monW * 0.018f);
                const float totalW = monW + 2.0f * pad;
                const float totalH = monH + 2.0f * pad;
                const float bottomY = h * 0.20f;
                const float centerY = -1.0f + 2.0f * ((bottomY + totalH * 0.5f) / h);

                SandboxPipUniforms pip{};
                pip.rectNdc = simd_make_float4(0.0f, centerY, totalW / w, totalH / h);
                pip.borderSize = simd_make_float4(pad / totalW, pad / totalH, 0.0f, 0.0f);

                [overlayEnc setRenderPipelineState:pipPipeline_];
                [overlayEnc setDepthStencilState:skyDepthState_];
                [overlayEnc setVertexBytes:&pip length:sizeof(pip) atIndex:0];
                [overlayEnc setFragmentBytes:&pip length:sizeof(pip) atIndex:0];
                [overlayEnc setFragmentTexture:monitorColorTexture_ atIndex:0];
                [overlayEnc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
            }

            [overlayEnc endEncoding];
        }
    }

    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];

    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_ = {world_.x, world_.y, world_.z, flying_ ? 1.0f : 0.0f,
                     prone_ ? 2.0f : (crouched_ ? 1.0f : 0.0f), world_.yaw, world_.pitch};
    }
    if (loadStage_ == 5) {
        loadStage_ = 6;
        loadPercent_ = 100;
    }
}

#pragma mark - Realms map mode

- (BOOL)pollMapChange {
    std::string path;
    BOOL changed = NO;
    while (takeMapChange(path)) {
        pendingMap_ = path;
        mapChangePending_ = true;
        changed = YES;
    }
    return changed;
}

- (void)showMapPath:(const std::string &)path {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    mapPathShown_ = path;
}

- (void)handleMapChange {
    mapChangePending_ = false;
    realms::Engine &engine = [self realms];
    IosSound().director().reset();
    const std::string path = pendingMap_;
    if (mapMode_.active()) mapMode_.close();
    mapState_ = 0;
    mapGame_ = 0;
    mapAttack_ = false;
    [self showMapPath:std::string()];
    if (path.empty() || !engine.mapLoaded()) {
        gRetainedMap.reset();
        [self resetInput];
        return;
    }
    std::string error;
    std::unique_ptr<mapview::Quake2View> retainedGame;
    if (gRetainedMap) retainedGame = std::move(gRetainedMap->game);
    mapAttack_ = false;
    if (!mapMode_.open(engine, device_, cityModel_, kCityAsset, {supportDir_, bundleRoot_}, error,
                       std::move(retainedGame))) {
        NSLog(@"[Realms] map mode: %s: %s", path.c_str(), error.c_str());
        gRetainedMap.reset();
        debugNotice_ = 7;
        return;
    }
    // Sandbox interactions pause; the sandbox world itself stays as it was.
    if (drone_.piloting()) drone_.leave();
    droneThrottle_ = 0.0f;
    droneYawInput_ = 0.0f;
    dronePitchInput_ = 0.0f;
    droneRollInput_ = 0.0f;
    placementPreview_.reset();
    mapYaw_ = mapMode_.spawnRenderYaw();
    mapPitch_ = 0.0f;
    mapCrouch_ = false;
    mapJump_ = 0.0f;
    if (gRetainedMap && gRetainedMap->path == mapMode_.session().path()) {
        mapMode_.session().walker() = gRetainedMap->walker;
        mapYaw_ = gRetainedMap->yaw;
        mapPitch_ = gRetainedMap->pitch;
        mapCrouch_ = gRetainedMap->crouch;
    }
    gRetainedMap.reset();
    mapState_ = mapMode_.texturedPending() ? 2 : 1;
    [self showMapPath:mapMode_.session().path()];
    debugNotice_ = 0;
    [self resetInput];
    const auto &w = mapMode_.session().walker();
    NSLog(@"[Realms] map mode: %s (%s), spawn %s at %.1f %.1f %.1f%s", path.c_str(),
          mapMode_.session().summary().c_str(), w.spawnSource.c_str(), w.origin[0], w.origin[1], w.origin[2],
          mapMode_.gameRunning() ? ", Quake 2 game" : "");
}

- (void)renderMapInView:(MTKView *)view dt:(float)dt {
    realms::Engine &engine = [self realms];
    mapMode_.advance(device_, commandQueue_, whiteTexture_);
    if (gameplayReady_) {
        realms::WalkInput input;
        input.forward = stickForward_.load();
        input.right = stickRight_.load();
        input.up = verticalInput_.load();
        input.jump = mapJump_ > 0.0f;
        input.crouch = mapCrouch_;
        mapMode_.update(engine, input, mapYaw_, mapPitch_, dt, mapAttack_);
    }
    for (const std::string &message : mapMode_.takeMessages()) NSLog(@"[Realms] %s", message.c_str());
    const bool game = mapMode_.gameRunning();
    if (!game) mapAttack_ = false;
    const realms::Quake2Module::PlayerStats stats = mapMode_.stats();
    mapGame_ = game ? 1 : 0;
    mapHealth_ = stats.health;
    mapArmor_ = stats.armor;
    mapAmmo_ = stats.ammo;
    if (gSoundHeard) {
        auto &sound = IosSound();
        sound.syncCvars(engine);
        sound.director().map(ekron::audio::mapFrame(gameplayReady_, engine, mapMode_.session().walker(), mapYaw_), dt,
                             sound);
    }
    // Game sounds drain every frame; they play once sound is up and not paused.
    mapMode_.playSounds(engine, IosSound().mixer(), dt, !gameplayReady_ || !gSoundHeard);
    mapJump_ = std::max(0.0f, mapJump_ - dt);
    mapState_ = mapMode_.texturedPending() ? 2 : 1;
    interactionAction_ = 0;
    freezeState_ = 0;
    holdingObject_ = false;
    droneMode_ = 0;

    MTLRenderPassDescriptor *passDesc = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    id<MTLCommandBuffer> commandBuffer = [commandQueue_ commandBuffer];
    if (!commandBuffer || !passDesc || !drawable) {
        dispatch_semaphore_signal(inFlightSemaphore_);
        return;
    }
    dispatch_semaphore_t sema = inFlightSemaphore_;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
        dispatch_semaphore_signal(sema);
    }];

    const CGSize drawSize = view.drawableSize;
    const float aspect = float(drawSize.width / drawSize.height);
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.56, 0.61, 0.63, 1.0);
    passDesc.depthAttachment.loadAction = MTLLoadActionClear;
    passDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
    passDesc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
    if (encoder) {
        [self encodeSceneSetup:encoder cameraEye:mapMode_.drawEye() yaw:mapYaw_ pitch:mapPitch_ roll:0.0f aspect:aspect];
        MetalMapPipelines pipelines;
        pipelines.sandbox = sandboxOpaquePipeline_;
        pipelines.modelOpaque = modelOpaquePipeline_;
        pipelines.modelBlended = modelBlendedPipeline_;
        pipelines.quakeMap = quakeMapPipeline_;
        pipelines.quakeEntityOpaque = quakeEntityOpaquePipeline_;
        pipelines.quakeEntityBlended = quakeEntityBlendedPipeline_;
        pipelines.quakeEffect = quakeEffectPipeline_;
        pipelines.depthWrite = depthWriteState_;
        pipelines.depthRead = depthReadOnlyState_;
        pipelines.white = whiteTexture_;
        mapMode_.draw(encoder, pipelines, gltfSamplers_, cityModel_, engine, device_, commandQueue_, mapYaw_, mapPitch_,
                      drawSize);
        [encoder endEncoding];
    }
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];

    const auto &w = mapMode_.session().walker();
    const physics::Vec3 at = mapMode_.eye();
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = {at.x, at.y, at.z, w.noclip ? 1.0f : 0.0f, w.ducked ? 1.0f : 0.0f, mapYaw_, mapPitch_};
}

- (void)loadMap:(NSString *)path {
    const char *chars = path.UTF8String;
    if (!chars) return;
    const std::string name(chars);
    if (!mapview::safeMapName(name)) {
        NSLog(@"[Realms] map: rejected name");
        return;
    }
    std::lock_guard<std::mutex> lock(commandMutex_);
    if (pendingMapRequests_.size() < 4) pendingMapRequests_.push_back(name);
}

- (NSString *)currentMapPath {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        path = mapPathShown_;
    }
    return [NSString stringWithUTF8String:path.c_str()] ?: @"";
}

+ (NSString *)mapImportFileName:(NSString *)pickedName {
    const char *chars = pickedName.UTF8String;
    if (!chars) return @"";
    const std::string name = mapview::importFileName(chars);
    return [NSString stringWithUTF8String:name.c_str()] ?: @"";
}

+ (BOOL)mapImportHeaderOk:(NSData *)head fileName:(NSString *)fileName {
    const char *chars = fileName.UTF8String;
    if (!chars || !head) return NO;
    return mapview::importHeaderOk(chars, static_cast<const unsigned char *>(head.bytes), head.length) ? YES : NO;
}

+ (int64_t)mapImportLimit {
    return static_cast<int64_t>(mapview::kMaxImportBytes);
}

#pragma mark - Control Bridge

- (void)setGameplayReady:(BOOL)ready {
    gameplayReady_ = ready;
    if (!ready) {
        stickRight_ = 0.0f;
        stickForward_ = 0.0f;
        verticalInput_ = 0.0f;
    }
}

- (void)setMoveRight:(float)right forward:(float)forward {
    const bool ok = gameplayReady_.load();
    stickRight_ = (ok && std::isfinite(right)) ? std::clamp(right, -1.0f, 1.0f) : 0.0f;
    stickForward_ = (ok && std::isfinite(forward)) ? std::clamp(forward, -1.0f, 1.0f) : 0.0f;
}

- (void)addLookDeltaX:(float)dx deltaY:(float)dy {
    if (!gameplayReady_.load() || !std::isfinite(dx) || !std::isfinite(dy)) return;
    std::lock_guard<std::mutex> lock(commandMutex_);
    lookDx_ = std::clamp(lookDx_ + dx, -400.0f, 400.0f);
    lookDy_ = std::clamp(lookDy_ + dy, -400.0f, 400.0f);
}

- (void)setVerticalInput:(float)vertical {
    verticalInput_ = std::isfinite(vertical) ? std::clamp(vertical, -1.0f, 1.0f) : 0.0f;
}

- (void)setDroneThrottle:(float)throttle yaw:(float)yaw pitch:(float)pitch roll:(float)roll {
    const bool ok = gameplayReady_.load();
    auto clean = [ok](float v) {
        return (ok && std::isfinite(v)) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
    };
    droneThrottle_ = clean(throttle);
    droneYawInput_ = clean(yaw);
    dronePitchInput_ = clean(pitch);
    droneRollInput_ = clean(roll);
}

- (void)playUiSound:(int32_t)sound {
    static const char *const names[] = {"ui_click", "ui_open", "ui_close", "ui_confirm", "ui_error"};
    if (sound < 0 || sound > 4) return;
    IosSound().ui(names[sound]);
}

- (void)setAudioActive:(BOOL)active {
    gAudio.wanted = active;
    IosSound().mixer().setPaused(!active);
    if (!gAudio.started) return;
    if (active) {
        [[AVAudioSession sharedInstance] setActive:YES error:nil];
        IosAudioStartEngine();
    } else {
        IosAudioStopEngine();
    }
}

- (void)sendCommand:(int32_t)command {
    std::lock_guard<std::mutex> lock(commandMutex_);
    if (pendingCommands_.size() < 64) {
        pendingCommands_.push_back(command);
    }
}

- (NSInteger)pollStatus:(float *)outValues count:(NSInteger)count {
    if (!outValues || count <= 0) return 0;
    std::array<float, 22> values{};
    values[17] = float(mapState_.load());
    values[18] = float(mapGame_.load());
    values[19] = float(mapHealth_.load());
    values[20] = float(mapArmor_.load());
    values[21] = float(mapAmmo_.load());
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        std::copy_n(snapshot_.begin(), 4, values.begin());
        values[14] = snapshot_[4];
        values[15] = snapshot_[5];
        values[16] = snapshot_[6];
    }
    values[4] = float(loadStage_.load());
    values[5] = float(debugNotice_.load());
    values[6] = holdingObject_.load() ? 1.0f : 0.0f;
    values[7] = float(currentTool_.load());
    values[8] = float(loadPercent_.load());
    values[9] = float(currentSelection_.load());
    values[10] = float(currentSurface_.load());
    values[11] = float(interactionAction_.load());
    values[12] = float(freezeState_.load());
    values[13] = float(droneMode_.load());

    const NSInteger n = std::min<NSInteger>(count, static_cast<NSInteger>(values.size()));
    std::memcpy(outValues, values.data(), static_cast<size_t>(n) * sizeof(float));
    return n;
}

- (void)setRemotePlayers:(const float *)values count:(NSInteger)count {
    if (count < 0 || count > static_cast<NSInteger>(sandbox::RemotePlayers::maxPlayers * 4)) return;
    remotePlayers_.replace(values, static_cast<size_t>(count));
}

- (void)resetInput {
    verticalInput_ = 0.0f;
    stickRight_ = 0.0f;
    stickForward_ = 0.0f;
    lastTime_ = std::chrono::steady_clock::now();
}

- (void)flushSave {
    if (saveJob_.valid()) {
        (void)saveJob_.get();
    }
    if (assetsInstalled_ && !savePath_.empty()) {
        sandbox::World saved;
        saved.objects = world_.objects;
        if (seatedCar_ >= 0 && size_t(seatedCar_) < saved.objects.size()) {
            saved.objects[size_t(seatedCar_)].anchored = carWasAnchored_;
        }
        if (tools_.holding() && tools_.held >= 0 && size_t(tools_.held) < saved.objects.size()) {
            saved.objects[size_t(tools_.held)] = tools_.original;
        }
        saved.save(savePath_);
        saveDirty_ = false;
    }
    realms::sandbox::saveUserConfig([self realms]);
}

@end
