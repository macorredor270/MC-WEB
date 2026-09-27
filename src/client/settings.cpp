#include "client/settings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <string>

#include "core/fs.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace mcw {
namespace {

float toFloat(std::string_view v, float fallback) {
  float out = fallback;
  try {
    out = std::stof(std::string(v));
  } catch (...) {
  }
  return std::isfinite(out) ? out : fallback;
}

}  // namespace

std::string Settings::serialize() const {
  return std::format(
      "renderDistance:{}\nfov:{}\nbrightness:{}\nsensitivity:{}\nclouds:{}\nviewBobbing:{}\nshowFps:{}\nguiScale:{}\nvolume:{}\n",
      renderDistance, fov, brightness, sensitivity, clouds, viewBobbing, showFps, guiScale, volume);
}

void Settings::parse(std::string_view text) {
  while (!text.empty()) {
    const std::size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) continue;
    const std::string_view key = line.substr(0, colon);
    std::string_view value = line.substr(colon + 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.remove_suffix(1);
    const bool on = value == "true";
    if (key == "renderDistance")
      renderDistance = std::clamp(static_cast<int>(toFloat(value, static_cast<float>(renderDistance))), kMinRenderDistance, kMaxRenderDistance);
    else if (key == "fov") fov = std::clamp(toFloat(value, fov), 30.0f, 110.0f);
    else if (key == "brightness") brightness = std::clamp(toFloat(value, brightness), 0.0f, 1.0f);
    else if (key == "sensitivity") sensitivity = std::clamp(toFloat(value, sensitivity), 0.0f, 1.0f);
    else if (key == "clouds") clouds = on;
    else if (key == "viewBobbing") viewBobbing = on;
    else if (key == "showFps") showFps = on;
    else if (key == "guiScale") guiScale = std::clamp(static_cast<int>(toFloat(value, 0)), 0, 6);
    else if (key == "volume") volume = std::clamp(toFloat(value, volume), 0.0f, 1.0f);
  }
}

float Settings::sensitivityScale() const { return std::pow(2.0f, (sensitivity - 0.5f) * 4.0f); }

#ifdef __EMSCRIPTEN__
EM_JS(char*, mcw_js_load_settings, (), {
  let s = '';
  try { s = localStorage.getItem('mcweb.options') || ''; } catch (e) {}
  return stringToNewUTF8(s);
});
EM_JS(void, mcw_js_save_settings, (const char* text), {
  try { localStorage.setItem('mcweb.options', UTF8ToString(text)); } catch (e) {}
});

std::string loadSettingsText() {
  char* p = mcw_js_load_settings();
  std::string s = p ? p : "";
  free(p);
  return s;
}
void saveSettingsText(const std::string& text) { mcw_js_save_settings(text.c_str()); }
#else
std::string loadSettingsText() { return fs::readText(fs::userDataDir() / "options.txt").value_or(""); }
void saveSettingsText(const std::string& text) { fs::writeFile(fs::userDataDir() / "options.txt", text.data(), text.size()); }
#endif

}  // namespace mcw
