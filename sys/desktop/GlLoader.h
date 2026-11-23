#pragma once
// Self-contained OpenGL 3.3 Core loader backed by SDL_GL_GetProcAddress.
// Avoids platform-specific OpenGL header differences and deprecation warnings on macOS & Linux.

#if defined(__has_include)
#if __has_include(<SDL.h>)
#include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#else
#include <SDL.h>
#endif
#include <cstddef>
#include <cstdint>
#include <type_traits>

using GLenum     = unsigned int;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLvoid     = void;
using GLbyte     = int8_t;
using GLshort    = int16_t;
using GLint      = int32_t;
using GLubyte    = uint8_t;
using GLushort   = uint16_t;
using GLuint     = uint32_t;
using GLsizei    = int32_t;
using GLfloat    = float;
using GLclampf   = float;
using GLdouble   = double;
using GLchar     = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr   = std::ptrdiff_t;

#ifndef APIENTRY
#if defined(_WIN32)
#define APIENTRY __stdcall
#else
#define APIENTRY
#endif
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

constexpr GLboolean GL_FALSE = 0;
constexpr GLboolean GL_TRUE  = 1;

constexpr GLbitfield GL_DEPTH_BUFFER_BIT   = 0x00000100;
constexpr GLbitfield GL_COLOR_BUFFER_BIT   = 0x00004000;

constexpr GLenum GL_TRIANGLES              = 0x0004;
constexpr GLenum GL_LEQUAL                 = 0x0203;
constexpr GLenum GL_SRC_ALPHA              = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA    = 0x0303;
constexpr GLenum GL_FRONT_AND_BACK         = 0x0408;
constexpr GLenum GL_CULL_FACE              = 0x0B44;
constexpr GLenum GL_DEPTH_TEST             = 0x0B71;
constexpr GLenum GL_BLEND                  = 0x0BE2;
constexpr GLenum GL_UNPACK_ALIGNMENT       = 0x0CF5;
constexpr GLenum GL_TEXTURE_2D             = 0x0DE1;
constexpr GLenum GL_UNSIGNED_BYTE          = 0x1401;
constexpr GLenum GL_FLOAT                  = 0x1406;
constexpr GLenum GL_RGBA                   = 0x1908;
constexpr GLenum GL_NEAREST                = 0x2600;
constexpr GLenum GL_LINEAR                 = 0x2601;
constexpr GLenum GL_NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr GLenum GL_LINEAR_MIPMAP_NEAREST  = 0x2701;
constexpr GLenum GL_NEAREST_MIPMAP_LINEAR  = 0x2702;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR   = 0x2703;
constexpr GLenum GL_TEXTURE_MAG_FILTER     = 0x2800;
constexpr GLenum GL_TEXTURE_MIN_FILTER     = 0x2801;
constexpr GLenum GL_TEXTURE_WRAP_S         = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T         = 0x2803;
constexpr GLenum GL_REPEAT                 = 0x2901;
constexpr GLenum GL_RGBA8                  = 0x8058;
constexpr GLenum GL_CLAMP_TO_EDGE          = 0x812F;
constexpr GLenum GL_DEPTH_COMPONENT24      = 0x81A6;
constexpr GLenum GL_MIRRORED_REPEAT        = 0x8370;
constexpr GLenum GL_TEXTURE0               = 0x84C0;
constexpr GLenum GL_TEXTURE1               = 0x84C1;
constexpr GLenum GL_TEXTURE2               = 0x84C2;
constexpr GLenum GL_TEXTURE3               = 0x84C3;
constexpr GLenum GL_TEXTURE4               = 0x84C4;
constexpr GLenum GL_NO_ERROR               = 0;
constexpr GLenum GL_ARRAY_BUFFER           = 0x8892;
constexpr GLenum GL_STATIC_DRAW            = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW           = 0x88E8;
constexpr GLenum GL_FRAGMENT_SHADER        = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER          = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS         = 0x8B81;
constexpr GLenum GL_LINK_STATUS            = 0x8B82;
constexpr GLenum GL_SRGB8_ALPHA8           = 0x8C43;
constexpr GLenum GL_TEXTURE_2D_ARRAY       = 0x8C1A;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE   = 0x8CD5;
constexpr GLenum GL_COLOR_ATTACHMENT0      = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT       = 0x8D00;
constexpr GLenum GL_FRAMEBUFFER            = 0x8D40;
constexpr GLenum GL_RENDERBUFFER           = 0x8D41;

