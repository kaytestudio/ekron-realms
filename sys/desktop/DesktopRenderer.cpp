#include "DesktopRenderer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>

namespace ekron::desktop {

namespace {

constexpr int kMonitorW = 480;
constexpr int kMonitorH = 270;
constexpr int kMaterialLayers = 8;

bool readBinaryFile(const std::string &path, std::vector<unsigned char> &out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0, std::ios::beg);
    return static_cast<bool>(f.read(reinterpret_cast<char *>(out.data()), sz));
}

GLint glWrapMode(int gltfWrap) {
    if (gltfWrap == 33071) return static_cast<GLint>(GL_CLAMP_TO_EDGE);
    if (gltfWrap == 33648) return static_cast<GLint>(GL_MIRRORED_REPEAT);
    return static_cast<GLint>(GL_REPEAT);
}

const char *kSkyVert = R"GLSL(#version 330 core
out vec2 vScreen;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vScreen = p * 2.0 - 1.0;
    gl_Position = vec4(vScreen, 0.9999, 1.0);
}
)GLSL";

const char *kSkyFrag = R"GLSL(#version 330 core
in vec2 vScreen;
uniform vec2 uAngles;
uniform float uRoll;
uniform float uAspect;
out vec4 fragColor;
void main() {
    vec3 ray = normalize(vec3(vScreen.x * max(uAspect, 0.001) / 1.37638, vScreen.y / 1.37638, -1.0));
    float cr = cos(uRoll), sr = sin(uRoll);
    ray.xy = vec2(cr * ray.x + sr * ray.y, -sr * ray.x + cr * ray.y);
    float altitude = cos(uAngles.y) * ray.y - sin(uAngles.y) * ray.z;
    float blend = smoothstep(0.0, 0.85, max(altitude, 0.0));
    fragColor = vec4(mix(vec3(0.56, 0.61, 0.63), vec3(0.32, 0.41, 0.51), blend), 1.0);
}
)GLSL";

const char *kWorldVert = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aCol;
layout(location = 2) in vec3 aNorm;
layout(location = 3) in vec2 aUV;
layout(location = 4) in float aLayer;

uniform vec3 uEye;
uniform vec2 uAngles;
uniform float uRoll;
uniform float uAspect;
uniform mat4 uObjectTransform;

out vec3 vCol;
out vec3 vNorm;
out vec2 vUV;
flat out int vLayer;
out vec3 vToEye;

void main() {
    vec3 worldPos = (uObjectTransform * vec4(aPos, 1.0)).xyz;
    vec3 p = worldPos - uEye;
    vToEye = p;
    float cy = cos(uAngles.x), sy = sin(uAngles.x);
    float cp = cos(uAngles.y), sp = sin(uAngles.y);
    vec3 q = vec3(cy * p.x - sy * p.z, p.y, sy * p.x + cy * p.z);
    q = vec3(q.x, cp * q.y + sp * q.z, -sp * q.y + cp * q.z);
    float cr = cos(uRoll), sr = sin(uRoll);
    q.xy = vec2(cr * q.x - sr * q.y, sr * q.x + cr * q.y);
    float f = 1.37638;
    gl_Position = vec4(q.x * f / max(uAspect, 0.001), q.y * f, -1.000533 * q.z - 0.160043, -q.z);
    vCol = aCol;
    vNorm = mat3(uObjectTransform) * aNorm;
    vUV = aUV;
    vLayer = int(aLayer + 0.5);
}
)GLSL";

const char *kModelVert = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec2 aUV;

uniform vec3 uEye;
uniform vec2 uAngles;
uniform float uRoll;
uniform float uAspect;
uniform mat4 uObjectTransform;

out vec3 vCol;
out vec3 vNorm;
out vec2 vUV;
flat out int vLayer;
out vec3 vToEye;

void main() {
    vec3 worldPos = (uObjectTransform * vec4(aPos, 1.0)).xyz;
    vec3 p = worldPos - uEye;
    vToEye = p;
    float cy = cos(uAngles.x), sy = sin(uAngles.x);
    float cp = cos(uAngles.y), sp = sin(uAngles.y);
    vec3 q = vec3(cy * p.x - sy * p.z, p.y, sy * p.x + cy * p.z);
    q = vec3(q.x, cp * q.y + sp * q.z, -sp * q.y + cp * q.z);
    float cr = cos(uRoll), sr = sin(uRoll);
    q.xy = vec2(cr * q.x - sr * q.y, sr * q.x + cr * q.y);
    float f = 1.37638;
    gl_Position = vec4(q.x * f / max(uAspect, 0.001), q.y * f, -1.000533 * q.z - 0.160043, -q.z);
    vCol = vec3(1.0);
    vNorm = mat3(uObjectTransform) * aNorm;
    vUV = aUV;
    vLayer = 4;
}
)GLSL";

const char *kWorldFrag = R"GLSL(#version 330 core
in vec3 vCol;
in vec3 vNorm;
in vec2 vUV;
flat in int vLayer;
in vec3 vToEye;

uniform vec3 uEye;
uniform sampler2DArray uAlbedoArray;
uniform int uTexturesReady;
uniform int uModelPass;
uniform int uPreviewPass;
uniform sampler2D uModelAlbedo;
uniform int uModelTextured;
uniform vec4 uBaseColorFactor;
uniform float uModelRoughness;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;

out vec4 fragColor;

