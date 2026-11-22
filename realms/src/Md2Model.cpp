// Realms Engine - Quake 2 .md2 models and PCX images (see Md2Model.h).
#include "realms/Md2Model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace realms {
namespace {

int32_t rdInt(const unsigned char *p) {
    return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}
int16_t rdShort(const unsigned char *p) { return int16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8); }
uint16_t rdUShort(const unsigned char *p) { return uint16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8); }
float rdFloat(const unsigned char *p) {
    const uint32_t u = uint32_t(rdInt(p));
    float f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}
std::string rdName(const unsigned char *p, size_t n) {
    size_t len = 0;
    while (len < n && p[len]) len++;
    return std::string(reinterpret_cast<const char *>(p), len);
}

constexpr size_t kHeaderSize = 68;
constexpr size_t kSkinNameSize = 64;
constexpr size_t kFrameHeader = 40; // scale[3], translate[3], name[16]

// true when [ofs, ofs + count * stride) lies inside the file
bool inFile(int32_t ofs, int64_t count, int64_t stride, size_t size) {
    if (ofs < 0 || count < 0) return false;
    const int64_t end = int64_t(ofs) + count * stride;
    return end >= ofs && uint64_t(end) <= uint64_t(size);
}

} // namespace

bool Md2Model::parse(const unsigned char *data, size_t size, std::string &error) {
    *this = Md2Model{};
    auto fail = [&](const std::string &why) {
        *this = Md2Model{};
        error = why;
        return false;
    };
    if (!data || size < kHeaderSize) return fail("file too small");
    if (std::memcmp(data, "IDP2", 4) != 0) return fail("not an IDP2 model");
    const int32_t version = rdInt(data + 4);
    if (version != 8) return fail("version " + std::to_string(version) + ", expected 8");
    const int32_t skinW = rdInt(data + 8), skinH = rdInt(data + 12), frameSize = rdInt(data + 16);
    const int32_t numSkins = rdInt(data + 20), numXyz = rdInt(data + 24), numSt = rdInt(data + 28);
    const int32_t numTris = rdInt(data + 32), /* numGlcmds = rdInt(data + 36), */ numFrames = rdInt(data + 40);
    const int32_t ofsSkins = rdInt(data + 44), ofsSt = rdInt(data + 48), ofsTris = rdInt(data + 52);
    const int32_t ofsFrames = rdInt(data + 56);
    const int32_t ofsEnd = rdInt(data + 64);

    if (skinW <= 0 || skinH <= 0 || skinW > 4096 || skinH > 4096) return fail("bad skin size");
    if (numSkins < 0 || numSkins > kMaxSkins) return fail("bad skin count");
    if (numXyz <= 0 || numXyz > kMaxVerts) return fail("bad vertex count");
    if (numSt <= 0 || numSt > 65536) return fail("bad st count");
    if (numTris <= 0 || numTris > kMaxTriangles) return fail("bad triangle count");
    if (numFrames <= 0 || numFrames > kMaxFrames) return fail("bad frame count");
    if (frameSize < int32_t(kFrameHeader) + numXyz * 4) return fail("frame size too small");
    if (ofsEnd < 0 || size_t(ofsEnd) > size) return fail("truncated file");
    if (!inFile(ofsSkins, numSkins, int64_t(kSkinNameSize), size)) return fail("skins out of bounds");
    if (!inFile(ofsSt, numSt, 4, size)) return fail("st out of bounds");
    if (!inFile(ofsTris, numTris, 12, size)) return fail("triangles out of bounds");
    if (!inFile(ofsFrames, numFrames, frameSize, size)) return fail("frames out of bounds");

    skinWidth_ = skinW;
    skinHeight_ = skinH;
    numVerts_ = numXyz;
    for (int i = 0; i < numSkins; i++) skins_.push_back(rdName(data + ofsSkins + size_t(i) * kSkinNameSize, kSkinNameSize));
    st_.resize(size_t(numSt));
    for (int i = 0; i < numSt; i++) {
        const unsigned char *p = data + ofsSt + size_t(i) * 4;
        st_[size_t(i)] = {rdShort(p), rdShort(p + 2)};
    }
    tris_.resize(size_t(numTris));
    for (int i = 0; i < numTris; i++) {
        const unsigned char *p = data + ofsTris + size_t(i) * 12;
        Triangle &t = tris_[size_t(i)];
        for (int k = 0; k < 3; k++) {
            t.xyz[size_t(k)] = rdUShort(p + k * 2);
            t.st[size_t(k)] = rdUShort(p + 6 + k * 2);
            if (t.xyz[size_t(k)] >= numXyz) return fail("triangle " + std::to_string(i) + ": bad vertex index");
            if (t.st[size_t(k)] >= numSt) return fail("triangle " + std::to_string(i) + ": bad st index");
        }
    }
    frames_.resize(size_t(numFrames));
    float windingSign = 0.0f; // > 0: counter-clockwise faces point outwards (decided on frame 0)
    for (int f = 0; f < numFrames; f++) {
        const unsigned char *p = data + ofsFrames + size_t(f) * size_t(frameSize);
        float scale[3], translate[3];
        for (int k = 0; k < 3; k++) {
            scale[k] = rdFloat(p + k * 4);
            translate[k] = rdFloat(p + 12 + k * 4);
            if (!std::isfinite(scale[k]) || !std::isfinite(translate[k]) || std::fabs(scale[k]) > 1e4f ||
                std::fabs(translate[k]) > 1e6f)
                return fail("frame " + std::to_string(f) + ": bad scale / translate");
        }
        Frame &fr = frames_[size_t(f)];
        fr.name = rdName(p + 24, 16);
        fr.positions.resize(size_t(numXyz) * 3);
        const unsigned char *v = p + kFrameHeader;
        for (int i = 0; i < numXyz; i++)
            for (int k = 0; k < 3; k++)
                fr.positions[size_t(i) * 3 + size_t(k)] = float(v[i * 4 + k]) * scale[k] + translate[k];
        fr.mins = {fr.positions[0], fr.positions[1], fr.positions[2]};
        fr.maxs = fr.mins;
        for (int i = 1; i < numXyz; i++)
            for (int k = 0; k < 3; k++) {
                const float c = fr.positions[size_t(i) * 3 + size_t(k)];
                fr.mins[size_t(k)] = std::min(fr.mins[size_t(k)], c);
                fr.maxs[size_t(k)] = std::max(fr.maxs[size_t(k)], c);
            }
        // Area-weighted vertex normals. Which winding faces outwards is
        // decided once, on frame 0: the face normals (b - a) x (c - a) must
        // point away from the model's centre on balance. buildVertices emits
        // the triangles so that counter-clockwise is outwards.
        auto faceNormal = [&](const Triangle &t, float n[3]) {
            const float *a = &fr.positions[size_t(t.xyz[0]) * 3];
            const float *b = &fr.positions[size_t(t.xyz[1]) * 3];
            const float *c = &fr.positions[size_t(t.xyz[2]) * 3];
            const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            n[0] = e1[1] * e2[2] - e1[2] * e2[1];
            n[1] = e1[2] * e2[0] - e1[0] * e2[2];
            n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        };
        if (f == 0) {
            const float centre[3] = {(fr.mins[0] + fr.maxs[0]) * 0.5f, (fr.mins[1] + fr.maxs[1]) * 0.5f,
                                     (fr.mins[2] + fr.maxs[2]) * 0.5f};
            double balance = 0.0;
            for (const Triangle &t : tris_) {
                float n[3];
                faceNormal(t, n);
                for (uint16_t idx : t.xyz)
                    for (int k = 0; k < 3; k++)
                        balance += double(n[k]) * double(fr.positions[size_t(idx) * 3 + size_t(k)] - centre[k]);
            }
            windingSign = balance < 0.0 ? -1.0f : 1.0f;
            flipWinding_ = balance < 0.0;
        }
        fr.normals.assign(size_t(numXyz) * 3, 0.0f);
        for (const Triangle &t : tris_) {
            float n[3];
            faceNormal(t, n);
            for (uint16_t idx : t.xyz)
                for (int k = 0; k < 3; k++) fr.normals[size_t(idx) * 3 + size_t(k)] += n[k] * windingSign;
        }
        for (int i = 0; i < numXyz; i++) {
            float *n = &fr.normals[size_t(i) * 3];
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 1e-8f) {
                n[0] /= len;
                n[1] /= len;
                n[2] /= len;
            } else {
                n[0] = 0;
                n[1] = 0;
                n[2] = 1;
            }
        }
    }
    error.clear();
    return true;
}