namespace ekron::gl {

using PFN_glViewport               = void (APIENTRYP)(GLint, GLint, GLsizei, GLsizei);
using PFN_glClearColor             = void (APIENTRYP)(GLfloat, GLfloat, GLfloat, GLfloat);
using PFN_glClear                  = void (APIENTRYP)(GLbitfield);
using PFN_glEnable                 = void (APIENTRYP)(GLenum);
using PFN_glDisable                = void (APIENTRYP)(GLenum);
using PFN_glDepthMask              = void (APIENTRYP)(GLboolean);
using PFN_glDepthFunc              = void (APIENTRYP)(GLenum);
using PFN_glBlendFunc              = void (APIENTRYP)(GLenum, GLenum);
using PFN_glPixelStorei            = void (APIENTRYP)(GLenum, GLint);
using PFN_glDrawArrays             = void (APIENTRYP)(GLenum, GLint, GLsizei);
using PFN_glGenTextures            = void (APIENTRYP)(GLsizei, GLuint *);
using PFN_glDeleteTextures         = void (APIENTRYP)(GLsizei, const GLuint *);
using PFN_glBindTexture            = void (APIENTRYP)(GLenum, GLuint);
using PFN_glActiveTexture          = void (APIENTRYP)(GLenum);
using PFN_glTexParameteri          = void (APIENTRYP)(GLenum, GLenum, GLint);
using PFN_glTexImage2D             = void (APIENTRYP)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
using PFN_glTexImage3D             = void (APIENTRYP)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
using PFN_glTexSubImage3D          = void (APIENTRYP)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *);
using PFN_glGenerateMipmap         = void (APIENTRYP)(GLenum);
using PFN_glGetError               = GLenum (APIENTRYP)(void);
using PFN_glGenBuffers             = void (APIENTRYP)(GLsizei, GLuint *);
using PFN_glDeleteBuffers          = void (APIENTRYP)(GLsizei, const GLuint *);
using PFN_glBindBuffer             = void (APIENTRYP)(GLenum, GLuint);
using PFN_glBufferData             = void (APIENTRYP)(GLenum, GLsizeiptr, const void *, GLenum);
using PFN_glGenVertexArrays        = void (APIENTRYP)(GLsizei, GLuint *);
using PFN_glDeleteVertexArrays     = void (APIENTRYP)(GLsizei, const GLuint *);
using PFN_glBindVertexArray        = void (APIENTRYP)(GLuint);
using PFN_glEnableVertexAttribArray = void (APIENTRYP)(GLuint);
using PFN_glVertexAttribPointer    = void (APIENTRYP)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
using PFN_glCreateShader           = GLuint (APIENTRYP)(GLenum);
using PFN_glDeleteShader           = void (APIENTRYP)(GLuint);
using PFN_glShaderSource           = void (APIENTRYP)(GLuint, GLsizei, const GLchar *const *, const GLint *);
using PFN_glCompileShader          = void (APIENTRYP)(GLuint);
using PFN_glGetShaderiv            = void (APIENTRYP)(GLuint, GLenum, GLint *);
using PFN_glGetShaderInfoLog       = void (APIENTRYP)(GLuint, GLsizei, GLsizei *, GLchar *);
using PFN_glCreateProgram          = GLuint (APIENTRYP)(void);
using PFN_glDeleteProgram          = void (APIENTRYP)(GLuint);
using PFN_glAttachShader           = void (APIENTRYP)(GLuint, GLuint);
using PFN_glLinkProgram            = void (APIENTRYP)(GLuint);
using PFN_glGetProgramiv           = void (APIENTRYP)(GLuint, GLenum, GLint *);
using PFN_glGetProgramInfoLog      = void (APIENTRYP)(GLuint, GLsizei, GLsizei *, GLchar *);
using PFN_glUseProgram             = void (APIENTRYP)(GLuint);
using PFN_glGetUniformLocation     = GLint (APIENTRYP)(GLuint, const GLchar *);
using PFN_glUniform1i              = void (APIENTRYP)(GLint, GLint);
using PFN_glUniform1iv             = void (APIENTRYP)(GLint, GLsizei, const GLint *);
using PFN_glUniform1f              = void (APIENTRYP)(GLint, GLfloat);
using PFN_glUniform2f              = void (APIENTRYP)(GLint, GLfloat, GLfloat);
using PFN_glUniform3f              = void (APIENTRYP)(GLint, GLfloat, GLfloat, GLfloat);
using PFN_glUniform4f              = void (APIENTRYP)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using PFN_glUniform4fv             = void (APIENTRYP)(GLint, GLsizei, const GLfloat *);
using PFN_glUniformMatrix3fv       = void (APIENTRYP)(GLint, GLsizei, GLboolean, const GLfloat *);
using PFN_glUniformMatrix4fv       = void (APIENTRYP)(GLint, GLsizei, GLboolean, const GLfloat *);
using PFN_glGenFramebuffers        = void (APIENTRYP)(GLsizei, GLuint *);
using PFN_glDeleteFramebuffers     = void (APIENTRYP)(GLsizei, const GLuint *);
using PFN_glBindFramebuffer        = void (APIENTRYP)(GLenum, GLuint);
using PFN_glFramebufferTexture2D   = void (APIENTRYP)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFN_glGenRenderbuffers       = void (APIENTRYP)(GLsizei, GLuint *);
using PFN_glDeleteRenderbuffers    = void (APIENTRYP)(GLsizei, const GLuint *);
using PFN_glBindRenderbuffer       = void (APIENTRYP)(GLenum, GLuint);
using PFN_glRenderbufferStorage    = void (APIENTRYP)(GLenum, GLenum, GLsizei, GLsizei);
using PFN_glFramebufferRenderbuffer = void (APIENTRYP)(GLenum, GLenum, GLenum, GLuint);
using PFN_glCheckFramebufferStatus = GLenum (APIENTRYP)(GLenum);

