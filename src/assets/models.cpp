// Horneado de blockstates y modelos JSON del formato público de resource packs de 1.8
// (documentado en minecraft.wiki, "Model"). Implementación propia.
#include "assets/models.h"

#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>

#include "assets/pack.h"
#include "assets/textures.h"
#include "core/face.h"
#include "core/log.h"
#include "data/blockstates.h"

namespace mcw {
namespace {

using json = nlohmann::json;
using Vec3 = std::array<float, 3>;

struct ModelFace {
  std::string texture;
  std::optional<std::array<float, 4>> uv;
  int cullface = -1;
  int rotation = 0;
  int tintIndex = -1;
};

struct ModelElement {
  Vec3 from{}, to{};
  bool rotated = false;
  Vec3 origin{8, 8, 8};
  int axis = 1;
  float angle = 0;
  bool rescale = false;
  bool shade = true;
  std::array<std::optional<ModelFace>, 6> faces;
};

struct ResolvedModel {
  std::map<std::string, std::string> textures;
  std::vector<ModelElement> elements;
  bool hasElements = false;
  bool ambientOcclusion = true;
};

std::string modelPath(std::string name) {
  if (name.rfind("minecraft:", 0) == 0) name = name.substr(10);
  return "assets/minecraft/models/" + name + ".json";
}

Vec3 readVec3(const json& j) {
  Vec3 v{};
  if (j.is_array() && j.size() == 3)
    for (int i = 0; i < 3; i++) v[i] = j[i].get<float>();
  return v;
}

class ModelLoader {
 public:
  ModelLoader(const PackStack& packs, std::vector<std::string>& errors) : packs_(packs), errors_(errors) {}

  /// Carga un modelo con toda su cadena de padres ya fusionada. `name` p.ej. "block/stone".
  const ResolvedModel* load(const std::string& name, int depth = 0) {
    if (auto it = cache_.find(name); it != cache_.end()) return it->second ? &*it->second : nullptr;
    if (depth > 16 || name.rfind("builtin/", 0) == 0) return nullptr;
    auto j = packs_.readJson(modelPath(name));
    if (!j) {
      errors_.push_back("modelo no encontrado: " + name);
      cache_[name] = std::nullopt;
      return nullptr;
    }
    ResolvedModel m;
    if (j->contains("parent")) {
      if (const ResolvedModel* parent = load((*j)["parent"].get<std::string>(), depth + 1)) m = *parent;
    }
    if (j->contains("ambientocclusion")) m.ambientOcclusion = (*j)["ambientocclusion"].get<bool>();
    if (j->contains("textures"))
      for (auto& [k, v] : (*j)["textures"].items())
        if (v.is_string()) m.textures[k] = v.get<std::string>();
    if (j->contains("elements")) {
      m.elements.clear();
      m.hasElements = true;
      for (const json& e : (*j)["elements"]) m.elements.push_back(parseElement(e));
    }
    cache_[name] = m;
    return &*cache_[name];
  }

 private:
  static ModelElement parseElement(const json& e) {
    ModelElement el;
    el.from = readVec3(e.value("from", json::array()));
    el.to = readVec3(e.value("to", json::array()));
    el.shade = e.value("shade", true);
    if (e.contains("rotation")) {
      const json& r = e["rotation"];
      el.rotated = true;
      el.origin = readVec3(r.value("origin", json::array({8, 8, 8})));
      const std::string axis = r.value("axis", "y");
      el.axis = axis == "x" ? 0 : (axis == "y" ? 1 : 2);
      el.angle = r.value("angle", 0.0f);
      el.rescale = r.value("rescale", false);
    }
    if (e.contains("faces")) {
      for (auto& [k, f] : e["faces"].items()) {
        const int dir = faceFromName(k);
        if (dir < 0) continue;
        ModelFace mf;
        mf.texture = f.value("texture", "");
        if (f.contains("uv") && f["uv"].is_array() && f["uv"].size() == 4)
          mf.uv = std::array<float, 4>{f["uv"][0].get<float>(), f["uv"][1].get<float>(), f["uv"][2].get<float>(), f["uv"][3].get<float>()};
        if (f.contains("cullface")) mf.cullface = faceFromName(f["cullface"].get<std::string>());
        mf.rotation = f.value("rotation", 0);
        mf.tintIndex = f.value("tintindex", -1);
        el.faces[dir] = mf;
      }
    }
    return el;
  }

