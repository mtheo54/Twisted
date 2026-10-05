// Tiny OpenGL 3.2 core loader: no GLEW, no GLAD. OpenGL 1.1 entry points come from the
// system library; everything newer is loaded at runtime through the platform's
// GetProcAddress (wglGetProcAddress / glXGetProcAddress). On macOS the framework
// exports all of them directly.
#pragma once

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <GL/gl.h>
#endif

#include <cstddef>

#ifndef APIENTRY
#define APIENTRY
#endif

namespace tw {

// Constants missing from the 1.1 headers (values from the Khronos registry).
enum : unsigned {
    TW_GL_ARRAY_BUFFER = 0x8892,
    TW_GL_STATIC_DRAW = 0x88E4,
    TW_GL_DYNAMIC_DRAW = 0x88E8,
    TW_GL_STREAM_DRAW = 0x88E0,
    TW_GL_FRAGMENT_SHADER = 0x8B30,
    TW_GL_VERTEX_SHADER = 0x8B31,
    TW_GL_COMPILE_STATUS = 0x8B81,
    TW_GL_LINK_STATUS = 0x8B82,
    TW_GL_INFO_LOG_LENGTH = 0x8B84,
    TW_GL_PROGRAM_POINT_SIZE = 0x8642,
    TW_GL_TEXTURE0 = 0x84C0,
    TW_GL_R8 = 0x8229,
    TW_GL_RED = 0x1903,
    TW_GL_CLAMP_TO_EDGE = 0x812F,
    TW_GL_MULTISAMPLE = 0x809D,
    TW_GL_FRAMEBUFFER_SRGB = 0x8DB9,
};

typedef char GLcharT;
typedef ptrdiff_t GLsizeiptrT;
typedef ptrdiff_t GLintptrT;

struct GlFunctions {
    unsigned(APIENTRY* CreateShader)(unsigned);
    void(APIENTRY* ShaderSource)(unsigned, int, const GLcharT* const*, const int*);
    void(APIENTRY* CompileShader)(unsigned);
    void(APIENTRY* GetShaderiv)(unsigned, unsigned, int*);
    void(APIENTRY* GetShaderInfoLog)(unsigned, int, int*, GLcharT*);
    unsigned(APIENTRY* CreateProgram)();
    void(APIENTRY* AttachShader)(unsigned, unsigned);
    void(APIENTRY* BindAttribLocation)(unsigned, unsigned, const GLcharT*);
    void(APIENTRY* LinkProgram)(unsigned);
    void(APIENTRY* GetProgramiv)(unsigned, unsigned, int*);
    void(APIENTRY* GetProgramInfoLog)(unsigned, int, int*, GLcharT*);
    void(APIENTRY* UseProgram)(unsigned);
    void(APIENTRY* DeleteShader)(unsigned);
    void(APIENTRY* DeleteProgram)(unsigned);
    int(APIENTRY* GetUniformLocation)(unsigned, const GLcharT*);
    void(APIENTRY* Uniform1f)(int, float);
    void(APIENTRY* Uniform1i)(int, int);
    void(APIENTRY* Uniform2f)(int, float, float);
    void(APIENTRY* Uniform3f)(int, float, float, float);
    void(APIENTRY* Uniform4f)(int, float, float, float, float);
    void(APIENTRY* Uniform3fv)(int, int, const float*);
    void(APIENTRY* Uniform1fv)(int, int, const float*);
    void(APIENTRY* UniformMatrix4fv)(int, int, unsigned char, const float*);
    void(APIENTRY* GenBuffers)(int, unsigned*);
    void(APIENTRY* BindBuffer)(unsigned, unsigned);
    void(APIENTRY* BufferData)(unsigned, GLsizeiptrT, const void*, unsigned);
    void(APIENTRY* BufferSubData)(unsigned, GLintptrT, GLsizeiptrT, const void*);
    void(APIENTRY* DeleteBuffers)(int, const unsigned*);
    void(APIENTRY* GenVertexArrays)(int, unsigned*);
    void(APIENTRY* BindVertexArray)(unsigned);
    void(APIENTRY* DeleteVertexArrays)(int, const unsigned*);
    void(APIENTRY* EnableVertexAttribArray)(unsigned);
    void(APIENTRY* VertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void*);
    void(APIENTRY* ActiveTexture)(unsigned);
};

extern GlFunctions GL;

// Loads all entry points. getProc returns the address of a GL function or null.
// Returns false if one is missing (the editor then shows nothing rather than crashing).
bool loadGl(void* (*getProc)(const char* name));

unsigned compileProgram(const char* vs, const char* fs, const char* const* attribs, int numAttribs);

} // namespace tw
