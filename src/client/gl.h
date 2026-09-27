#pragma once
// OpenGL 3.3 core en escritorio (glad) y OpenGL ES 3.0 / WebGL2 en navegador.
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <string>
#include <string_view>

namespace mcw::gl {

/// Compila un programa. Las fuentes no llevan `#version`: se añade la cabecera de la plataforma.
GLuint makeProgram(std::string_view vertexSrc, std::string_view fragmentSrc, const char* name);
void checkErrors(const char* where);
const char* shaderHeader();

}  // namespace mcw::gl