  const PackStack& packs_;
  std::vector<std::string>& errors_;
  std::map<std::string, std::optional<ResolvedModel>> cache_;
};

/// Resuelve "#var" siguiendo las referencias entre variables de textura.
std::optional<std::string> resolveTexture(const ResolvedModel& m, std::string ref) {
  for (int i = 0; i < 10 && !ref.empty() && ref[0] == '#'; i++) {
    auto it = m.textures.find(ref.substr(1));
    if (it == m.textures.end()) return std::nullopt;
    ref = it->second;
  }
  if (ref.empty() || ref[0] == '#') return std::nullopt;
  return ref;
}

/// Esquinas de cada cara (arriba-izq, abajo-izq, abajo-der, arriba-der vistas desde fuera).
std::array<Vec3, 4> faceCorners(int face, const Vec3& f, const Vec3& t) {
  switch (face) {
    case Face::Down: return {{{f[0], f[1], t[2]}, {f[0], f[1], f[2]}, {t[0], f[1], f[2]}, {t[0], f[1], t[2]}}};
    case Face::Up: return {{{f[0], t[1], f[2]}, {f[0], t[1], t[2]}, {t[0], t[1], t[2]}, {t[0], t[1], f[2]}}};
    case Face::North: return {{{t[0], t[1], f[2]}, {t[0], f[1], f[2]}, {f[0], f[1], f[2]}, {f[0], t[1], f[2]}}};
    case Face::South: return {{{f[0], t[1], t[2]}, {f[0], f[1], t[2]}, {t[0], f[1], t[2]}, {t[0], t[1], t[2]}}};
    case Face::West: return {{{f[0], t[1], f[2]}, {f[0], f[1], f[2]}, {f[0], f[1], t[2]}, {f[0], t[1], t[2]}}};
    default: return {{{t[0], t[1], t[2]}, {t[0], f[1], t[2]}, {t[0], f[1], f[2]}, {t[0], t[1], f[2]}}};
  }
}

/// UV por defecto (proyección de la posición) en unidades de 0..16.
std::array<float, 2> projectUV(int face, const Vec3& p) {
  switch (face) {
    case Face::Down: return {p[0], 16 - p[2]};
    case Face::Up: return {p[0], p[2]};
    case Face::North: return {16 - p[0], 16 - p[1]};
    case Face::South: return {p[0], 16 - p[1]};
    case Face::West: return {p[2], 16 - p[1]};
    default: return {16 - p[2], 16 - p[1]};
  }
}

std::array<float, 4> defaultUV(int face, const Vec3& f, const Vec3& t) {
  switch (face) {
    case Face::Down: return {f[0], 16 - t[2], t[0], 16 - f[2]};
    case Face::Up: return {f[0], f[2], t[0], t[2]};
    case Face::North: return {16 - t[0], 16 - t[1], 16 - f[0], 16 - f[1]};
    case Face::South: return {f[0], 16 - t[1], t[0], 16 - f[1]};
    case Face::West: return {f[2], 16 - t[1], t[2], 16 - f[1]};
    default: return {16 - t[2], 16 - t[1], 16 - f[2], 16 - f[1]};
  }
}

void rotateAround(Vec3& p, const Vec3& o, int axis, float degrees, bool rescale) {
  const float rad = degrees * 3.14159265358979f / 180.0f;
  const float c = std::cos(rad), s = std::sin(rad);
  const int a = axis == 0 ? 1 : 0, b = axis == 2 ? 1 : 2;  // los dos ejes que giran
  float u = p[a] - o[a], v = p[b] - o[b];
  float nu, nv;
  if (axis == 1) { nu = u * c + v * s; nv = -u * s + v * c; }  // y: x,z
  else { nu = u * c - v * s; nv = u * s + v * c; }            // x: y,z   z: x,y
  if (rescale && std::abs(c) > 1e-4f) {
    const float k = 1.0f / c;
    nu *= k;
    nv *= k;
  }
  p[a] = nu + o[a];
  p[b] = nv + o[b];
}

/// Giro de variante (múltiplos de 90º) de un punto en 0..16 alrededor del centro del bloque.
void rotateVariant(Vec3& p, int rx, int ry) {
  for (int i = 0; i < ((rx / 90) & 3); i++) {  // x: norte -> abajo
    const float y = p[1] - 8, z = p[2] - 8;
    p[1] = z + 8;
    p[2] = -y + 8;
  }
  for (int i = 0; i < ((ry / 90) & 3); i++) {  // y: este -> sur (horario visto desde arriba)
    const float x = p[0] - 8, z = p[2] - 8;
    p[0] = -z + 8;
    p[2] = x + 8;
  }
}

int rotateFaceDir(int face, int rx, int ry) {
  Vec3 n{static_cast<float>(kFaceNormals[face][0]) * 8 + 8, static_cast<float>(kFaceNormals[face][1]) * 8 + 8,
         static_cast<float>(kFaceNormals[face][2]) * 8 + 8};
  rotateVariant(n, rx, ry);
  for (int f = 0; f < 6; f++)
    if (std::abs(n[0] - (kFaceNormals[f][0] * 8 + 8)) < 0.01f && std::abs(n[1] - (kFaceNormals[f][1] * 8 + 8)) < 0.01f &&
        std::abs(n[2] - (kFaceNormals[f][2] * 8 + 8)) < 0.01f)
      return f;
  return face;
}

int nearestFace(const std::array<Vec3, 4>& p) {
  const Vec3 a{p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
  const Vec3 b{p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
  const Vec3 n{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  int best = Face::Up;
  float bestDot = -1e9f;
  for (int f = 0; f < 6; f++) {
    const float d = n[0] * kFaceNormals[f][0] + n[1] * kFaceNormals[f][1] + n[2] * kFaceNormals[f][2];
    if (d > bestDot) { bestDot = d; best = f; }
  }
  return best;
}

BakedModel bakeModel(const ResolvedModel& m, int rx, int ry, bool uvlock, BlockTextures& textures, std::vector<std::string>& errors,
                     const std::string& what) {
  BakedModel out;
  out.ambientOcclusion = m.ambientOcclusion;
  for (const ModelElement& el : m.elements) {
    for (int face = 0; face < 6; face++) {
      if (!el.faces[face]) continue;
      const ModelFace& mf = *el.faces[face];
      BakedQuad q;
      auto tex = resolveTexture(m, mf.texture);
      if (!tex) {
        errors.push_back("textura sin resolver " + mf.texture + " en " + what);
        q.layer = BlockTextures::kMissingLayer;
      } else {
        q.layer = textures.layerFor(*tex);
      }
      q.tintIndex = static_cast<i8>(mf.tintIndex);
      q.shade = el.shade;

      auto corners = faceCorners(face, el.from, el.to);
      const auto uv = mf.uv.value_or(defaultUV(face, el.from, el.to));
      std::array<std::array<float, 2>, 4> uvs = {{{uv[0], uv[1]}, {uv[0], uv[3]}, {uv[2], uv[3]}, {uv[2], uv[1]}}};
      const int rot = ((mf.rotation / 90) % 4 + 4) % 4;
      if (rot) {
        auto r = uvs;
        for (int i = 0; i < 4; i++) uvs[i] = r[(i + rot) % 4];
      }
      for (auto& c : corners) {
        if (el.rotated && el.angle != 0) rotateAround(c, el.origin, el.axis, el.angle, el.rescale);
        rotateVariant(c, rx, ry);
      }
      const int finalFace = rotateFaceDir(face, rx, ry);
      if (uvlock && (rx || ry))
        for (int i = 0; i < 4; i++) uvs[i] = projectUV(finalFace, corners[i]);

      for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 3; k++) q.pos[i][k] = corners[i][k] / 16.0f;
        q.uv[i] = {uvs[i][0] / 16.0f, uvs[i][1] / 16.0f};
      }
      q.cullface = static_cast<i8>(mf.cullface >= 0 ? rotateFaceDir(mf.cullface, rx, ry) : -1);
      q.face = static_cast<i8>(nearestFace(corners));
      // ¿Alineada con un eje y sobre el plano de la cara del bloque?
      const int axis = q.face / 2 == 0 ? 1 : (q.face / 2 == 1 ? 2 : 0);
      const float plane = (q.face % 2 == 1) ? 1.0f : 0.0f;
      bool flat = true;
      for (int i = 0; i < 4; i++) flat &= std::abs(q.pos[i][axis] - q.pos[0][axis]) < 1e-4f;
      q.onFace = flat && std::abs(q.pos[0][axis] - plane) < 1e-4f;
      out.quads.push_back(q);
    }
  }
  return out;
}

}  // namespace

BlockModels::BlockModels() {
  // Modelo de "falta": cubo con la textura de error
  BakedModel cube;
  for (int face = 0; face < 6; face++) {
    BakedQuad q;
    const auto corners = faceCorners(face, {0, 0, 0}, {16, 16, 16});
    for (int i = 0; i < 4; i++)
      for (int k = 0; k < 3; k++) q.pos[i][k] = corners[i][k] / 16.0f;
    q.uv = {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};
    q.cullface = static_cast<i8>(face);
    q.face = static_cast<i8>(face);
    q.onFace = true;
    cube.quads.push_back(q);
  }
  auto v = std::make_shared<VariantList>();
  v->models.push_back(cube);
  v->weights.push_back(1);
  v->totalWeight = 1;
  missing_ = v;
}

void BlockModels::bake(const PackStack& packs, BlockTextures& textures) {
  table_.fill(nullptr);
  errors_.clear();
  baked_ = 0;
  ModelLoader loader(packs, errors_);
  std::map<std::string, std::optional<json>> blockstates;
  std::map<std::string, std::shared_ptr<const VariantList>> variantCache;

  for (const auto& [state, ref] : allMappedStates()) {
    const std::string key = ref.file + "#" + ref.variant;
    if (auto it = variantCache.find(key); it != variantCache.end()) {
      table_[state] = it->second;
      if (it->second) baked_++;
      continue;
    }
    auto bsIt = blockstates.find(ref.file);
    if (bsIt == blockstates.end()) {
      auto j = packs.readJson("assets/minecraft/blockstates/" + ref.file + ".json");
      if (!j) errors_.push_back("blockstate no encontrado: " + ref.file);
      bsIt = blockstates.emplace(ref.file, std::move(j)).first;
    }
    const std::optional<json>& bs = bsIt->second;
    std::shared_ptr<VariantList> list;
    if (bs && bs->contains("variants") && (*bs)["variants"].contains(ref.variant)) {
      const json& v = (*bs)["variants"][ref.variant];
      list = std::make_shared<VariantList>();
      auto addOne = [&](const json& e) {
        const std::string model = e.value("model", "");
        const ResolvedModel* m = loader.load("block/" + model);
        if (!m) return;
        list->models.push_back(bakeModel(*m, e.value("x", 0), e.value("y", 0), e.value("uvlock", false), textures, errors_, key));
        const int w = std::max(1, e.value("weight", 1));
        list->weights.push_back(w);
        list->totalWeight += w;
      };
      if (v.is_array()) for (const json& e : v) addOne(e);
      else addOne(v);
      if (list->models.empty()) list.reset();
    } else if (bs) {
      errors_.push_back("variante no encontrada: " + key);
    }
    variantCache[key] = list;
    table_[state] = list;
    if (list) baked_++;
  }

  waterStill = textures.layerFor("blocks/water_still");
  waterFlow = textures.layerFor("blocks/water_flow");
  lavaStill = textures.layerFor("blocks/lava_still");
  lavaFlow = textures.layerFor("blocks/lava_flow");

  log::info("modelos de bloque: {} estados horneados, {} avisos", baked_, errors_.size());
  for (std::size_t i = 0; i < std::min<std::size_t>(errors_.size(), 10); i++) log::warn("  {}", errors_[i]);
}

}  // namespace mcw
