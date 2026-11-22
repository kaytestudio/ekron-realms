#include "realms/MapTextures.h"

#include "realms/Engine.h"
#include "realms/MapGeometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "Quake2Palette.inc"

namespace realms {

namespace {

uint32_t rdU32(const unsigned char *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// One axis of resampleTiled: `count` lines of `src` RGBA texels (texelStep
// floats apart, lines lineStep apart) to `dst` texels.
void resampleAxis(const float *in, int src, int dst, int count, size_t texelStep, size_t lineStep, float *out,
                  size_t outTexelStep, size_t outLineStep) {
    const double ratio = double(src) / double(dst);
    for (int line = 0; line < count; line++) {
        const float *row = in + size_t(line) * lineStep;
        float *orow = out + size_t(line) * outLineStep;
        for (int i = 0; i < dst; i++) {
            float acc[4] = {0, 0, 0, 0};
            if (ratio <= 1.0) {
                // bilinear between the two nearest texel centres, wrapping
                const double x = (i + 0.5) * ratio - 0.5;
                const double fx = std::floor(x);
                const float t = float(x - fx);
                const int a = int(((long long)fx % src + src) % src), b = (a + 1) % src;
                for (int c = 0; c < 4; c++)
                    acc[c] = row[size_t(a) * texelStep + size_t(c)] * (1.0f - t) +
                             row[size_t(b) * texelStep + size_t(c)] * t;
            } else {
                // the average of the source span [i, i + 1) * ratio
                const double lo = i * ratio, hi = (i + 1) * ratio;
                for (int j = int(std::floor(lo)); j < int(std::ceil(hi)); j++) {
                    const float w = float(std::min(hi, double(j + 1)) - std::max(lo, double(j)));
                    const int k = std::min(j, src - 1);
                    for (int c = 0; c < 4; c++) acc[c] += row[size_t(k) * texelStep + size_t(c)] * w;
                }
                for (float &v : acc) v /= float(ratio);
            }
            std::copy(acc, acc + 4, orow + size_t(i) * outTexelStep);
        }
    }
}

std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

const Quake2Palette &builtinQuake2Palette() {
    static const Quake2Palette palette = [] {
        Quake2Palette p{};
        std::memcpy(p.data(), kQuake2Palette, p.size());
        return p;
    }();
    return palette;
}

bool loadQuake2Palette(const Engine &engine, Quake2Palette &out, std::string *error) {
    out = builtinQuake2Palette();
    std::vector<unsigned char> bytes;
    if (!engine.loadFile("pics/colormap.pcx", bytes)) {
        if (error) *error = "pics/colormap.pcx not found";
        return false;
    }
    // an 8-bit PCX ends with 0x0c and the 768-byte palette
    if (bytes.size() < 128 + 769 || bytes[0] != 0x0a || bytes[bytes.size() - 769] != 0x0c) {
        if (error) *error = "pics/colormap.pcx: no 256-colour palette";
        return false;
    }
    std::memcpy(out.data(), bytes.data() + bytes.size() - 768, out.size());
    return true;
}

bool decodeWal(const unsigned char *data, size_t size, const Quake2Palette &palette, RgbaImage &out,
               std::string &error) {
    out = {};
    // miptex_t: name[32], width, height, offsets[4], animname[32], flags,
    // contents, value
    constexpr size_t kHeader = 100;
    if (!data || size < kHeader) {
        error = "too short for a .wal header";
        return false;
    }
    const uint32_t w = rdU32(data + 32), h = rdU32(data + 36), ofs = rdU32(data + 40);
    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        error = "bad size " + std::to_string(w) + "x" + std::to_string(h);
        return false;
    }
    if (ofs < kHeader || ofs > size || size - ofs < size_t(w) * size_t(h)) {
        error = "pixels outside the file";
        return false;
    }
    out.width = int(w);
    out.height = int(h);
    out.rgba.resize(size_t(w) * size_t(h) * 4);
    const unsigned char *src = data + ofs;
    for (size_t i = 0; i < size_t(w) * size_t(h); i++) {
        const size_t c = size_t(src[i]) * 3;
        out.rgba[i * 4] = palette[c];
        out.rgba[i * 4 + 1] = palette[c + 1];
        out.rgba[i * 4 + 2] = palette[c + 2];
        out.rgba[i * 4 + 3] = 255;
    }
    return true;
}

void resampleTiled(const uint8_t *rgba, int w, int h, int size, uint8_t *out) {
    std::vector<float> src(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < src.size(); i++) src[i] = rgba[i];
    // across (w -> size per row), then down (h -> size per column)
    std::vector<float> wide(size_t(size) * size_t(h) * 4), done(size_t(size) * size_t(size) * 4);
    resampleAxis(src.data(), w, size, h, 4, size_t(w) * 4, wide.data(), 4, size_t(size) * 4);
    resampleAxis(wide.data(), h, size, size, size_t(size) * 4, 4, done.data(), size_t(size) * 4, 4);
    for (size_t i = 0; i < done.size(); i++) out[i] = uint8_t(std::clamp(std::lround(done[i]), 0L, 255L));
}

int MapTextures::find(const std::string &material) const {
    const auto it = index_.find(material);
    return it == index_.end() ? -1 : it->second;
}

void MapTextures::clear() {
    textures_.clear();
    index_.clear();
    pixels_.clear();
    layerSize_ = 0;
    missing_ = 0;
    gamePalette_ = false;
}

size_t MapTextures::load(const Engine &engine, const MapGeometry &geometry, int maxLayerSize, size_t maxBytes) {
    clear();
    Quake2Palette palette;
    gamePalette_ = loadQuake2Palette(engine, palette);

    std::vector<RgbaImage> images;
    std::vector<std::string> tried;
    for (const MapGeometryBatch &b : geometry.batches) {
        if (b.material.empty() || std::find(tried.begin(), tried.end(), b.material) != tried.end()) continue;
        tried.push_back(b.material);
        if (textures_.size() >= kMaxTextures) {
            missing_++;
            continue;
        }
        std::vector<unsigned char> bytes;
        RgbaImage image;
        std::string error;
        const std::string path = "textures/" + b.material + ".wal";
        if ((!engine.loadFile(path, bytes) && !engine.loadFile(lower(path), bytes)) ||
            !decodeWal(bytes.data(), bytes.size(), palette, image, error)) {
            missing_++;
            continue;
        }
        index_.emplace(b.material, int(textures_.size()));
        textures_.push_back({b.material, image.width, image.height});
        images.push_back(std::move(image));
    }
    if (textures_.empty()) return 0;

    int largest = 1;
    for (const Texture &t : textures_) largest = std::max({largest, t.width, t.height});
    int size = 16;
    while (size < largest && size < maxLayerSize) size *= 2;
    while (size > 16 && size_t(size) * size_t(size) * 4 * textures_.size() > maxBytes) size /= 2;
    layerSize_ = size;
    const size_t layerBytes = size_t(size) * size_t(size) * 4;
    pixels_.resize(layerBytes * textures_.size());
    for (size_t i = 0; i < images.size(); i++)
        resampleTiled(images[i].rgba.data(), images[i].width, images[i].height, size, pixels_.data() + i * layerBytes);
    return textures_.size();
}

} // namespace realms
