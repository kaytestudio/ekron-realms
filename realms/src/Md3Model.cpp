// Realms Engine - Quake 3 .md3 models (see Md3Model.h).
#include "realms/Md3Model.h"

#include <algorithm>
#include <cctype>
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

// md3Header_t, md3Frame_t, md3Tag_t, md3Surface_t, md3Shader_t, md3St_t,
// md3XyzNormal_t
constexpr size_t kHeaderSize = 108;
constexpr size_t kFrameSize = 56;
constexpr size_t kTagSize = 112;
constexpr size_t kSurfaceHeader = 108;
constexpr size_t kShaderSize = 68;
constexpr size_t kTriangleSize = 12;
constexpr size_t kStSize = 8;
constexpr size_t kXyzNormalSize = 8;
constexpr size_t kNameSize = 64;

// true when [ofs, ofs + count * stride) lies inside [0, size)
bool inRange(int64_t ofs, int64_t count, int64_t stride, int64_t size) {
    if (ofs < 0 || count < 0) return false;
    const int64_t end = ofs + count * stride;
    return end >= ofs && end <= size;
}

bool finite3(const float *v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// The latitude / longitude bytes of md3XyzNormal_t, as the model tools
// encode them (lat = atan2(y, x), lng = acos(z), both * 255 / 2pi).
void decodeNormal(int16_t packed, float *n) {
    constexpr float kStep = 2.0f * 3.14159265358979323846f / 255.0f;
    const float lat = float((uint16_t(packed) >> 8) & 0xff) * kStep;
    const float lng = float(uint16_t(packed) & 0xff) * kStep;
    n[0] = std::cos(lat) * std::sin(lng);
    n[1] = std::sin(lat) * std::sin(lng);
    n[2] = std::cos(lng);
}

void normalise(float &x, float &y, float &z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len > 1e-8f) {
        x /= len;
        y /= len;
        z /= len;
    } else {
        x = 0;
        y = 0;
        z = 1;
    }
}

