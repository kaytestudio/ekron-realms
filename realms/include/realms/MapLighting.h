// Quake 2 BSP lighting without OpenGL (gl_rsurf.c / gl_light.c): face
// lightmaps packed into 128x128 pages (LM_AllocBlock), the atlas built for
// the current light styles (R_BuildLightMap), R_LightPoint, and CS_LIGHTS
// style strings at 10 Hz ('m' = 1).
// Bad lumps fail load(); faces whose samples don't fit get no lightmap.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace realms {

class Engine;

using MapLightStyles = std::array<float, 256>; // per style index, 1 = normal ('m')
MapLightStyles defaultLightStyles();
// CL_RunLightStyles: frame (timeMs / 100) % length of `pattern`, 'a' = 0,
// 'm' = 1, 'z' = 2.08. An empty pattern is 1.
float lightStyleValue(const std::string &pattern, double timeMs);

class MapLighting {
public:
    static constexpr int kPage = 128;          // BLOCK_WIDTH / BLOCK_HEIGHT
    static constexpr int kPagesAcross = 8;     // atlas width: 1024
    static constexpr int kMaxPages = 8 * 32;   // atlas height up to 4096
    static constexpr int kLuxel = 16;          // map units (texels) per lightmap sample

    // IBSP v38 contents. False with an error when the lumps are unusable;
    // a map without lighting data loads with no lit faces (empty()).
    bool load(const unsigned char *data, size_t size, std::string &error);
    // The engine's loaded map (BSP only; glTF maps have no lightmaps).
    bool load(const Engine &engine, std::string &error);
    void clear();

    bool empty() const { return litFaces_ == 0; }
    size_t litFaces() const { return litFaces_; }
    size_t faceCount() const { return faces_.size(); }
    int width() const { return empty() ? 0 : kPage * kPagesAcross; }
    int height() const { return empty() ? 0 : kPage * ((pages_ + kPagesAcross - 1) / kPagesAcross); }
    // Faces that needed a lightmap but didn't fit (too big, atlas full, bad offsets).
    size_t droppedFaces() const { return dropped_; }
    // Light styles used by the lit faces (sorted, without 255).
    const std::vector<int> &styles() const { return styles_; }

    // Atlas coordinates (0..1) of point `p` on face `face` (gl_rsurf.c
    // BuildSurfaceDisplayList). False when the face has no lightmap (sky,
    // warp, unlit, or not in the atlas).
    bool faceUv(size_t face, const float p[3], float uv[2]) const;

    // RGBA8 atlas, width() x height(), for the given style values.
    void compose(const MapLightStyles &styles, std::vector<uint8_t> &rgba) const;
    // True when composing with `a` and `b` gives a different atlas.
    bool stylesDiffer(const MapLightStyles &a, const MapLightStyles &b) const;

    // R_LightPoint without dynamic lights (Quake2Module::lightAt adds them):
    // the light (0..~2 per channel) at the first lit world face straight
    // below `p` (down to 2048 units).
    // False when the map has no lightmaps or nothing is below; `rgb` is
    // then black, as in Quake 2.
    bool lightPoint(const float p[3], const MapLightStyles &styles, float rgb[3]) const;

private:
    struct Face {
        int texinfo = -1;
        int32_t lightofs = -1;
        std::array<uint8_t, 4> styles{{255, 255, 255, 255}};
        float texturemins[2] = {0, 0};
        int extents[2] = {0, 0};
        int smax = 0, tmax = 0;
        int page = -1, x = 0, y = 0; // atlas placement (page -1: no lightmap)
        bool samples = false;        // its samples are in the lighting lump
    };
    struct Texinfo {
        float vecs[2][4] = {};
        int32_t flags = 0;
    };
    struct Node {
        int plane = 0;
        int32_t children[2] = {0, 0};
        uint16_t firstFace = 0, numFaces = 0;
    };
    struct Plane {
        float normal[3] = {0, 0, 0};
        float dist = 0;
    };

    int recursiveLightPoint(int32_t node, const float start[3], const float end[3], const MapLightStyles &styles,
                            float rgb[3], int depth) const;
    int styleCount(const Face &f) const;

    std::vector<Face> faces_;
    std::vector<Texinfo> texinfo_;
    std::vector<Node> nodes_;
    std::vector<Plane> planes_;
    std::vector<uint8_t> lighting_;
    int32_t headnode_ = -1;
    int pages_ = 0;
    size_t litFaces_ = 0, dropped_ = 0;
    std::vector<int> styles_;
};

} // namespace realms
