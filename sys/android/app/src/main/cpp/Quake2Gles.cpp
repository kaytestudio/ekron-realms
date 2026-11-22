// Realms map mode on Android: Quake 2 entities and effects (see Quake2Gles.h).
#include "Quake2Gles.h"

#include "realms/GameView.h"

#include <android/log.h>

#include <algorithm>
#include <cstddef>

namespace {

// The sandbox camera (Renderer.cpp's vertex shader): yaw about Y, pitch,
// 72 degree vertical field of view, near 0.1.
const char *entityVertex=R"(#version 300 es
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
uniform mat4 objectTransform;
uniform vec3 eye;
uniform vec2 angles;
uniform float aspect;
out vec3 worldNormal;
out vec2 texUV;
out vec3 toEye;
void main(){
 vec3 p=(objectTransform*vec4(position,1.0)).xyz-eye;
 float cy=cos(angles.x),sy=sin(angles.x),cp=cos(angles.y),sp=sin(angles.y);
 vec3 q=vec3(cy*p.x-sy*p.z,p.y,sy*p.x+cy*p.z);
 q=vec3(q.x,cp*q.y+sp*q.z,-sp*q.y+cp*q.z);
 float f=1.37638;
 gl_Position=vec4(q.x*f/aspect,q.y*f,-1.001334*q.z-.200134,-q.z);
 worldNormal=mat3(objectTransform)*normal;texUV=uv;toEye=p;
})";

// Quake 2 light values (1 = the skin as drawn) doubled like gl_intensity 2
// and taken to linear space; dynamic lights added like the desktop client.
const char *entityFragment=R"(#version 300 es
precision highp float;
in vec3 worldNormal;
in vec2 texUV;
in vec3 toEye;
uniform sampler2D skin;
uniform bool textured;
uniform vec4 tint;
uniform bool fullbright;
uniform bool lit;
uniform vec3 light;
uniform vec3 eye;
uniform vec3 lightDirection;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform int dlightCount;
uniform vec4 dlights[8];
uniform vec4 dlightColors[8];
out vec4 outColor;
void main(){
 vec4 s=textured?texture(skin,texUV):vec4(1.0);
 vec3 base=pow(max(tint.rgb,vec3(0.0)),vec3(2.2))*s.rgb;
 float alpha=tint.a*s.a;
 vec3 n=normalize(worldNormal);
 if(!gl_FrontFacing)n=-n;
 float diffuse=max(dot(n,normalize(-lightDirection)),0.0);
 vec3 c;
 if(lit)c=base*pow(2.0*max(light,vec3(0.0)),vec3(2.2))*(.72+.56*diffuse);
 else c=base*(ambientColor*(.78+.22*max(n.y,0.0))+lightColor*diffuse);
 vec3 world=toEye+eye;
 for(int i=0;i<(lit?0:dlightCount)&&i<8;i++){
  vec3 toLight=dlights[i].xyz-world;
  float f=clamp(1.0-length(toLight)/max(dlights[i].w,1e-4),0.0,1.0);
  float facing=dot(toLight,toLight)>1e-8?.35+.65*max(dot(n,normalize(toLight)),0.0):1.0;
  c+=base*dlightColors[i].rgb*(1.6*f*f*facing);
 }
 if(fullbright)c=base;
 float d=max(length(toEye)-22.0,0.0);
 float fog=1.0-exp(-d*d*.00022);
 vec3 fogColor=pow(vec3(.56,.61,.63),vec3(2.2));
 outColor=vec4(pow(max(mix(c,fogColor,fog),vec3(0.0)),vec3(1.0/2.2)),alpha);
})";

const char *effectVertex=R"(#version 300 es
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
layout(location=2) in vec3 shape;
uniform vec3 eye;
uniform vec2 angles;
uniform float aspect;
out vec4 tint;
out vec3 corner;
void main(){
 vec3 p=position-eye;
 float cy=cos(angles.x),sy=sin(angles.x),cp=cos(angles.y),sp=sin(angles.y);
 vec3 q=vec3(cy*p.x-sy*p.z,p.y,sy*p.x+cy*p.z);
 q=vec3(q.x,cp*q.y+sp*q.z,-sp*q.y+cp*q.z);
 float f=1.37638;
 gl_Position=vec4(q.x*f/aspect,q.y*f,-1.001334*q.z-.200134,-q.z);
 tint=color;corner=shape;
})";

