#include "client/perf.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

namespace mcw {
namespace {

// (en el navegador, las constantes de la extensión no están en las cabeceras de GLES3)
constexpr GLenum kTimeElapsed = 0x88BF;  // GL_TIME_ELAPSED / GL_TIME_ELAPSED_EXT
constexpr GLenum kGpuDisjoint = 0x8FBB;  // GL_GPU_DISJOINT_EXT

}  // namespace

void GpuTimer::init() {
#ifdef __EMSCRIPTEN__
  // Solo si el navegador deja medir (Chrome de escritorio sí; muchos móviles y Safari no)
  supported_ = emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(), "EXT_disjoint_timer_query_webgl2") != 0;
#else
  supported_ = GLAD_GL_VERSION_3_3 != 0;
#endif
  if (!supported_) return;
  glGenQueries(kQueries, queries_);
}

void GpuTimer::begin() {
  if (!supported_ || open_ || inFlight_[next_]) return;  // (si la GPU va muy atrasada, este frame no se mide)
  glBeginQuery(kTimeElapsed, queries_[next_]);
  open_ = true;
}

void GpuTimer::end() {
  if (!supported_) return;
  if (open_) {
    glEndQuery(kTimeElapsed);
    inFlight_[next_] = true;
    next_ = (next_ + 1) % kQueries;
    open_ = false;
  }
  // Recoger lo que ya está listo, sin esperar
  for (int i = 0; i < kQueries; i++) {
    if (!inFlight_[i]) continue;
    GLuint ready = 0;
    glGetQueryObjectuiv(queries_[i], GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) continue;
    GLuint ns = 0;
    glGetQueryObjectuiv(queries_[i], GL_QUERY_RESULT, &ns);
    inFlight_[i] = false;
    GLint disjoint = 0;
#ifdef __EMSCRIPTEN__
    glGetIntegerv(kGpuDisjoint, &disjoint);
#else
    (void)kGpuDisjoint;
#endif
    if (disjoint) continue;  // la GPU cambió de frecuencia o se perdió: ese tiempo no vale
    const double v = static_cast<double>(ns) / 1e6;
    ms_ = haveValue_ ? ms_ * 0.9 + v * 0.1 : v;
    haveValue_ = true;
  }
}

}  // namespace mcw