void main() {
    vec3 base = pow(max(vCol, vec3(0.0)), vec3(2.2));
    if (uModelPass == 0 && uTexturesReady != 0) {
        base *= texture(uAlbedoArray, vec3(vUV, clamp(vLayer, 0, 7))).rgb;
    }
    if (uModelPass == 0 && vLayer != 4) {
        vec3 world = vToEye + uEye;
        base *= 0.94 + 0.035 * sin(world.x * 0.21 + world.z * 0.17)
                     + 0.025 * sin(world.y * 0.33 - world.x * 0.13);
    }
    if (uModelPass == 0 && vLayer == 7) {
        float pattern = mod(floor(gl_FragCoord.x) + floor(gl_FragCoord.y) * 2.0, 4.0);
        if (pattern > 0.0) discard;
    }

    float alpha = 1.0;
    if (uModelPass != 0) {
        vec4 s = uModelTextured != 0 ? texture(uModelAlbedo, vUV) : vec4(1.0);
        base = uBaseColorFactor.rgb * s.rgb;
        alpha = uBaseColorFactor.a * s.a;
        if (uAlphaMode == 1 && alpha < uAlphaCutoff) discard;
        if (uAlphaMode != 2) alpha = 1.0;
    }
    if (uPreviewPass != 0) {
        base = mix(base, base * vec3(0.35, 1.25, 0.55) + vec3(0.01, 0.035, 0.015), 0.32);
        alpha *= 0.62;
    }

    vec3 n = dot(vNorm, vNorm) > 1e-12 ? normalize(vNorm) : vec3(0.0, 1.0, 0.0);
    if (uModelPass != 0 && !gl_FrontFacing) n = -n;
    vec3 lightDir = normalize(-uLightDirection);
    float diffuse = max(dot(n, lightDir), 0.0);
    float sky = 0.78 + 0.22 * max(n.y, 0.0);
    vec3 lit = base * (uAmbientColor * sky + uLightColor * diffuse);

    float roughness = uModelPass != 0 ? uModelRoughness : (vLayer == 7 ? 0.18 : (vLayer == 3 ? 0.48 : 0.94));
    float strength = uModelPass != 0 ? 0.035 : (vLayer == 7 ? 0.28 : (vLayer == 3 ? 0.12 : 0.006));
    vec3 viewDir = dot(vToEye, vToEye) > 1e-12 ? -normalize(vToEye) : vec3(0.0, 0.0, 1.0);
    vec3 halfVector = normalize(lightDir + viewDir);
    float shine = pow(max(dot(n, halfVector), 0.0), mix(90.0, 8.0, clamp(roughness, 0.0, 1.0)));
    lit += uLightColor * (shine * strength * diffuse);

    float d = max(length(vToEye) - 22.0, 0.0);
    float fog = 1.0 - exp(-d * d * 0.00022);
    vec3 fogColor = pow(vec3(0.56, 0.61, 0.63), vec3(2.2));
    fragColor = vec4(pow(max(mix(lit, fogColor, fog), vec3(0.0)), vec3(1.0 / 2.2)), alpha);
}
)GLSL";

const char *kPipVert = R"GLSL(#version 330 core
uniform vec4 uRectNdc;
out vec2 vUV;
void main() {
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0),  vec2(-1.0, 1.0)
    );
    vec2 c = corners[gl_VertexID % 6];
    gl_Position = vec4(uRectNdc.xy + c * uRectNdc.zw, 0.0, 1.0);
    vUV = c * 0.5 + 0.5;
}
)GLSL";

const char *kPipFrag = R"GLSL(#version 330 core
in vec2 vUV;
uniform vec2 uBorder;
uniform sampler2D uFeed;
out vec4 fragColor;
void main() {
    if (vUV.x < uBorder.x || vUV.x > (1.0 - uBorder.x) ||
        vUV.y < uBorder.y || vUV.y > (1.0 - uBorder.y)) {
        fragColor = vec4(0.12, 0.65, 0.58, 1.0);
        return;
    }
    vec2 inner = (vUV - uBorder) / max(vec2(1.0) - 2.0 * uBorder, vec2(0.001));
    fragColor = vec4(texture(uFeed, inner).rgb, 1.0);
}
)GLSL";

const char *kUiVert = R"GLSL(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aMode;

uniform vec2 uScreenSize;

out vec2 vUV;
out vec4 vColor;
flat out int vMode;

void main() {
    vec2 ndc = vec2(
        (aPos.x / max(uScreenSize.x, 1.0)) * 2.0 - 1.0,
        1.0 - (aPos.y / max(uScreenSize.y, 1.0)) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
    vMode = int(aMode + 0.5);
}
)GLSL";

const char *kUiFrag = R"GLSL(#version 330 core
in vec2 vUV;
in vec4 vColor;
flat in int vMode;

uniform sampler2D uFontTex;
uniform sampler2D uIconTex;

out vec4 fragColor;

void main() {
    if (vMode == 1) {
        float a = texture(uFontTex, vUV).a;
        if (a < 0.1) discard;
        fragColor = vec4(vColor.rgb, vColor.a * a);
    } else if (vMode == 2) {
        vec4 t = texture(uIconTex, vUV);
        fragColor = t * vColor;
    } else {
        fragColor = vColor;
    }
}
)GLSL";

void applyCommonSceneUniforms(ekron::gl::Api &gl,
                              GLuint prog,
                              physics::Vec3 eye,
                              float yaw,
                              float pitch,
                              float roll,
                              float aspect,
                              bool texturesReady) {
    gl.Uniform3f(gl.GetUniformLocation(prog, "uEye"), eye.x, eye.y, eye.z);
    gl.Uniform2f(gl.GetUniformLocation(prog, "uAngles"), yaw, pitch);
    gl.Uniform1f(gl.GetUniformLocation(prog, "uRoll"), roll);
    gl.Uniform1f(gl.GetUniformLocation(prog, "uAspect"), aspect);
    gl.Uniform1i(gl.GetUniformLocation(prog, "uAlbedoArray"), 0);
    gl.Uniform1i(gl.GetUniformLocation(prog, "uModelAlbedo"), 1);
    gl.Uniform1i(gl.GetUniformLocation(prog, "uTexturesReady"), texturesReady ? 1 : 0);
    gl.Uniform3f(gl.GetUniformLocation(prog, "uLightDirection"), -0.45f, -1.0f, -0.25f);
    gl.Uniform3f(gl.GetUniformLocation(prog, "uLightColor"), 0.55f, 0.52f, 0.48f);
    gl.Uniform3f(gl.GetUniformLocation(prog, "uAmbientColor"), 0.55f, 0.59f, 0.65f);
}