// Round particles fade out to the edge; beams (shape 1) are solid.
const char *effectFragment=R"(#version 300 es
precision highp float;
in vec4 tint;
in vec3 corner;
out vec4 outColor;
void main(){
 float a=tint.a;
 if(corner.z<.5){
  float r=length(corner.xy);
  if(r>1.0)discard;
  a*=1.0-smoothstep(.55,1.0,r);
 }
 outColor=vec4(tint.rgb,a);
})";

GLuint compile(GLenum type, const char *text) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "Realms", "map: Quake 2 shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint program(const char *vs, const char *fs) {
    const GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    GLuint p = 0;
    if (v && f) {
        p = glCreateProgram();
        glAttachShader(p, v);
        glAttachShader(p, f);
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(p, sizeof(log), nullptr, log);
            __android_log_print(ANDROID_LOG_ERROR, "Realms", "map: Quake 2 program: %s", log);
            glDeleteProgram(p);
            p = 0;
        }
    }
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    return p;
}

void setCamera(GLuint p, const Quake2Gles::Camera &c) {
    glUniform3f(glGetUniformLocation(p, "eye"), c.eye.x, c.eye.y, c.eye.z);
    glUniform2f(glGetUniformLocation(p, "angles"), c.yaw, c.pitch);
    glUniform1f(glGetUniformLocation(p, "aspect"), c.aspect);
}

} // namespace