int Md2Model::findFrame(const std::string &name) const {
    for (size_t i = 0; i < frames_.size(); i++)
        if (frames_[i].name == name) return int(i);
    return -1;
}

void Md2Model::buildVertices(int frame, int oldframe, float backlerp, std::vector<Md2Vertex> &out) const {
    if (frames_.empty()) return;
    if (frame < 0 || frame >= numFrames()) frame = 0;
    if (oldframe < 0 || oldframe >= numFrames()) oldframe = 0;
    if (!(backlerp >= 0.0f)) backlerp = 0.0f; // also catches NaN
    backlerp = std::min(backlerp, 1.0f);
    const float frontlerp = 1.0f - backlerp;
    const Frame &cur = frames_[size_t(frame)];
    const Frame &old = frames_[size_t(oldframe)];
    const float invW = 1.0f / float(skinWidth_), invH = 1.0f / float(skinHeight_);
    out.reserve(out.size() + tris_.size() * 3);
    static constexpr int kOrder[2][3] = {{0, 1, 2}, {0, 2, 1}};
    const int *order = kOrder[flipWinding_ ? 1 : 0];
    for (const Triangle &t : tris_) {
        for (int corner = 0; corner < 3; corner++) {
            const int k = order[corner];
            const size_t i = size_t(t.xyz[size_t(k)]) * 3;
            Md2Vertex v;
            v.x = old.positions[i] * backlerp + cur.positions[i] * frontlerp;
            v.y = old.positions[i + 1] * backlerp + cur.positions[i + 1] * frontlerp;
            v.z = old.positions[i + 2] * backlerp + cur.positions[i + 2] * frontlerp;
            float nx = old.normals[i] * backlerp + cur.normals[i] * frontlerp;
            float ny = old.normals[i + 1] * backlerp + cur.normals[i + 1] * frontlerp;
            float nz = old.normals[i + 2] * backlerp + cur.normals[i + 2] * frontlerp;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                nx /= len;
                ny /= len;
                nz /= len;
            } else {
                nx = 0;
                ny = 0;
                nz = 1;
            }
            v.nx = nx;
            v.ny = ny;
            v.nz = nz;
            const auto &st = st_[size_t(t.st[size_t(k)])];
            v.u = (float(st[0]) + 0.5f) * invW;
            v.v = (float(st[1]) + 0.5f) * invH;
            out.push_back(v);
        }
    }
}

