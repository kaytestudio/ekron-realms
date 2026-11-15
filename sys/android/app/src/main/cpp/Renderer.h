#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <chrono>
#include "SandboxWorld.h"
#include "TextureLoader.h"
#include "GltfRenderer.h"
struct android_app;
class Renderer {
public:
    explicit Renderer(android_app *app);
    ~Renderer();
    void handleInput();
    void render();
    void resetInput();
private:
    android_app *app_;
    EGLDisplay display_=EGL_NO_DISPLAY;
    EGLSurface surface_=EGL_NO_SURFACE;
    EGLContext context_=EGL_NO_CONTEXT;
    GLuint program_=0,skyProgram_=0,buffer_=0,vao_=0;
    TextureLoader textures_;
    city::GltfRenderer city_;
    city::GltfRenderer car_;
    bool texturesReady_=false;
    int width_=1,height_=1,leftId_=-1,rightId_=-1;
    float originX_=0,originY_=0,rightX_=0,rightY_=0,moveX_=0,moveY_=0;
    bool building_=false;
    sandbox::World world_;
    std::string savePath_;
    std::chrono::steady_clock::time_point last_;
};
