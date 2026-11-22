#include "realms/MapSky.h"

#include "realms/Engine.h"
#include "realms/MapTextures.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace realms {

const char *const MapSky::kSuffixes[MapSky::kFaces] = {"rt", "lf", "bk", "ft", "up", "dn"};

bool decodeTga(const unsigned char *data, size_t size, RgbaImage &out, std::string &error) {
    out = {};
    if (!data || size < 18) {
        error = "too short for a TGA header";
        return false;
    }
    const int idLength = data[0], colorMapType = data[1], type = data[2];
    const int w = data[12] | data[13] << 8, h = data[14] | data[15] << 8, bpp = data[16];
    const bool topDown = (data[17] & 0x20) != 0;
    if ((type != 2 && type != 10) || colorMapType != 0 || (bpp != 24 && bpp != 32)) {
        error = "only 24/32-bit true-colour TGA";
        return false;
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        error = "bad size";
        return false;
    }
    const size_t bytes = size_t(bpp / 8), count = size_t(w) * size_t(h);
    size_t at = 18 + size_t(idLength);
    std::vector<unsigned char> bgra(count * 4, 255);
    auto readPixel = [&](unsigned char *dst) {
        dst[0] = data[at];
        dst[1] = data[at + 1];
        dst[2] = data[at + 2];
        dst[3] = bytes == 4 ? data[at + 3] : 255;
        at += bytes;
    };
    size_t done = 0;
    while (done < count) {
        if (type == 2) {
            if (at + bytes > size) break;
            readPixel(&bgra[done * 4]);
            done++;
            continue;
        }
        if (at >= size) break;
        const int header = data[at++];
        const size_t run = std::min(size_t(header & 0x7f) + 1, count - done);
        if (header & 0x80) { // one pixel repeated
            if (at + bytes > size) break;
            readPixel(&bgra[done * 4]);
            for (size_t k = 1; k < run; k++) std::copy(&bgra[done * 4], &bgra[done * 4] + 4, &bgra[(done + k) * 4]);
        } else {
            if (at + bytes * run > size) break;
            for (size_t k = 0; k < run; k++) readPixel(&bgra[(done + k) * 4]);
        }
        done += run;
    }
    if (done < count) {
        error = "pixels cut short";
        return false;
    }
    out.width = w;
    out.height = h;
    out.rgba.resize(count * 4);
    for (int y = 0; y < h; y++) {
        const int src = topDown ? y : h - 1 - y; // most TGAs start at the bottom row
        for (int x = 0; x < w; x++) {
            const unsigned char *p = &bgra[(size_t(src) * size_t(w) + size_t(x)) * 4];
            unsigned char *q = &out.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = p[3];
        }
    }
    return true;
}

MapSkySettings skySettings(const std::string &entities) {
    MapSkySettings s;
    for (const MapEntity &e : Engine::parseEntities(entities)) {
        if (Engine::entityValue(e, "classname") != "worldspawn") continue;
        const std::string name = Engine::entityValue(e, "sky");
        if (!name.empty()) s.name = name;
        s.rotate = std::strtof(Engine::entityValue(e, "skyrotate").c_str(), nullptr);
        float a[3] = {0, 0, 0};
        if (std::sscanf(Engine::entityValue(e, "skyaxis").c_str(), "%f %f %f", &a[0], &a[1], &a[2]) == 3)
            s.axis = {a[0], a[1], a[2]};
        break;
    }
    return s;
}

void MapSky::clear() {
    settings_ = {};
    pixels_.clear();
    size_ = 0;
}

bool MapSky::load(const Engine &engine, const MapSkySettings &settings, std::string *error, int maxSize) {
    clear();
    std::vector<RgbaImage> faces(kFaces);
    for (int i = 0; i < kFaces; i++) {
        const std::string base = "env/" + settings.name + kSuffixes[i];
        std::vector<unsigned char> bytes;
        std::string why;
        // ref_gl reads the .tga; the software renderer and gl_skymip the .pcx
        bool ok = engine.loadFile(base + ".tga", bytes) && decodeTga(bytes.data(), bytes.size(), faces[size_t(i)], why);
        if (!ok) ok = engine.loadFile(base + ".pcx", bytes) && decodePcx(bytes, faces[size_t(i)], why);
        if (!ok) {
            if (error) *error = base + (why.empty() ? ": not found" : ": " + why);
            return false;
        }
    }
    int largest = 1;
    for (const RgbaImage &f : faces) largest = std::max({largest, f.width, f.height});
    int size = 16;
    while (size < largest && size < maxSize) size *= 2;
    const size_t layer = size_t(size) * size_t(size) * 4;
    pixels_.resize(layer * kFaces);
    for (int i = 0; i < kFaces; i++) {
        const RgbaImage &f = faces[size_t(i)];
        resampleTiled(f.rgba.data(), f.width, f.height, size, pixels_.data() + size_t(i) * layer);
    }
    settings_ = settings;
    size_ = size;
    return true;
}

std::array<float, 9> MapSky::renderToSky(double seconds) const {
    // render (x, y, z) -> Quake (x, -z, y)
    const float swap[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0}; // column-major
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const auto &a = settings_.axis;
    const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (settings_.rotate != 0.0f && len > 0.0f) {
        // glRotatef(time * skyrotate, axis) turns the box; a world direction
        // is in the box at the inverse rotation (the transpose)
        const double angle = std::fmod(seconds * double(settings_.rotate), 360.0) * 3.14159265358979323846 / 180.0;
        const float x = a[0] / len, y = a[1] / len, z = a[2] / len;
        const float c = float(std::cos(angle)), s = float(std::sin(angle)), t = 1.0f - c;
        const float r[3][3] = {{t * x * x + c, t * x * y - s * z, t * x * z + s * y},
                               {t * x * y + s * z, t * y * y + c, t * y * z - s * x},
                               {t * x * z - s * y, t * y * z + s * x, t * z * z + c}}; // r[row][col]
        for (int col = 0; col < 3; col++)
            for (int row = 0; row < 3; row++) rot[col * 3 + row] = r[col][row]; // transposed
    }
    std::array<float, 9> m{};
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) {
            float v = 0.0f;
            for (int k = 0; k < 3; k++) v += rot[k * 3 + row] * swap[col * 3 + k];
            m[size_t(col * 3 + row)] = v;
        }
    return m;
}

void MapSky::faceCoords(const float d[3], int &layer, float &u, float &v) {
    const float ax = std::fabs(d[0]), ay = std::fabs(d[1]), az = std::fabs(d[2]);
    float s, t;
    if (ax >= ay && ax >= az) {
        layer = d[0] > 0.0f ? 0 : 1;
        s = (d[0] > 0.0f ? -d[1] : d[1]) / ax;
        t = d[2] / ax;
    } else if (ay >= az) {
        layer = d[1] > 0.0f ? 2 : 3;
        s = (d[1] > 0.0f ? d[0] : -d[0]) / ay;
        t = d[2] / ay;
    } else {
        layer = d[2] > 0.0f ? 4 : 5;
        s = -d[1] / az;
        t = (d[2] > 0.0f ? -d[0] : d[0]) / az;
    }
    u = (s + 1.0f) * 0.5f;
    v = 1.0f - (t + 1.0f) * 0.5f;
}

} // namespace realms