const std::array<float, 16> kIdentityMatrix = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

} // namespace

bool DesktopGltfModel::loadFromDisk(const std::string &assetRoot,
                                    const std::string &relativePath,
                                    std::string &logOut,
                                    city::LoadOptions options) {
    const std::string fullPath = assetRoot + "/" + relativePath;
    auto reader = [](const std::string &p, std::vector<unsigned char> &out) {
        return readBinaryFile(p, out);
    };
    loaded_ = scene_.load(fullPath, reader, logOut, options) && !scene_.batches.empty();
    return loaded_;
}

void DesktopGltfModel::upload(ekron::gl::Api &gl) {
    if (!loaded_ || uploaded_) return;

    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    textures_.assign(scene_.materials.size(), 0);
    for (size_t i = 0; i < scene_.materials.size(); ++i) {
        const auto &mat = scene_.materials[i];
        if (mat.image < 0 || size_t(mat.image) >= scene_.images.size()) continue;
        const auto &img = scene_.images[size_t(mat.image)];
        if (img.width <= 0 || img.height <= 0 || img.rgba.empty()) continue;

        GLuint tex = 0;
        gl.GenTextures(1, &tex);
        gl.BindTexture(GL_TEXTURE_2D, tex);
        gl.TexImage2D(GL_TEXTURE_2D,
                      0,
                      static_cast<GLint>(GL_SRGB8_ALPHA8),
                      img.width,
                      img.height,
                      0,
                      GL_RGBA,
                      GL_UNSIGNED_BYTE,
                      img.rgba.data());
        gl.GenerateMipmap(GL_TEXTURE_2D);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrapMode(mat.wrapS));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrapMode(mat.wrapT));
        textures_[i] = tex;
    }

    for (const auto &batch : scene_.batches) {
        if (batch.vertices.empty()) continue;
        UploadedGlMesh mesh;
        mesh.count = static_cast<GLsizei>(batch.vertices.size());
        mesh.material = batch.material;
        const float inv = 1.0f / float(batch.vertices.size());
        for (const auto &v : batch.vertices) {
            mesh.x += v.x * inv;
            mesh.z += v.z * inv;
        }
        gl.GenVertexArrays(1, &mesh.vao);
        gl.GenBuffers(1, &mesh.vbo);
        gl.BindVertexArray(mesh.vao);
        gl.BindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        gl.BufferData(GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(batch.vertices.size() * sizeof(city::Vertex)),
                      batch.vertices.data(),
                      GL_STATIC_DRAW);
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(city::Vertex), reinterpret_cast<const void *>(0));
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(city::Vertex), reinterpret_cast<const void *>(3 * sizeof(float)));
        gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(city::Vertex), reinterpret_cast<const void *>(6 * sizeof(float)));
        meshes_.push_back(mesh);
    }
    gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);

    for (auto &img : scene_.images) {
        std::vector<unsigned char>().swap(img.rgba);
    }
    for (auto &b : scene_.batches) {
        std::vector<city::Vertex>().swap(b.vertices);
    }
    uploaded_ = true;
}

void DesktopGltfModel::release(ekron::gl::Api &gl) {
    for (auto &m : meshes_) {
        if (m.vbo) gl.DeleteBuffers(1, &m.vbo);
        if (m.vao) gl.DeleteVertexArrays(1, &m.vao);
    }
    meshes_.clear();
    for (GLuint t : textures_) {
        if (t) gl.DeleteTextures(1, &t);
    }
    textures_.clear();
    uploaded_ = false;
}

