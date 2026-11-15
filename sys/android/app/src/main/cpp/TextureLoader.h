#pragma once
#include <GLES3/gl3.h>
struct AAssetManager;

// Layers must match the material IDs in SandboxWorld.h.
// All GL methods, including destruction, require the owning current EGL context.
class TextureLoader {
public:
    static constexpr int Size = 512;
    static constexpr int Layers = 5; // asphalt, concrete, brick, rust, untextured white
    TextureLoader() = default;
    ~TextureLoader();
    TextureLoader(const TextureLoader&) = delete;
    TextureLoader& operator=(const TextureLoader&) = delete;
    bool load(AAssetManager *assets);
    void bind(GLuint unit = 0) const;
    void release();
private:
    GLuint texture_ = 0;
};
