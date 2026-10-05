#include "Gl.h"

#include <cstdio>

namespace tw {

GlFunctions GL;

bool loadGl(void* (*getProc)(const char* name)) {
#if defined(__APPLE__)
    (void)getProc;
#define TW_LOAD(name) GL.name = &gl##name;
#else
#define TW_LOAD(name)                                                       \
    *reinterpret_cast<void**>(&GL.name) = getProc("gl" #name);              \
    if (!GL.name) { std::fprintf(stderr, "twisted: missing gl" #name "\n"); return false; }
#endif
    TW_LOAD(CreateShader) TW_LOAD(ShaderSource) TW_LOAD(CompileShader) TW_LOAD(GetShaderiv)
    TW_LOAD(GetShaderInfoLog) TW_LOAD(CreateProgram) TW_LOAD(AttachShader) TW_LOAD(BindAttribLocation)
    TW_LOAD(LinkProgram) TW_LOAD(GetProgramiv) TW_LOAD(GetProgramInfoLog) TW_LOAD(UseProgram)
    TW_LOAD(DeleteShader) TW_LOAD(DeleteProgram) TW_LOAD(GetUniformLocation) TW_LOAD(Uniform1f)
    TW_LOAD(Uniform1i) TW_LOAD(Uniform2f) TW_LOAD(Uniform3f) TW_LOAD(Uniform4f) TW_LOAD(Uniform3fv)
    TW_LOAD(Uniform1fv) TW_LOAD(UniformMatrix4fv) TW_LOAD(GenBuffers) TW_LOAD(BindBuffer)
    TW_LOAD(BufferData) TW_LOAD(BufferSubData) TW_LOAD(DeleteBuffers) TW_LOAD(GenVertexArrays)
    TW_LOAD(BindVertexArray) TW_LOAD(DeleteVertexArrays) TW_LOAD(EnableVertexAttribArray)
    TW_LOAD(VertexAttribPointer) TW_LOAD(ActiveTexture)
#undef TW_LOAD
    return true;
}

static unsigned compileStage(unsigned type, const char* src) {
    unsigned s = GL.CreateShader(type);
    GL.ShaderSource(s, 1, &src, nullptr);
    GL.CompileShader(s);
    int ok = 0;
    GL.GetShaderiv(s, TW_GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GL.GetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "twisted: shader error: %s\n", log);
    }
    return s;
}

unsigned compileProgram(const char* vs, const char* fs, const char* const* attribs, int numAttribs) {
    const unsigned v = compileStage(TW_GL_VERTEX_SHADER, vs);
    const unsigned f = compileStage(TW_GL_FRAGMENT_SHADER, fs);
    const unsigned p = GL.CreateProgram();
    GL.AttachShader(p, v);
    GL.AttachShader(p, f);
    for (int i = 0; i < numAttribs; ++i) GL.BindAttribLocation(p, (unsigned)i, attribs[i]);
    GL.LinkProgram(p);
    int ok = 0;
    GL.GetProgramiv(p, TW_GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GL.GetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "twisted: link error: %s\n", log);
    }
    GL.DeleteShader(v);
    GL.DeleteShader(f);
    return p;
}

} // namespace tw