bool Quake2Gles::ensure() {
    if (tried_) return entityProgram_ && effectProgram_;
    tried_ = true;
    entityProgram_ = program(entityVertex, entityFragment);
    effectProgram_ = program(effectVertex, effectFragment);
    if (!entityProgram_ || !effectProgram_) return false;
    using EV = mapview::EntityVertex;
    glGenVertexArrays(1, &entityVao_);
    glGenBuffers(1, &entityVbo_);
    glBindVertexArray(entityVao_);
    glBindBuffer(GL_ARRAY_BUFFER, entityVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EV), reinterpret_cast<void *>(offsetof(EV, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EV), reinterpret_cast<void *>(offsetof(EV, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(EV), reinterpret_cast<void *>(offsetof(EV, u)));
    using FV = mapview::EffectVertex;
    glGenVertexArrays(1, &effectVao_);
    glGenBuffers(1, &effectVbo_);
    glBindVertexArray(effectVao_);
    glBindBuffer(GL_ARRAY_BUFFER, effectVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FV), reinterpret_cast<void *>(offsetof(FV, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FV), reinterpret_cast<void *>(offsetof(FV, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(FV), reinterpret_cast<void *>(offsetof(FV, u)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(entityProgram_);
    glUniform1i(glGetUniformLocation(entityProgram_, "skin"), 0);
    return true;
}

void Quake2Gles::release() {
    for (auto &kv : skins_)
        if (kv.second) glDeleteTextures(1, &kv.second);
    skins_.clear();
    if (entityVbo_) glDeleteBuffers(1, &entityVbo_);
    if (effectVbo_) glDeleteBuffers(1, &effectVbo_);
    if (entityVao_) glDeleteVertexArrays(1, &entityVao_);
    if (effectVao_) glDeleteVertexArrays(1, &effectVao_);
    if (entityProgram_) glDeleteProgram(entityProgram_);
    if (effectProgram_) glDeleteProgram(effectProgram_);
    entityVbo_ = effectVbo_ = entityVao_ = effectVao_ = entityProgram_ = effectProgram_ = 0;
    tried_ = false;
}

size_t Quake2Gles::skinsLoaded() const {
    size_t n = 0;
    for (const auto &kv : skins_) n += kv.second != 0;
    return n;
}

GLuint Quake2Gles::skin(realms::Engine &engine, const std::string &name) {
    if (name.empty()) return 0;
    const auto it = skins_.find(name);
    if (it != skins_.end()) return it->second;
    GLuint tex = 0;
    realms::RgbaImage img;
    std::string error;
    if (realms::GameModelCache::loadSkin(engine, name, img, error)) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     img.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else {
        engine.print("map: skin " + name + ": " + error + "\n", REALMS_PRINT_DEVELOPER);
    }
    skins_[name] = tex;
    return tex;
}

void Quake2Gles::drawEntities(realms::Engine &engine, const std::vector<mapview::EntityVertex> &vertices,
                              const std::vector<mapview::EntityBatch> &batches,
                              const std::vector<mapview::DynamicLight> &lights, const Camera &camera) {
    if (vertices.empty() || batches.empty() || !ensure()) return;
    glBindVertexArray(entityVao_);
    glBindBuffer(GL_ARRAY_BUFFER, entityVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(mapview::EntityVertex)), vertices.data(),
                 GL_STREAM_DRAW);
    glUseProgram(entityProgram_);
    setCamera(entityProgram_, camera);
    glUniform3f(glGetUniformLocation(entityProgram_, "lightDirection"), -.45f, -1.f, -.25f);
    glUniform3f(glGetUniformLocation(entityProgram_, "lightColor"), .55f, .52f, .48f);
    glUniform3f(glGetUniformLocation(entityProgram_, "ambientColor"), .55f, .59f, .65f);
    float pos[8 * 4] = {}, col[8 * 4] = {};
    const int count = int(std::min<size_t>(lights.size(), 8));
    for (int i = 0; i < count; i++) {
        const mapview::DynamicLight &l = lights[size_t(i)];
        const float p[4] = {l.x, l.y, l.z, l.radius}, c[4] = {l.r, l.g, l.b, 1.0f};
        std::copy(p, p + 4, pos + i * 4);
        std::copy(c, c + 4, col + i * 4);
    }
    glUniform1i(glGetUniformLocation(entityProgram_, "dlightCount"), count);
    glUniform4fv(glGetUniformLocation(entityProgram_, "dlights"), 8, pos);
    glUniform4fv(glGetUniformLocation(entityProgram_, "dlightColors"), 8, col);
    const GLint transformLoc = glGetUniformLocation(entityProgram_, "objectTransform");
    const GLint tintLoc = glGetUniformLocation(entityProgram_, "tint");
    const GLint texturedLoc = glGetUniformLocation(entityProgram_, "textured");
    const GLint fullbrightLoc = glGetUniformLocation(entityProgram_, "fullbright");
    const GLint litLoc = glGetUniformLocation(entityProgram_, "lit");
    const GLint lightLoc = glGetUniformLocation(entityProgram_, "light");
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    // opaque first, then translucent (shells, BFG / plasma), view weapon last of each
    for (int pass = 0; pass < 2; pass++) {
        const bool blended = pass == 1;
        if (blended) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
        for (const mapview::EntityBatch &b : batches) {
            if ((b.color[3] < 0.999f) != blended || b.count <= 0) continue;
            if (size_t(b.first) + size_t(b.count) > vertices.size()) continue;
            const GLuint tex = skin(engine, b.skin);
            std::array<float, 4> tint = b.color;
            // untextured models (skin missing) get a neutral grey, boxes their colour
            if (!tex && !b.fallbackBox) tint = {0.7f * b.color[0], 0.7f * b.color[1], 0.72f * b.color[2], b.color[3]};
            glBindTexture(GL_TEXTURE_2D, tex);
            glUniformMatrix4fv(transformLoc, 1, GL_FALSE, b.transform.data());
            glUniform4f(tintLoc, tint[0], tint[1], tint[2], tint[3]);
            glUniform1i(texturedLoc, tex ? 1 : 0);
            glUniform1i(fullbrightLoc, b.fullbright ? 1 : 0);
            glUniform1i(litLoc, b.lit ? 1 : 0);
            glUniform3f(lightLoc, b.light[0], b.light[1], b.light[2]);
            // the view weapon never sinks into walls (ref_gl: gldepthmax 0.3)
            if (b.viewWeapon) glDepthRangef(0.0f, 0.3f);
            glDrawArrays(GL_TRIANGLES, b.first, b.count);
            if (b.viewWeapon) glDepthRangef(0.0f, 1.0f);
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
}

void Quake2Gles::drawEffects(const std::vector<mapview::EffectVertex> &vertices, const Camera &camera) {
    if (vertices.empty() || !ensure()) return;
    glBindVertexArray(effectVao_);
    glBindBuffer(GL_ARRAY_BUFFER, effectVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(mapview::EffectVertex)), vertices.data(),
                 GL_STREAM_DRAW);
    glUseProgram(effectProgram_);
    setCamera(effectProgram_, camera);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
