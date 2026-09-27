#include "client/gl.h"

#include <stdexcept>
#include <vector>

#include "core/log.h"

namespace mcw::gl {

const char* shaderHeader() {
#if defined(__EMSCRIPTEN__)
  return "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2DArray;\n";
#else
  return "#version 330 core\n";
#endif
}

static GLuint compile(GLenum type, std::string_view src, const char* name) {
  const std::string full = std::string(shaderHeader()) + std::string(src);
  const GLuint sh = glCreateShader(type);
  const char* p = full.c_str();
  glShaderSource(sh, 1, &p, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> logBuf(static_cast<std::size_t>(len) + 1);
    glGetShaderInfoLog(sh, len, nullptr, logBuf.data());
    log::error("shader {} ({}): {}", name, type == GL_VERTEX_SHADER ? "vértices" : "fragmentos", logBuf.data());
    throw std::runtime_error(std::string("no compila el shader ") + name);
  }
  return sh;
}

GLuint makeProgram(std::string_view vs, std::string_view fs, const char* name) {
  const GLuint v = compile(GL_VERTEX_SHADER, vs, name);
  const GLuint f = compile(GL_FRAGMENT_SHADER, fs, name);
  const GLuint prog = glCreateProgram();
  glAttachShader(prog, v);
  glAttachShader(prog, f);
  glLinkProgram(prog);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> logBuf(static_cast<std::size_t>(len) + 1);
    glGetProgramInfoLog(prog, len, nullptr, logBuf.data());
    log::error("programa {}: {}", name, logBuf.data());
    throw std::runtime_error(std::string("no enlaza el programa ") + name);
  }
  return prog;
}

void checkErrors(const char* where) {
  for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) log::warn("error de GL 0x{:04X} en {}", e, where);
}

}  // namespace mcw::gl