struct Api {
    PFN_glViewport                Viewport = nullptr;
    PFN_glClearColor              ClearColor = nullptr;
    PFN_glClear                   Clear = nullptr;
    PFN_glEnable                  Enable = nullptr;
    PFN_glDisable                 Disable = nullptr;
    PFN_glDepthMask               DepthMask = nullptr;
    PFN_glDepthFunc               DepthFunc = nullptr;
    PFN_glBlendFunc               BlendFunc = nullptr;
    PFN_glPixelStorei             PixelStorei = nullptr;
    PFN_glDrawArrays              DrawArrays = nullptr;
    PFN_glGenTextures             GenTextures = nullptr;
    PFN_glDeleteTextures          DeleteTextures = nullptr;
    PFN_glBindTexture             BindTexture = nullptr;
    PFN_glActiveTexture           ActiveTexture = nullptr;
    PFN_glTexParameteri           TexParameteri = nullptr;
    PFN_glTexImage2D              TexImage2D = nullptr;
    PFN_glTexImage3D              TexImage3D = nullptr;
    PFN_glTexSubImage3D           TexSubImage3D = nullptr;
    PFN_glGenerateMipmap          GenerateMipmap = nullptr;
    PFN_glGetError                GetError = nullptr;
    PFN_glGenBuffers              GenBuffers = nullptr;
    PFN_glDeleteBuffers           DeleteBuffers = nullptr;
    PFN_glBindBuffer              BindBuffer = nullptr;
    PFN_glBufferData              BufferData = nullptr;
    PFN_glGenVertexArrays         GenVertexArrays = nullptr;
    PFN_glDeleteVertexArrays      DeleteVertexArrays = nullptr;
    PFN_glBindVertexArray         BindVertexArray = nullptr;
    PFN_glEnableVertexAttribArray EnableVertexAttribArray = nullptr;
    PFN_glVertexAttribPointer     VertexAttribPointer = nullptr;
    PFN_glCreateShader            CreateShader = nullptr;
    PFN_glDeleteShader            DeleteShader = nullptr;
    PFN_glShaderSource            ShaderSource = nullptr;
    PFN_glCompileShader           CompileShader = nullptr;
    PFN_glGetShaderiv             GetShaderiv = nullptr;
    PFN_glGetShaderInfoLog        GetShaderInfoLog = nullptr;
    PFN_glCreateProgram           CreateProgram = nullptr;
    PFN_glDeleteProgram           DeleteProgram = nullptr;
    PFN_glAttachShader            AttachShader = nullptr;
    PFN_glLinkProgram             LinkProgram = nullptr;
    PFN_glGetProgramiv            GetProgramiv = nullptr;
    PFN_glGetProgramInfoLog       GetProgramInfoLog = nullptr;
    PFN_glUseProgram              UseProgram = nullptr;
    PFN_glGetUniformLocation      GetUniformLocation = nullptr;
    PFN_glUniform1i               Uniform1i = nullptr;
    PFN_glUniform1iv              Uniform1iv = nullptr;
    PFN_glUniform1f               Uniform1f = nullptr;
    PFN_glUniform2f               Uniform2f = nullptr;
    PFN_glUniform3f               Uniform3f = nullptr;
    PFN_glUniform4f               Uniform4f = nullptr;
    PFN_glUniform4fv              Uniform4fv = nullptr;
    PFN_glUniformMatrix3fv        UniformMatrix3fv = nullptr;
    PFN_glUniformMatrix4fv        UniformMatrix4fv = nullptr;
    PFN_glGenFramebuffers         GenFramebuffers = nullptr;
    PFN_glDeleteFramebuffers      DeleteFramebuffers = nullptr;
    PFN_glBindFramebuffer         BindFramebuffer = nullptr;
    PFN_glFramebufferTexture2D    FramebufferTexture2D = nullptr;
    PFN_glGenRenderbuffers        GenRenderbuffers = nullptr;
    PFN_glDeleteRenderbuffers     DeleteRenderbuffers = nullptr;
    PFN_glBindRenderbuffer        BindRenderbuffer = nullptr;
    PFN_glRenderbufferStorage     RenderbufferStorage = nullptr;
    PFN_glFramebufferRenderbuffer FramebufferRenderbuffer = nullptr;
    PFN_glCheckFramebufferStatus  CheckFramebufferStatus = nullptr;