GLuint DesktopRenderer::compileProgram(ekron::gl::Api &gl, const char *vsSrc, const char *fsSrc, std::string &errOut) {
    auto compileStage = [&](GLenum type, const char *src) -> GLuint {
        GLuint sh = gl.CreateShader(type);
        gl.ShaderSource(sh, 1, &src, nullptr);
        gl.CompileShader(sh);
        GLint ok = 0;
        gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024]{};
            gl.GetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
            errOut += log;
            gl.DeleteShader(sh);
            return 0;
        }
        return sh;
    };

    GLuint vs = compileStage(GL_VERTEX_SHADER, vsSrc);
    if (!vs) return 0;
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsSrc);
    if (!fs) {
        gl.DeleteShader(vs);
        return 0;
    }

    GLuint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    GLint linked = 0;
    gl.GetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024]{};
        gl.GetProgramInfoLog(prog, sizeof(log) - 1, nullptr, log);
        errOut += log;
        gl.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool DesktopRenderer::loadMaterialArray(ekron::gl::Api &gl, const std::string &assetRoot) {
    constexpr int kTexSize = 512;
    auto fallbackLayer = [&](int layer) {
        constexpr unsigned char colors[8][3] = {
            {48, 50, 53}, {137, 135, 129}, {136, 72, 47}, {124, 68, 35},
            {255, 255, 255}, {135, 91, 49}, {43, 56, 48}, {142, 177, 182}
        };
        std::vector<unsigned char> out(size_t(kTexSize * kTexSize * 4));
        for (int y = 0; y < kTexSize; ++y) {
            for (int x = 0; x < kTexSize; ++x) {
                const int shade = layer == 4 ? 0 : ((x / 32 + y / 32) % 2) * 7;
                const size_t p = (size_t(y) * kTexSize + size_t(x)) * 4;
                for (int c = 0; c < 3; ++c) {
                    out[p + size_t(c)] = static_cast<unsigned char>(colors[layer][c] + shade);
                }
                out[p + 3] = 255;
            }
        }
        return out;
    };

    auto decodeResampled = [&](const char *relPath, int layer) {
        std::vector<unsigned char> png;
        int w = 0, h = 0;
        std::vector<unsigned char> raw;
        if (!readBinaryFile(assetRoot + "/" + relPath, png) ||
            !decodeImageBytes(png.data(), png.size(), w, h, raw) || w <= 0 || h <= 0) {
            return fallbackLayer(layer);
        }
        std::vector<unsigned char> out(size_t(kTexSize * kTexSize * 4));
        for (int y = 0; y < kTexSize; ++y) {
            for (int x = 0; x < kTexSize; ++x) {
                const int sx = x * w / kTexSize;
                const int sy = y * h / kTexSize;
                std::copy_n(raw.data() + (size_t(sy) * size_t(w) + size_t(sx)) * 4,
                            4,
                            out.data() + (size_t(y) * kTexSize + size_t(x)) * 4);
            }
        }
        return out;
    };

    std::array<std::vector<unsigned char>, kMaterialLayers> layers;
    layers[0] = decodeResampled("textures/asphalt.png", 0);
    layers[1] = decodeResampled("textures/concrete.png", 1);
    layers[2] = decodeResampled("textures/brick.png", 2);
    layers[3] = decodeResampled("textures/rust.png", 3);
    layers[4] = fallbackLayer(4);
    layers[5] = decodeResampled("textures/wood.png", 5);
    layers[6] = decodeResampled("textures/fabric.png", 6);
    layers[7] = decodeResampled("textures/glass.png", 7);

    gl.GenTextures(1, &materialArrayTex_);
    gl.BindTexture(GL_TEXTURE_2D_ARRAY, materialArrayTex_);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.TexImage3D(GL_TEXTURE_2D_ARRAY,
                  0,
                  static_cast<GLint>(GL_SRGB8_ALPHA8),
                  kTexSize,
                  kTexSize,
                  kMaterialLayers,
                  0,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  nullptr);
    for (int z = 0; z < kMaterialLayers; ++z) {
        gl.TexSubImage3D(GL_TEXTURE_2D_ARRAY,
                         0,
                         0,
                         0,
                         z,
                         kTexSize,
                         kTexSize,
                         1,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         layers[size_t(z)].data());
    }
    gl.GenerateMipmap(GL_TEXTURE_2D_ARRAY);
    gl.TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
    gl.TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    gl.TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_REPEAT));
    gl.TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_REPEAT));
    gl.BindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return true;
}