std::string lowerExt(const std::string &name) {
    if (name.size() < 4) return {};
    std::string ext = name.substr(name.size() - 4);
    for (char &c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

} // namespace

bool Md3Model::parse(const unsigned char *data, size_t size, std::string &error) {
    *this = Md3Model{};
    auto fail = [&](const std::string &why) {
        *this = Md3Model{};
        error = why;
        return false;
    };
    if (!data || size < kHeaderSize) return fail("file too small");
    if (std::memcmp(data, "IDP3", 4) != 0) return fail("not an IDP3 model");
    const int32_t version = rdInt(data + 4);
    if (version != kVersion) return fail("version " + std::to_string(version) + ", expected 15");
    const int32_t numFrames = rdInt(data + 76), numTags = rdInt(data + 80), numSurfaces = rdInt(data + 84);
    const int32_t ofsFrames = rdInt(data + 92), ofsTags = rdInt(data + 96), ofsSurfaces = rdInt(data + 100);
    const int32_t ofsEnd = rdInt(data + 104);
    if (numFrames <= 0 || numFrames > kMaxFrames) return fail("bad frame count");
    if (numTags < 0 || numTags > kMaxTags) return fail("bad tag count");
    if (numSurfaces <= 0 || numSurfaces > kMaxSurfaces) return fail("bad surface count");
    if (ofsEnd < int32_t(kHeaderSize) || size_t(ofsEnd) > size) return fail("truncated file");
    const int64_t end = ofsEnd;
    if (!inRange(ofsFrames, numFrames, kFrameSize, end)) return fail("frames out of bounds");
    if (!inRange(ofsTags, int64_t(numFrames) * numTags, kTagSize, end)) return fail("tags out of bounds");
    if (!inRange(ofsSurfaces, 1, kSurfaceHeader, end)) return fail("surfaces out of bounds");

    name_ = rdName(data + 8, kNameSize);
    numTags_ = numTags;
    frames_.resize(size_t(numFrames));
    for (int f = 0; f < numFrames; f++) {
        const unsigned char *p = data + ofsFrames + size_t(f) * kFrameSize;
        float v[10];
        for (int k = 0; k < 10; k++) v[k] = rdFloat(p + k * 4);
        if (!finite3(v) || !finite3(v + 3) || !finite3(v + 6) || !std::isfinite(v[9]))
            return fail("frame " + std::to_string(f) + ": bad bounds");
        Frame &fr = frames_[size_t(f)];
        fr.mins = {v[0], v[1], v[2]};
        fr.maxs = {v[3], v[4], v[5]};
        fr.localOrigin = {v[6], v[7], v[8]};
        fr.radius = v[9];
        fr.name = rdName(p + 40, 16);
    }
    tags_.resize(size_t(numFrames) * size_t(numTags));
    for (size_t i = 0; i < tags_.size(); i++) {
        const unsigned char *p = data + ofsTags + i * kTagSize;
        Tag &t = tags_[i];
        t.name = rdName(p, kNameSize);
        float v[12];
        for (int k = 0; k < 12; k++) v[k] = rdFloat(p + kNameSize + size_t(k) * 4);
        for (int k = 0; k < 12; k += 3)
            if (!finite3(v + k)) return fail("tag " + t.name + ": bad origin / axis");
        t.origin = {v[0], v[1], v[2]};
        for (int a = 0; a < 3; a++) t.axis[size_t(a)] = {v[3 + a * 3], v[4 + a * 3], v[5 + a * 3]};
    }

    int64_t at = ofsSurfaces;
    surfaces_.resize(size_t(numSurfaces));
    for (int s = 0; s < numSurfaces; s++) {
        const std::string which = "surface " + std::to_string(s);
        if (!inRange(at, 1, kSurfaceHeader, end)) return fail(which + ": out of bounds");
        const unsigned char *p = data + at;
        if (std::memcmp(p, "IDP3", 4) != 0) return fail(which + ": bad ident");
        Surface &surf = surfaces_[size_t(s)];
        surf.name = rdName(p + 4, kNameSize);
        const int32_t sFrames = rdInt(p + 72), numShaders = rdInt(p + 76), numVerts = rdInt(p + 80);
        const int32_t numTris = rdInt(p + 84), ofsTris = rdInt(p + 88), ofsShaders = rdInt(p + 92);
        const int32_t ofsSt = rdInt(p + 96), ofsXyz = rdInt(p + 100), sEnd = rdInt(p + 104);
        if (sFrames != numFrames) return fail(which + ": frame count differs from the model's");
        if (numShaders < 0 || numShaders > kMaxShaders) return fail(which + ": bad shader count");
        if (numVerts <= 0 || numVerts > kMaxVerts) return fail(which + ": bad vertex count");
        if (numTris <= 0 || numTris > kMaxTriangles) return fail(which + ": bad triangle count");
        if (sEnd < int32_t(kSurfaceHeader) || !inRange(at, 1, sEnd, end)) return fail(which + ": truncated");
        const int64_t span = sEnd;
        if (!inRange(ofsShaders, numShaders, kShaderSize, span)) return fail(which + ": shaders out of bounds");
        if (!inRange(ofsTris, numTris, kTriangleSize, span)) return fail(which + ": triangles out of bounds");
        if (!inRange(ofsSt, numVerts, kStSize, span)) return fail(which + ": st out of bounds");
        if (!inRange(ofsXyz, int64_t(numFrames) * numVerts, kXyzNormalSize, span))
            return fail(which + ": vertices out of bounds");

        surf.numVerts = numVerts;
        for (int i = 0; i < numShaders; i++) surf.shaders.push_back(rdName(p + ofsShaders + size_t(i) * kShaderSize, kNameSize));
        surf.triangles.resize(size_t(numTris));
        for (int i = 0; i < numTris; i++) {
            const unsigned char *t = p + ofsTris + size_t(i) * kTriangleSize;
            for (int k = 0; k < 3; k++) {
                const int32_t idx = rdInt(t + k * 4);
                if (idx < 0 || idx >= numVerts)
                    return fail(which + ": triangle " + std::to_string(i) + ": bad vertex index");
                surf.triangles[size_t(i)][size_t(k)] = uint32_t(idx);
            }
        }
        surf.st.resize(size_t(numVerts));
        for (int i = 0; i < numVerts; i++) {
            const float u = rdFloat(p + ofsSt + size_t(i) * kStSize), v = rdFloat(p + ofsSt + size_t(i) * kStSize + 4);
            if (!std::isfinite(u) || !std::isfinite(v) || std::fabs(u) > 1e4f || std::fabs(v) > 1e4f)
                return fail(which + ": bad texture coordinates");
            surf.st[size_t(i)] = {u, v};
        }
        const size_t count = size_t(numFrames) * size_t(numVerts);
        surf.positions.resize(count * 3);
        surf.normals.resize(count * 3);
        for (size_t i = 0; i < count; i++) {
            const unsigned char *x = p + ofsXyz + i * kXyzNormalSize;
            for (int k = 0; k < 3; k++) surf.positions[i * 3 + size_t(k)] = float(rdShort(x + k * 2)) * kXyzScale;
            decodeNormal(rdShort(x + 6), &surf.normals[i * 3]);
        }
        at += sEnd;
    }

    // Winding: on frame 0, the face normals (b - a) x (c - a) must agree with
    // the stored vertex normals on balance; if not, every triangle is emitted
    // the other way round.
    double balance = 0.0;
    for (const Surface &surf : surfaces_) {
        for (const auto &t : surf.triangles) {
            const float *a = &surf.positions[size_t(t[0]) * 3];
            const float *b = &surf.positions[size_t(t[1]) * 3];
            const float *c = &surf.positions[size_t(t[2]) * 3];
            const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            for (uint32_t idx : t)
                for (int k = 0; k < 3; k++) balance += double(n[k]) * double(surf.normals[size_t(idx) * 3 + size_t(k)]);
        }
    }
    flipWinding_ = balance < 0.0;
    error.clear();
    return true;
}

int Md3Model::numTriangles() const {
    int n = 0;
    for (const Surface &s : surfaces_) n += int(s.triangles.size());
    return n;
}

int Md3Model::findFrame(const std::string &name) const {
    for (size_t i = 0; i < frames_.size(); i++)
        if (frames_[i].name == name) return int(i);
    return -1;
}

const Md3Model::Tag *Md3Model::tag(int frame, const std::string &name) const {
    if (frames_.empty() || numTags_ == 0) return nullptr;
    if (frame < 0 || frame >= numFrames()) frame = 0;
    for (int i = 0; i < numTags_; i++) {
        const Tag &t = tags_[size_t(frame) * size_t(numTags_) + size_t(i)];
        if (t.name == name) return &t;
    }
    return nullptr;
}

const std::string &Md3Model::skin(int surface, int skinnum) const {
    static const std::string kNone;
    if (surface < 0 || surface >= numSurfaces()) return kNone;
    const auto &shaders = surfaces_[size_t(surface)].shaders;
    if (shaders.empty()) return kNone;
    return shaders[size_t(skinnum >= 0 && size_t(skinnum) < shaders.size() ? skinnum : 0)];
}

void Md3Model::buildVertices(int surface, int frame, int oldframe, float backlerp, std::vector<ModelVertex> &out) const {
    if (frames_.empty() || surface < 0 || surface >= numSurfaces()) return;
    if (frame < 0 || frame >= numFrames()) frame = 0;
    if (oldframe < 0 || oldframe >= numFrames()) oldframe = 0;
    if (!(backlerp >= 0.0f)) backlerp = 0.0f; // also catches NaN
    backlerp = std::min(backlerp, 1.0f);
    const float frontlerp = 1.0f - backlerp;
    const Surface &s = surfaces_[size_t(surface)];
    const size_t curBase = size_t(frame) * size_t(s.numVerts) * 3;
    const size_t oldBase = size_t(oldframe) * size_t(s.numVerts) * 3;
    out.reserve(out.size() + s.triangles.size() * 3);
    static constexpr int kOrder[2][3] = {{0, 1, 2}, {0, 2, 1}};
    const int *order = kOrder[flipWinding_ ? 1 : 0];
    for (const auto &t : s.triangles) {
        for (int corner = 0; corner < 3; corner++) {
            const uint32_t idx = t[size_t(order[corner])];
            const size_t c = curBase + size_t(idx) * 3, o = oldBase + size_t(idx) * 3;
            ModelVertex v;
            v.x = s.positions[o] * backlerp + s.positions[c] * frontlerp;
            v.y = s.positions[o + 1] * backlerp + s.positions[c + 1] * frontlerp;
            v.z = s.positions[o + 2] * backlerp + s.positions[c + 2] * frontlerp;
            v.nx = s.normals[o] * backlerp + s.normals[c] * frontlerp;
            v.ny = s.normals[o + 1] * backlerp + s.normals[c + 1] * frontlerp;
            v.nz = s.normals[o + 2] * backlerp + s.normals[c + 2] * frontlerp;
            normalise(v.nx, v.ny, v.nz);
            v.u = s.st[idx][0];
            v.v = s.st[idx][1];
            out.push_back(v);
        }
    }
}

void Md3Model::buildVertices(int frame, int oldframe, float backlerp, std::vector<ModelVertex> &out) const {
    for (int s = 0; s < numSurfaces(); s++) buildVertices(s, frame, oldframe, backlerp, out);
}

std::string md3PathFor(const std::string &modelName) {
    if (lowerExt(modelName) != ".md2") return modelName;
    return modelName.substr(0, modelName.size() - 1) + "3";
}

bool isAliasModelName(const std::string &name) {
    const std::string ext = lowerExt(name);
    return ext == ".md2" || ext == ".md3";
}

} // namespace realms
