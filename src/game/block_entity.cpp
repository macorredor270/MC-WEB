#include "game/block_entity.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace mcw {

const std::vector<BannerPatternInfo>& bannerPatterns() {
  static const std::vector<BannerPatternInfo> kPatterns = {
      {"bs", "stripe_bottom"},        {"ts", "stripe_top"},           {"ls", "stripe_left"},        {"rs", "stripe_right"},
      {"cs", "stripe_center"},        {"ms", "stripe_middle"},        {"drs", "stripe_downright"},  {"dls", "stripe_downleft"},
      {"ss", "small_stripes"},        {"cr", "cross"},                {"sc", "straight_cross"},     {"bt", "triangle_bottom"},
      {"tt", "triangle_top"},         {"bts", "triangles_bottom"},    {"tts", "triangles_top"},     {"ld", "diagonal_left"},
      {"rd", "diagonal_right"},       {"lud", "diagonal_up_left"},    {"rud", "diagonal_up_right"}, {"mc", "circle"},
      {"mr", "rhombus"},              {"vh", "half_vertical"},        {"hh", "half_horizontal"},    {"vhr", "half_vertical_right"},
      {"hhb", "half_horizontal_bottom"}, {"bo", "border"},            {"cbo", "curly_border"},      {"gra", "gradient"},
      {"gru", "gradient_up"},         {"bri", "bricks"},              {"cre", "creeper"},           {"sku", "skull"},
      {"flo", "flower"},              {"moj", "mojang"},              {"bl", "square_bottom_left"}, {"br", "square_bottom_right"},
      {"tl", "square_top_left"},      {"tr", "square_top_right"}};
  return kPatterns;
}

std::string bannerPatternTexture(const std::string& code) {
  for (const BannerPatternInfo& p : bannerPatterns())
    if (code == p.code) return p.texture;
  return {};
}

std::string signLineToJson(const std::string& line) { return nlohmann::json{{"text", line}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

namespace {
void collectText(const nlohmann::json& j, std::string& out) {
  if (j.is_string()) {
    out += j.get<std::string>();
  } else if (j.is_object()) {
    if (auto t = j.find("text"); t != j.end() && t->is_string()) out += t->get<std::string>();
    if (auto e = j.find("extra"); e != j.end() && e->is_array())
      for (const auto& x : *e) collectText(x, out);
  } else if (j.is_array()) {
    for (const auto& x : j) collectText(x, out);
  }
}
}  // namespace

std::string signLineFromJson(const std::string& json) {
  const auto j = nlohmann::json::parse(json, nullptr, false);
  if (j.is_discarded()) return json;  // texto suelto sin comillas
  std::string out;
  collectText(j, out);
  return out;
}

int signRotationFor(float yaw) {
  const double mcYaw = 180.0 - yaw * 180.0 / 3.14159265358979;
  return static_cast<int>(std::floor((mcYaw + 180.0) * 16.0 / 360.0 + 0.5)) & 15;
}

}  // namespace mcw