bool DesktopRenderer::init(ekron::gl::Api &gl, const std::string &assetRoot, std::string &errorOut) {
    skyProg_ = compileProgram(gl, kSkyVert, kSkyFrag, errorOut);
    worldProg_ = compileProgram(gl, kWorldVert, kWorldFrag, errorOut);
    modelProg_ = compileProgram(gl, kModelVert, kWorldFrag, errorOut);
    pipProg_ = compileProgram(gl, kPipVert, kPipFrag, errorOut);
    uiProg_ = compileProgram(gl, kUiVert, kUiFrag, errorOut);
    if (!skyProg_ || !worldProg_ || !modelProg_ || !pipProg_ || !uiProg_) {
        return false;
    }

    gl.GenVertexArrays(1, &emptyVao_);

    auto setupMeshVao = [&](GLuint &vao, GLuint &vbo) {
        gl.GenVertexArrays(1, &vao);
        gl.GenBuffers(1, &vbo);
        gl.BindVertexArray(vao);
        gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
        constexpr GLsizei stride = sizeof(sandbox::MeshVertex);
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(0));
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(3 * sizeof(float)));
        gl.EnableVertexAttribArray(2);
        gl.VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(6 * sizeof(float)));
        gl.EnableVertexAttribArray(3);
        gl.VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(9 * sizeof(float)));
        gl.EnableVertexAttribArray(4);
        gl.VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void *>(11 * sizeof(float)));
    };
    setupMeshVao(worldVao_, worldVbo_);
    setupMeshVao(ghostVao_, ghostVbo_);

    gl.GenVertexArrays(1, &uiVao_);
    gl.GenBuffers(1, &uiVbo_);
    gl.BindVertexArray(uiVao_);
    gl.BindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    constexpr GLsizei uiStride = sizeof(UiVertex);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, uiStride, reinterpret_cast<const void *>(0));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, uiStride, reinterpret_cast<const void *>(2 * sizeof(float)));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, uiStride, reinterpret_cast<const void *>(4 * sizeof(float)));
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, uiStride, reinterpret_cast<const void *>(8 * sizeof(float)));

    gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);

    // 1x1 white texture
    const unsigned char white[4] = {255, 255, 255, 255};
    gl.GenTextures(1, &whiteTex_);
    gl.BindTexture(GL_TEXTURE_2D, whiteTex_);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_SRGB8_ALPHA8), 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));

    // Built-in UTF-8 ASCII + Cyrillic font atlas
    const auto fontRgba = ekron::font::buildAtlasRgba();
    gl.GenTextures(1, &fontTex_);
    gl.BindTexture(GL_TEXTURE_2D, fontTex_);
    gl.TexImage2D(GL_TEXTURE_2D,
                  0,
                  static_cast<GLint>(GL_RGBA8),
                  ekron::font::kAtlasW,
                  ekron::font::kAtlasH,
                  0,
                  GL_RGBA,
                  GL_UNSIGNED_BYTE,
                  fontRgba.data());
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));

    texturesReady_ = loadMaterialArray(gl, assetRoot);

    // Offscreen 480x270 FPV monitor FBO
    gl.GenFramebuffers(1, &monitorFbo_);
    gl.BindFramebuffer(GL_FRAMEBUFFER, monitorFbo_);
    gl.GenTextures(1, &monitorColorTex_);
    gl.BindTexture(GL_TEXTURE_2D, monitorColorTex_);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), kMonitorW, kMonitorH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, monitorColorTex_, 0);

    gl.GenRenderbuffers(1, &monitorDepthRbo_);
    gl.BindRenderbuffer(GL_RENDERBUFFER, monitorDepthRbo_);
    gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kMonitorW, kMonitorH);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, monitorDepthRbo_);
    monitorReady_ = (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.BindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void DesktopRenderer::shutdown(ekron::gl::Api &gl) {
    for (auto &kv : iconCache_) {
        if (kv.second) gl.DeleteTextures(1, &kv.second);
    }
    iconCache_.clear();
    if (monitorDepthRbo_) gl.DeleteRenderbuffers(1, &monitorDepthRbo_);
    if (monitorColorTex_) gl.DeleteTextures(1, &monitorColorTex_);
    if (monitorFbo_) gl.DeleteFramebuffers(1, &monitorFbo_);
    if (fontTex_) gl.DeleteTextures(1, &fontTex_);
    if (materialArrayTex_) gl.DeleteTextures(1, &materialArrayTex_);
    if (whiteTex_) gl.DeleteTextures(1, &whiteTex_);
    if (uiVbo_) gl.DeleteBuffers(1, &uiVbo_);
    if (uiVao_) gl.DeleteVertexArrays(1, &uiVao_);
    if (ghostVbo_) gl.DeleteBuffers(1, &ghostVbo_);
    if (ghostVao_) gl.DeleteVertexArrays(1, &ghostVao_);
    if (worldVbo_) gl.DeleteBuffers(1, &worldVbo_);
    if (worldVao_) gl.DeleteVertexArrays(1, &worldVao_);
    if (emptyVao_) gl.DeleteVertexArrays(1, &emptyVao_);
    if (uiProg_) gl.DeleteProgram(uiProg_);
    if (pipProg_) gl.DeleteProgram(pipProg_);
    if (modelProg_) gl.DeleteProgram(modelProg_);
    if (worldProg_) gl.DeleteProgram(worldProg_);
    if (skyProg_) gl.DeleteProgram(skyProg_);
}

GLuint DesktopRenderer::loadCatalogIcon(ekron::gl::Api &gl, const std::string &assetRoot, const std::string &iconFile) {
    auto it = iconCache_.find(iconFile);
    if (it != iconCache_.end()) return it->second;

    std::vector<unsigned char> png;
    GLuint tex = 0;
    if (readBinaryFile(assetRoot + "/catalog/rendered/" + iconFile, png)) {
        int w = 0, h = 0;
        std::vector<unsigned char> rgba;
        if (decodeImageBytes(png.data(), png.size(), w, h, rgba) && w > 0 && h > 0) {
            gl.GenTextures(1, &tex);
            gl.BindTexture(GL_TEXTURE_2D, tex);
            gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
            gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            gl.GenerateMipmap(GL_TEXTURE_2D);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
            gl.BindTexture(GL_TEXTURE_2D, 0);
        }
    }
    iconCache_[iconFile] = tex;
    return tex;
}

void DesktopRenderer::drawGltfModel(ekron::gl::Api &gl,
                                    const DesktopGltfModel &model,
                                    const std::array<float, 16> &transform,
                                    float eyeX,
                                    float eyeZ,
                                    bool hologram) {
    if (!model.ready()) return;
    const auto &meshes = model.meshes();
    const auto &scene = model.scene();
    const auto &textures = model.textures();

    std::vector<size_t> order(meshes.size());
    std::iota(order.begin(), order.end(), 0);
    auto blended = [&](const UploadedGlMesh &m) {
        return m.material >= 0 && size_t(m.material) < scene.materials.size() &&
               scene.materials[size_t(m.material)].alphaMode == 2;
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const bool ba = blended(meshes[a]);
        const bool bb = blended(meshes[b]);
        if (ba != bb) return !ba;
        return ba && std::hypot(meshes[a].x - eyeX, meshes[a].z - eyeZ) > std::hypot(meshes[b].x - eyeX, meshes[b].z - eyeZ);
    });

    gl.UniformMatrix4fv(gl.GetUniformLocation(modelProg_, "uObjectTransform"), 1, GL_FALSE, transform.data());
    gl.Uniform1i(gl.GetUniformLocation(modelProg_, "uModelPass"), 1);
    gl.Uniform1i(gl.GetUniformLocation(modelProg_, "uPreviewPass"), hologram ? 1 : 0);

    for (size_t idx : order) {
        const auto &mesh = meshes[idx];
        if (!mesh.vao || mesh.count <= 0) continue;
        city::Material fallback;
        const auto &mat = (mesh.material >= 0 && size_t(mesh.material) < scene.materials.size())
            ? scene.materials[size_t(mesh.material)]
            : fallback;

        const bool useBlend = hologram || mat.alphaMode == 2;
        if (useBlend) {
            gl.Enable(GL_BLEND);
            gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl.DepthMask(hologram ? GL_TRUE : GL_FALSE);
        } else {
            gl.Disable(GL_BLEND);
            gl.DepthMask(GL_TRUE);
        }

        const bool hasTex = mat.image >= 0 && mesh.material >= 0 &&
                            size_t(mesh.material) < textures.size() && textures[size_t(mesh.material)] != 0;
        gl.ActiveTexture(GL_TEXTURE1);
        gl.BindTexture(GL_TEXTURE_2D, hasTex ? textures[size_t(mesh.material)] : whiteTex_);
        gl.Uniform1i(gl.GetUniformLocation(modelProg_, "uModelTextured"), hasTex ? 1 : 0);
        gl.Uniform4f(gl.GetUniformLocation(modelProg_, "uBaseColorFactor"),
                     mat.color[0], mat.color[1], mat.color[2], mat.color[3]);
        gl.Uniform1f(gl.GetUniformLocation(modelProg_, "uModelRoughness"), mat.roughness);
        gl.Uniform1i(gl.GetUniformLocation(modelProg_, "uAlphaMode"), mat.alphaMode);
        gl.Uniform1f(gl.GetUniformLocation(modelProg_, "uAlphaCutoff"), mat.cutoff);

        gl.BindVertexArray(mesh.vao);
        gl.DrawArrays(GL_TRIANGLES, 0, mesh.count);
    }

    gl.Disable(GL_BLEND);
    gl.DepthMask(GL_TRUE);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindVertexArray(0);
}