    bool load() {
        bool ok = true;
        auto resolve = [&](auto &fn, const char *name) {
            void *p = SDL_GL_GetProcAddress(name);
            if (!p) ok = false;
            fn = reinterpret_cast<std::decay_t<decltype(fn)>>(p);
        };
        resolve(Viewport, "glViewport");
        resolve(ClearColor, "glClearColor");
        resolve(Clear, "glClear");
        resolve(Enable, "glEnable");
        resolve(Disable, "glDisable");
        resolve(DepthMask, "glDepthMask");
        resolve(DepthFunc, "glDepthFunc");
        resolve(BlendFunc, "glBlendFunc");
        resolve(PixelStorei, "glPixelStorei");
        resolve(DrawArrays, "glDrawArrays");
        resolve(GenTextures, "glGenTextures");
        resolve(DeleteTextures, "glDeleteTextures");
        resolve(BindTexture, "glBindTexture");
        resolve(ActiveTexture, "glActiveTexture");
        resolve(TexParameteri, "glTexParameteri");
        resolve(TexImage2D, "glTexImage2D");
        resolve(TexImage3D, "glTexImage3D");
        resolve(TexSubImage3D, "glTexSubImage3D");
        resolve(GenerateMipmap, "glGenerateMipmap");
        resolve(GetError, "glGetError");
        resolve(GenBuffers, "glGenBuffers");
        resolve(DeleteBuffers, "glDeleteBuffers");
        resolve(BindBuffer, "glBindBuffer");
        resolve(BufferData, "glBufferData");
        resolve(GenVertexArrays, "glGenVertexArrays");
        resolve(DeleteVertexArrays, "glDeleteVertexArrays");
        resolve(BindVertexArray, "glBindVertexArray");
        resolve(EnableVertexAttribArray, "glEnableVertexAttribArray");
        resolve(VertexAttribPointer, "glVertexAttribPointer");
        resolve(CreateShader, "glCreateShader");
        resolve(DeleteShader, "glDeleteShader");
        resolve(ShaderSource, "glShaderSource");
        resolve(CompileShader, "glCompileShader");
        resolve(GetShaderiv, "glGetShaderiv");
        resolve(GetShaderInfoLog, "glGetShaderInfoLog");
        resolve(CreateProgram, "glCreateProgram");
        resolve(DeleteProgram, "glDeleteProgram");
        resolve(AttachShader, "glAttachShader");
        resolve(LinkProgram, "glLinkProgram");
        resolve(GetProgramiv, "glGetProgramiv");
        resolve(GetProgramInfoLog, "glGetProgramInfoLog");
        resolve(UseProgram, "glUseProgram");
        resolve(GetUniformLocation, "glGetUniformLocation");
        resolve(Uniform1i, "glUniform1i");
        resolve(Uniform1iv, "glUniform1iv");
        resolve(Uniform1f, "glUniform1f");
        resolve(Uniform2f, "glUniform2f");
        resolve(Uniform3f, "glUniform3f");
        resolve(Uniform4f, "glUniform4f");
        resolve(Uniform4fv, "glUniform4fv");
        resolve(UniformMatrix3fv, "glUniformMatrix3fv");
        resolve(UniformMatrix4fv, "glUniformMatrix4fv");
        resolve(GenFramebuffers, "glGenFramebuffers");
        resolve(DeleteFramebuffers, "glDeleteFramebuffers");
        resolve(BindFramebuffer, "glBindFramebuffer");
        resolve(FramebufferTexture2D, "glFramebufferTexture2D");
        resolve(GenRenderbuffers, "glGenRenderbuffers");
        resolve(DeleteRenderbuffers, "glDeleteRenderbuffers");
        resolve(BindRenderbuffer, "glBindRenderbuffer");
        resolve(RenderbufferStorage, "glRenderbufferStorage");
        resolve(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
        resolve(CheckFramebufferStatus, "glCheckFramebufferStatus");
        return ok;
    }
};

} // namespace ekron::gl