bool decodePcx(const unsigned char *data, size_t size, RgbaImage &out, std::string &error, int transparentIndex) {
    out = RgbaImage{};
    constexpr size_t kPcxHeader = 128, kPalette = 769;
    if (!data || size < kPcxHeader + kPalette) {
        error = "file too small";
        return false;
    }
    const int manufacturer = data[0], version = data[1], encoding = data[2], bpp = data[3];
    const int xmin = rdUShort(data + 4), ymin = rdUShort(data + 6);
    const int xmax = rdUShort(data + 8), ymax = rdUShort(data + 10);
    const int planes = data[65];
    const int bytesPerLine = rdUShort(data + 66);
    if (manufacturer != 0x0a || version != 5 || encoding != 1 || bpp != 8 || planes != 1) {
        error = "not an 8-bit RLE PCX";
        return false;
    }
    if (xmax < xmin || ymax < ymin || xmax - xmin >= 4096 || ymax - ymin >= 4096) {
        error = "bad image size";
        return false;
    }
    const int w = xmax - xmin + 1, h = ymax - ymin + 1;
    if (bytesPerLine < w) {
        error = "bad bytes per line";
        return false;
    }
    const unsigned char *palette = data + size - 768;
    if (palette[-1] != 0x0c) {
        error = "no 256-colour palette";
        return false;
    }
    std::vector<unsigned char> indices(size_t(w) * size_t(h));
    const unsigned char *p = data + kPcxHeader;
    const unsigned char *end = data + size - kPalette;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < bytesPerLine) {
            if (p >= end) {
                error = "truncated image data";
                return false;
            }
            int b = *p++, run = 1;
            if ((b & 0xc0) == 0xc0) {
                run = b & 0x3f;
                if (p >= end) {
                    error = "truncated image data";
                    return false;
                }
                b = *p++;
            }
            // a run may cross the end of the line; the excess is padding
            for (int r = 0; r < run && x < bytesPerLine; r++, x++)
                if (x < w) indices[size_t(y) * size_t(w) + size_t(x)] = uint8_t(b);
        }
    }
    out.width = w;
    out.height = h;
    out.rgba.resize(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < indices.size(); i++) {
        const unsigned char idx = indices[i];
        out.rgba[i * 4] = palette[idx * 3];
        out.rgba[i * 4 + 1] = palette[idx * 3 + 1];
        out.rgba[i * 4 + 2] = palette[idx * 3 + 2];
        out.rgba[i * 4 + 3] = (transparentIndex >= 0 && idx == transparentIndex) ? 0 : 255;
    }
    error.clear();
    return true;
}

} // namespace realms