void DesktopRenderer::drawWorldScene(ekron::gl::Api &gl,
                                     int viewW,
                                     int viewH,
                                     const std::vector<sandbox::MeshVertex> &worldVertices,
                                     const DesktopGltfModel &cityModel,
                                     const DesktopGltfModel &carModel,
                                     const std::vector<std::array<float, 16>> &carTransforms,
                                     physics::Vec3 eye,
                                     float yaw,
                                     float pitch,
                                     float roll) {
    const float aspect = float(std::max(viewW, 1)) / float(std::max(viewH, 1));
    gl.Viewport(0, 0, viewW, viewH);
    gl.Disable(GL_CULL_FACE);
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthFunc(GL_LEQUAL);
    gl.DepthMask(GL_TRUE);
    gl.Disable(GL_BLEND);
    gl.ClearColor(0.56f, 0.61f, 0.63f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Sky pass
    gl.UseProgram(skyProg_);
    gl.DepthMask(GL_FALSE);
    gl.Uniform2f(gl.GetUniformLocation(skyProg_, "uAngles"), yaw, pitch);
    gl.Uniform1f(gl.GetUniformLocation(skyProg_, "uRoll"), roll);
    gl.Uniform1f(gl.GetUniformLocation(skyProg_, "uAspect"), aspect);
    gl.BindVertexArray(emptyVao_);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);
    gl.DepthMask(GL_TRUE);

    // Procedural + placed objects pass
    if (!worldVertices.empty()) {
        gl.UseProgram(worldProg_);
        applyCommonSceneUniforms(gl, worldProg_, eye, yaw, pitch, roll, aspect, texturesReady_);
        gl.UniformMatrix4fv(gl.GetUniformLocation(worldProg_, "uObjectTransform"), 1, GL_FALSE, kIdentityMatrix.data());
        gl.Uniform1i(gl.GetUniformLocation(worldProg_, "uModelPass"), 0);
        gl.Uniform1i(gl.GetUniformLocation(worldProg_, "uPreviewPass"), 0);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D_ARRAY, materialArrayTex_);
        gl.ActiveTexture(GL_TEXTURE1);
        gl.BindTexture(GL_TEXTURE_2D, whiteTex_);

        gl.BindVertexArray(worldVao_);
        gl.BindBuffer(GL_ARRAY_BUFFER, worldVbo_);
        gl.BufferData(GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(worldVertices.size() * sizeof(sandbox::MeshVertex)),
                      worldVertices.data(),
                      GL_DYNAMIC_DRAW);
        gl.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(worldVertices.size()));
    }

    // glTF city + vehicles
    gl.UseProgram(modelProg_);
    applyCommonSceneUniforms(gl, modelProg_, eye, yaw, pitch, roll, aspect, texturesReady_);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D_ARRAY, materialArrayTex_);

    drawGltfModel(gl, cityModel, kIdentityMatrix, eye.x, eye.z, false);
    if (carModel.ready()) {
        for (const auto &carMat : carTransforms) {
            drawGltfModel(gl, carModel, carMat, eye.x, eye.z, false);
        }
    }
}

