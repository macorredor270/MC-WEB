#include "client/ogg.h"

#include <algorithm>
#include <cstdlib>

// stb_vorbis (dominio público / MIT): decodificador de Vorbis de un solo archivo
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wundef"
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mcw {

std::vector<float> decodeOggMono(std::span<const u8> data, int sampleRate) {
  int channels = 0, rate = 0;
  short* pcm = nullptr;
  const int frames = stb_vorbis_decode_memory(data.data(), static_cast<int>(data.size()), &channels, &rate, &pcm);
  if (frames <= 0 || !pcm || channels <= 0 || rate <= 0) {
    std::free(pcm);
    return {};
  }
  std::vector<float> mono(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; i++) {
    float sum = 0;
    for (int c = 0; c < channels; c++) sum += pcm[i * channels + c];
    mono[static_cast<std::size_t>(i)] = sum / static_cast<float>(channels) / 32768.0f;
  }
  std::free(pcm);
  if (rate == sampleRate) return mono;
  const double ratio = static_cast<double>(rate) / sampleRate;
  std::vector<float> out(static_cast<std::size_t>(static_cast<double>(frames) / ratio));
  for (std::size_t i = 0; i < out.size(); i++) {
    const double p = static_cast<double>(i) * ratio;
    const std::size_t a = static_cast<std::size_t>(p), b = std::min(a + 1, mono.size() - 1);
    const float f = static_cast<float>(p - static_cast<double>(a));
    out[i] = mono[a] * (1.0f - f) + mono[b] * f;
  }
  return out;
}

}  // namespace mcw
