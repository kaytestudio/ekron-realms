// Quake 2 world textures: the textures/<name>.wal of each face material,
// through Quake 2's palette, resampled into the square layers of one
// texture array. Texture coordinates divided by each texture's own size
// (texinfo texels / width, height) keep repeat wrapping right whatever the
// layer size.
#pragma once

#include "realms/Md2Model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace realms {

class Engine;
struct MapGeometry;

using Quake2Palette = std::array<uint8_t, 768>; // RGB per index

// Quake 2's palette (the one in pics/colormap.pcx), built in.
const Quake2Palette &builtinQuake2Palette();
// pics/colormap.pcx's palette; false, with the built-in one in `out`, when
// the file is missing or not an 8-bit PCX.
bool loadQuake2Palette(const Engine &engine, Quake2Palette &out, std::string *error = nullptr);

// A .wal (miptex_t): its full-size mip level as RGBA (alpha 255).
bool decodeWal(const unsigned char *data, size_t size, const Quake2Palette &palette, RgbaImage &out,
               std::string &error);

class MapTextures {
public:
    struct Texture {
        std::string name; // the material: texinfo's texture name
        int width = 0, height = 0;
    };
    // GL_MAX_ARRAY_TEXTURE_LAYERS is at least 256 in GL 3.3 and GLES 3.0.
    static constexpr size_t kMaxTextures = 256;

    // The .wal of every material in `geometry` (BSP faces or brushes),
    // through loadQuake2Palette. The layer size is the largest texture side
    // rounded up to a power of two (16 to maxLayerSize), halved down to 16
    // until all layers fit in maxBytes. Returns the number of textures loaded.
    size_t load(const Engine &engine, const MapGeometry &geometry, int maxLayerSize = 256,
                size_t maxBytes = size_t(48) << 20);
    void clear();

    bool empty() const { return textures_.empty(); }
    size_t count() const { return textures_.size(); }
    int layerSize() const { return layerSize_; }
    const Texture &texture(size_t index) const { return textures_[index]; }
    // The layer of a material, -1 when it has no texture.
    int find(const std::string &material) const;
    // count() layers of layerSize() x layerSize() RGBA, top row first.
    const std::vector<uint8_t> &pixels() const { return pixels_; }
    // Materials whose .wal is missing or unreadable (drawn without it).
    size_t missing() const { return missing_; }
    bool fromGamePalette() const { return gamePalette_; }

private:
    std::vector<Texture> textures_;
    std::unordered_map<std::string, int> index_;
    std::vector<uint8_t> pixels_;
    int layerSize_ = 0;
    size_t missing_ = 0;
    bool gamePalette_ = false;
};

// Resamples RGBA (w x h) to size x size as a tiling texture: bilinear with
// wrap-around when enlarging, a box filter when shrinking.
void resampleTiled(const uint8_t *rgba, int w, int h, int size, uint8_t *out);

} // namespace realms