void DesktopRenderer::renderMonitorPass(ekron::gl::Api &gl,
                                        const std::vector<sandbox::MeshVertex> &worldVertices,
                                        const DesktopGltfModel &cityModel,
                                        const DesktopGltfModel &carModel,
                                        const std::vector<std::array<float, 16>> &carTransforms,
                                        physics::Vec3 eye,
                                        float yaw,
                                        float pitch,
                                        float roll) {
    if (!monitorReady_) return;
    gl.BindFramebuffer(GL_FRAMEBUFFER, monitorFbo_);
    drawWorldScene(gl, kMonitorW, kMonitorH, worldVertices, cityModel, carModel, carTransforms, eye, yaw, pitch, roll);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DesktopRenderer::renderMainWorld(ekron::gl::Api &gl,
                                      int drawableW,
                                      int drawableH,
                                      const std::vector<sandbox::MeshVertex> &worldVertices,
                                      const std::vector<sandbox::MeshVertex> &ghostVertices,
                                      const DesktopGltfModel &cityModel,
                                      const DesktopGltfModel &carModel,
                                      const std::vector<std::array<float, 16>> &carTransforms,
                                      const std::array<float, 16> *ghostCarTransform,
                                      const DesktopGltfModel *droneModel,
                                      const std::array<float, 16> *droneTransform,
                                      const DesktopGltfModel *remoteViewmodel,
                                      bool showMonitorOverlay,
                                      physics::Vec3 eye,
                                      float yaw,
                                      float pitch,
                                      float roll) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    drawWorldScene(gl, drawableW, drawableH, worldVertices, cityModel, carModel, carTransforms, eye, yaw, pitch, roll);

    const float aspect = float(std::max(drawableW, 1)) / float(std::max(drawableH, 1));

    // Placement hologram preview
    if (ghostCarTransform && carModel.ready()) {
        gl.UseProgram(modelProg_);
        applyCommonSceneUniforms(gl, modelProg_, eye, yaw, pitch, roll, aspect, texturesReady_);
        drawGltfModel(gl, carModel, *ghostCarTransform, eye.x, eye.z, true);
    } else if (!ghostVertices.empty()) {
        gl.UseProgram(worldProg_);
        applyCommonSceneUniforms(gl, worldProg_, eye, yaw, pitch, roll, aspect, texturesReady_);
        gl.UniformMatrix4fv(gl.GetUniformLocation(worldProg_, "uObjectTransform"), 1, GL_FALSE, kIdentityMatrix.data());
        gl.Uniform1i(gl.GetUniformLocation(worldProg_, "uModelPass"), 0);
        gl.Uniform1i(gl.GetUniformLocation(worldProg_, "uPreviewPass"), 1);
        gl.Enable(GL_BLEND);
        gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl.DepthMask(GL_TRUE);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D_ARRAY, materialArrayTex_);
        gl.BindVertexArray(ghostVao_);
        gl.BindBuffer(GL_ARRAY_BUFFER, ghostVbo_);
        gl.BufferData(GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(ghostVertices.size() * sizeof(sandbox::MeshVertex)),
                      ghostVertices.data(),
                      GL_DYNAMIC_DRAW);
        gl.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(ghostVertices.size()));
        gl.Disable(GL_BLEND);
    }

    // 3D FPV drone in world
    if (droneModel && droneTransform && droneModel->ready()) {
        gl.UseProgram(modelProg_);
        applyCommonSceneUniforms(gl, modelProg_, eye, yaw, pitch, roll, aspect, texturesReady_);
        drawGltfModel(gl, *droneModel, *droneTransform, eye.x, eye.z, false);
    }

    // Depth-cleared 3D FPV remote controller viewmodel & live monitor PiP
    if ((remoteViewmodel && remoteViewmodel->ready()) || (showMonitorOverlay && monitorReady_)) {
        gl.Clear(GL_DEPTH_BUFFER_BIT);
        if (remoteViewmodel && remoteViewmodel->ready()) {
            gl.UseProgram(modelProg_);
            applyCommonSceneUniforms(gl, modelProg_, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, aspect, texturesReady_);
            const auto &lo = remoteViewmodel->scene().minimum;
            const auto &hi = remoteViewmodel->scene().maximum;
            const std::array<float, 16> vmMat = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                -(lo[0] + hi[0]) * 0.5f,
                -0.18f - (lo[1] + hi[1]) * 0.5f,
                -0.55f - (lo[2] + hi[2]) * 0.5f,
                1
            };
            drawGltfModel(gl, *remoteViewmodel, vmMat, 0.0f, 0.0f, false);
        }

        if (showMonitorOverlay && monitorReady_) {
            const float w = float(std::max(drawableW, 1));
            const float h = float(std::max(drawableH, 1));
            const float monW = std::max(32.0f, std::min(w * 0.22f, h * 0.46f));
            const float monH = monW * (9.0f / 16.0f);
            const float pad = std::max(3.0f, monW * 0.018f);
            const float totalW = monW + 2.0f * pad;
            const float totalH = monH + 2.0f * pad;
            const float bottomY = h * 0.20f;
            const float centerY = -1.0f + 2.0f * ((bottomY + totalH * 0.5f) / h);

            gl.UseProgram(pipProg_);
            gl.Disable(GL_DEPTH_TEST);
            gl.Disable(GL_BLEND);
            gl.Uniform4f(gl.GetUniformLocation(pipProg_, "uRectNdc"), 0.0f, centerY, totalW / w, totalH / h);
            gl.Uniform2f(gl.GetUniformLocation(pipProg_, "uBorder"), pad / totalW, pad / totalH);
            gl.Uniform1i(gl.GetUniformLocation(pipProg_, "uFeed"), 0);
            gl.ActiveTexture(GL_TEXTURE0);
            gl.BindTexture(GL_TEXTURE_2D, monitorColorTex_);
            gl.BindVertexArray(emptyVao_);
            gl.DrawArrays(GL_TRIANGLES, 0, 6);
            gl.Enable(GL_DEPTH_TEST);
        }
    }
}

void DesktopRenderer::beginUi(int logicalW, int logicalH) {
    uiLogicalW_ = std::max(logicalW, 1);
    uiLogicalH_ = std::max(logicalH, 1);
    uiVertices_.clear();
    uiCmds_.clear();
}

void DesktopRenderer::drawRect(float x, float y, float w, float h, Color c) {
    if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f) return;
    const GLint first = static_cast<GLint>(uiVertices_.size());
    const float x1 = x + w;
    const float y1 = y + h;
    uiVertices_.push_back({x,  y,  0, 0, c.r, c.g, c.b, c.a, 0.0f});
    uiVertices_.push_back({x1, y,  1, 0, c.r, c.g, c.b, c.a, 0.0f});
    uiVertices_.push_back({x1, y1, 1, 1, c.r, c.g, c.b, c.a, 0.0f});
    uiVertices_.push_back({x,  y,  0, 0, c.r, c.g, c.b, c.a, 0.0f});
    uiVertices_.push_back({x1, y1, 1, 1, c.r, c.g, c.b, c.a, 0.0f});
    uiVertices_.push_back({x,  y1, 0, 1, c.r, c.g, c.b, c.a, 0.0f});
    if (!uiCmds_.empty() && uiCmds_.back().texture == 0) {
        uiCmds_.back().count += 6;
    } else {
        uiCmds_.push_back({0, first, 6});
    }
}

void DesktopRenderer::drawBorder(float x, float y, float w, float h, float t, Color c) {
    drawRect(x, y, w, t, c);
    drawRect(x, y + h - t, w, t, c);
    drawRect(x, y + t, t, std::max(0.0f, h - 2.0f * t), c);
    drawRect(x + w - t, y + t, t, std::max(0.0f, h - 2.0f * t), c);
}

void DesktopRenderer::drawText(float x, float y, std::string_view utf8, float scale, Color c) {
    if (utf8.empty() || scale <= 0.0f || c.a <= 0.0f) return;
    const float gw = float(ekron::font::kGlyphW) * scale;
    const float gh = float(ekron::font::kGlyphH) * scale;
    float penX = std::round(x);
    const float penY = std::round(y);
    size_t i = 0;
    while (i < utf8.size()) {
        const uint32_t cp = ekron::font::nextUtf8Codepoint(utf8, i);
        if (cp == '\n') {
            penX = std::round(x);
            continue;
        }
        const int slot = ekron::font::glyphSlot(cp);
        const int col = slot % ekron::font::kAtlasCols;
        const int row = slot / ekron::font::kAtlasCols;
        const float u0 = float(col * ekron::font::kGlyphW) / float(ekron::font::kAtlasW);
        const float v0 = float(row * ekron::font::kGlyphH) / float(ekron::font::kAtlasH);
        const float u1 = float((col + 1) * ekron::font::kGlyphW) / float(ekron::font::kAtlasW);
        const float v1 = float((row + 1) * ekron::font::kGlyphH) / float(ekron::font::kAtlasH);

        const GLint first = static_cast<GLint>(uiVertices_.size());
        uiVertices_.push_back({penX,      penY,      u0, v0, c.r, c.g, c.b, c.a, 1.0f});
        uiVertices_.push_back({penX + gw, penY,      u1, v0, c.r, c.g, c.b, c.a, 1.0f});
        uiVertices_.push_back({penX + gw, penY + gh, u1, v1, c.r, c.g, c.b, c.a, 1.0f});
        uiVertices_.push_back({penX,      penY,      u0, v0, c.r, c.g, c.b, c.a, 1.0f});
        uiVertices_.push_back({penX + gw, penY + gh, u1, v1, c.r, c.g, c.b, c.a, 1.0f});
        uiVertices_.push_back({penX,      penY + gh, u0, v1, c.r, c.g, c.b, c.a, 1.0f});
        if (!uiCmds_.empty() && uiCmds_.back().texture == 0) {
            uiCmds_.back().count += 6;
        } else {
            uiCmds_.push_back({0, first, 6});
        }
        penX += gw;
    }
}

void DesktopRenderer::drawCenteredText(float cx, float cy, std::string_view utf8, float scale, Color c) {
    const float w = ekron::font::measureTextWidth(utf8, scale);
    const float h = float(ekron::font::kGlyphH) * scale;
    drawText(cx - w * 0.5f, cy - h * 0.5f, utf8, scale, c);
}

void DesktopRenderer::drawIcon(ekron::gl::Api &gl, GLuint tex, float x, float y, float w, float h, Color tint) {
    (void)gl;
    if (!tex || w <= 0.0f || h <= 0.0f) return;
    const GLint first = static_cast<GLint>(uiVertices_.size());
    uiVertices_.push_back({x,     y,     0.0f, 0.0f, tint.r, tint.g, tint.b, tint.a, 2.0f});
    uiVertices_.push_back({x + w, y,     1.0f, 0.0f, tint.r, tint.g, tint.b, tint.a, 2.0f});
    uiVertices_.push_back({x + w, y + h, 1.0f, 1.0f, tint.r, tint.g, tint.b, tint.a, 2.0f});
    uiVertices_.push_back({x,     y,     0.0f, 0.0f, tint.r, tint.g, tint.b, tint.a, 2.0f});
    uiVertices_.push_back({x + w, y + h, 1.0f, 1.0f, tint.r, tint.g, tint.b, tint.a, 2.0f});
    uiVertices_.push_back({x,     y + h, 0.0f, 1.0f, tint.r, tint.g, tint.b, tint.a, 2.0f});
    if (!uiCmds_.empty() && uiCmds_.back().texture == tex) {
        uiCmds_.back().count += 6;
    } else {
        uiCmds_.push_back({tex, first, 6});
    }
}

void DesktopRenderer::flushUi(ekron::gl::Api &gl, int drawableW, int drawableH) {
    if (uiVertices_.empty() || uiCmds_.empty()) return;
    gl.Viewport(0, 0, drawableW, drawableH);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl.UseProgram(uiProg_);
    gl.Uniform2f(gl.GetUniformLocation(uiProg_, "uScreenSize"), float(uiLogicalW_), float(uiLogicalH_));
    gl.Uniform1i(gl.GetUniformLocation(uiProg_, "uFontTex"), 0);
    gl.Uniform1i(gl.GetUniformLocation(uiProg_, "uIconTex"), 1);

    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, fontTex_);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, whiteTex_);

    gl.BindVertexArray(uiVao_);
    gl.BindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    gl.BufferData(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(uiVertices_.size() * sizeof(UiVertex)),
                  uiVertices_.data(),
                  GL_DYNAMIC_DRAW);

    for (const auto &cmd : uiCmds_) {
        gl.ActiveTexture(GL_TEXTURE1);
        gl.BindTexture(GL_TEXTURE_2D, cmd.texture ? cmd.texture : whiteTex_);
        gl.DrawArrays(GL_TRIANGLES, cmd.first, cmd.count);
    }

    gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Disable(GL_BLEND);
    gl.Enable(GL_DEPTH_TEST);
}

} // namespace ekron::desktop
