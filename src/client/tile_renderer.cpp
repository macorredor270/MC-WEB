// Los bloques que se dibujan como entidades (cofres, carteles, estandartes y cabezas): modelos de cajas con las texturas de
// textures/entity/ del paquete activo, como en 1.8.
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

#include "assets/pack.h"
#include "client/camera.h"
#include "client/entity_renderer.h"
#include "client/terrain.h"
#include "data/blocks.h"

namespace mcw {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kSignScale = 2.0f / 3.0f / 16.0f;  // 1.8 dibuja carteles y estandartes a 2/3 del tamaño de un bloque de 16 píxeles

ModelBox box(glm::vec3 lo, glm::vec3 hi, glm::vec3 size, glm::vec2 uv) {
  ModelBox b;
  b.min = lo;
  b.max = hi;
  b.size = size;
  b.uv = uv;
  return b;
}

/// Cofre sencillo (`dbl` falso, 14 de ancho) o doble (30). Piezas: 0 base, 1 tapa (bisagra detrás), 2 y 3 pestillos.
const EntityModel& chestModel(bool dbl) {
  static const EntityModel single = [] {
    EntityModel m;
    m.texWidth = 64;
    m.texHeight = 64;
    m.parts.resize(3);
    m.parts[0].boxes.push_back(box({-7, 0, -7}, {7, 10, 7}, {14, 10, 14}, {0, 19}));
    m.parts[1].pivot = {0, 9, 7};
    m.parts[1].boxes.push_back(box({-7, 0, -14}, {7, 5, 0}, {14, 5, 14}, {0, 0}));
    m.parts[2].parent = 1;
    m.parts[2].boxes.push_back(box({-1, -2, -15}, {1, 2, -14}, {2, 4, 1}, {0, 0}));
    return m;
  }();
  static const EntityModel dbls = [] {
    EntityModel m;
    m.texWidth = 128;
    m.texHeight = 64;
    m.parts.resize(4);
    m.parts[0].boxes.push_back(box({-15, 0, -7}, {15, 10, 7}, {30, 10, 14}, {0, 19}));
    m.parts[1].pivot = {0, 9, 7};
    m.parts[1].boxes.push_back(box({-15, 0, -14}, {15, 5, 0}, {30, 5, 14}, {0, 0}));
    m.parts[2].parent = 1;
    m.parts[2].pivot = {-7, 0, 0};
    m.parts[2].boxes.push_back(box({-1, -2, -15}, {1, 2, -14}, {2, 4, 1}, {0, 0}));
    m.parts[3].parent = 1;
    m.parts[3].pivot = {7, 0, 0};
    m.parts[3].boxes.push_back(box({-1, -2, -15}, {1, 2, -14}, {2, 4, 1}, {0, 0}));
    return m;
  }();
  return dbl ? dbls : single;
}

/// Cartel: pieza 0 tabla, pieza 1 poste (los de pared lo ocultan).
const EntityModel& signModel(bool stick) {
  static const EntityModel withStick = [] {
    EntityModel m;
    m.texWidth = 64;
    m.texHeight = 32;
    m.parts.resize(2);
    m.parts[0].boxes.push_back(box({-12, 2, -1}, {12, 14, 1}, {24, 12, 2}, {0, 0}));
    m.parts[1].boxes.push_back(box({-1, -12, -1}, {1, 2, 1}, {2, 14, 2}, {0, 14}));
    return m;
  }();
  static const EntityModel boardOnly = [] {
    EntityModel m = withStick;
    m.parts.resize(1);
    return m;
  }();
  return stick ? withStick : boardOnly;
}

/// Estandarte: 0 palo, 1 travesaño, 2 tela (cuelga del travesaño y se mece un poco).
const EntityModel& bannerModel(bool pole) {
  static const EntityModel withPole = [] {
    EntityModel m;
    m.texWidth = 64;
    m.texHeight = 64;
    m.parts.resize(3);
    m.parts[0].boxes.push_back(box({-1, 0, -1}, {1, 42, 1}, {2, 42, 2}, {44, 0}));
    m.parts[1].boxes.push_back(box({-10, 42, -1}, {10, 44, 1}, {20, 2, 2}, {0, 42}));
    m.parts[2].pivot = {0, 44, -1.1f};
    m.parts[2].boxes.push_back(box({-10, -40, -1}, {10, 0, 0}, {20, 40, 1}, {0, 0}));
    return m;
  }();
  static const EntityModel clothOnly = [] {
    EntityModel m = withPole;
    m.parts.erase(m.parts.begin());  // sin palo: el travesaño pasa a ser la pieza 0 y la tela la 1
    m.parts[1].parent = -1;
    return m;
  }();
  return pole ? withPole : clothOnly;
}

/// Cabeza (8x8x8), con la capa de sombrero en las de jugador.
const EntityModel& skullModel(bool hat, float texH) {
  static EntityModel cache[4];
  EntityModel& m = cache[(hat ? 1 : 0) + (texH > 48.0f ? 2 : 0)];
  if (m.parts.empty()) {
    m.texWidth = 64;
    m.texHeight = texH > 48.0f ? 64.0f : 32.0f;
    m.parts.resize(1);
    m.parts[0].boxes.push_back(box({-4, 0, -4}, {4, 8, 4}, {8, 8, 8}, {0, 0}));
    if (hat) m.parts[0].boxes.push_back(box({-4.25f, -0.25f, -4.25f}, {4.25f, 8.25f, 4.25f}, {8, 8, 8}, {32, 0}));
  }
  return m;
}

/// Hacia dónde mira (ángulo del modelo) algo con la rotación de suelo `rot` (0..15) de un cartel, estandarte o cabeza.
float floorYaw(int rot) { return kPi - static_cast<float>(rot & 15) * kPi / 8.0f; }

/// Y lo que mira algo de pared con ese metadato (2 norte, 3 sur, 4 oeste, 5 este).
float wallYaw(int facing) {
  switch (facing) {
    case 3: return kPi;
    case 4: return kPi / 2;
    case 5: return -kPi / 2;
    default: return 0;
  }
}

}  // namespace

const EntityRenderer::Tex& EntityRenderer::signTexture(const SignText& text) {
  std::string key = "sign";
  for (const std::string& l : text.lines) key += '\n' + l;
  if (const auto it = dynamicTextures_.find(key); it != dynamicTextures_.end()) return it->second;
  if (!fontTried_) {
    fontTried_ = true;
    if (const auto f = packs_.readImage("assets/minecraft/textures/font/ascii.png")) font_.load(*f);
  }
  // La cara de delante de la tabla (24x12 píxeles) ampliada 6 veces: así cada píxel de la fuente (1/6 de uno de la tabla)
  // cabe entero, como el texto de 1.8
  const auto sign = packs_.readImage("assets/minecraft/textures/entity/sign.png");
  const int k = sign ? std::max(1, sign->width / 64) : 1;
  Image img(24 * 6, 12 * 6, 0xFFA07C46);
  if (sign)
    for (int y = 0; y < 12 * 6; y++)
      for (int x = 0; x < 24 * 6; x++) img.set(x, y, sign->get(2 * k + x * k / 6, 2 * k + y * k / 6));
  for (std::size_t i = 0; i < 4; i++) {
    const std::string& line = text.lines[i];
    if (line.empty()) continue;
    const int w = font_.width(line);
    font_.blit(img, (img.width - w) / 2, img.height / 2 + static_cast<int>(i) * 10 - 20, line, 0x000000);
  }
  Tex t = upload(img);
  t.w = 1;
  t.h = 1;
  return dynamicTextures_.emplace(key, t).first->second;
}

const EntityRenderer::Tex& EntityRenderer::bannerTexture(const BannerData& banner) {
  std::string key = "banner" + std::to_string(banner.base);
  for (const BannerPattern& p : banner.patterns) key += ',' + p.code + std::to_string(p.color);
  if (const auto it = dynamicTextures_.find(key); it != dynamicTextures_.end()) return it->second;
  Image img = packs_.readImage("assets/minecraft/textures/entity/banner_base.png").value_or(Image(64, 64, 0xFFFFFFFF));
  // Tinte de cada número de color (los números de los tintes: 0 negro ... 15 blanco)
  auto dye = [](int c) {
    const glm::vec3 v = fleeceColor(15 - (c & 15));
    return v;
  };
  const auto base = packs_.readImage("assets/minecraft/textures/entity/banner/base.png");
  auto layer = [&](const Image& mask, const glm::vec3& color, bool replaceBase) {
    for (int y = 0; y < std::min(img.height, mask.height); y++)
      for (int x = 0; x < std::min(img.width, mask.width); x++) {
        const u32 m = mask.get(x, y);
        if ((m >> 24) < 128) continue;  // fuera de la tela
        const float cover = replaceBase ? 1.0f : static_cast<float>((m >> 16) & 0xFF) / 255.0f;
        if (cover <= 0.0f) continue;
        const u32 cur = img.get(x, y);
        const float r = static_cast<float>((cur >> 16) & 0xFF), g = static_cast<float>((cur >> 8) & 0xFF), b = static_cast<float>(cur & 0xFF);
        // Base: la tela blanca de la textura por el color; dibujo: el color pleno, mezclado por lo que cubre
        const float nr = replaceBase ? r * color.r : color.r * 255.0f, ng = replaceBase ? g * color.g : color.g * 255.0f,
                    nb = replaceBase ? b * color.b : color.b * 255.0f;
        const auto mix = [&](float a, float c) { return static_cast<u32>(std::clamp(a + (c - a) * cover, 0.0f, 255.0f)); };
        img.set(x, y, 0xFF000000 | (mix(r, nr) << 16) | (mix(g, ng) << 8) | mix(b, nb));
      }
  };
  if (base) layer(*base, dye(banner.base), true);
  for (const BannerPattern& p : banner.patterns) {
    const std::string tex = bannerPatternTexture(p.code);
    if (tex.empty()) continue;
    if (const auto mask = packs_.readImage("assets/minecraft/textures/entity/banner/" + tex + ".png")) layer(*mask, dye(p.color), false);
  }
  Tex t = upload(img);
  t.w = 64;
  t.h = 64;
  return dynamicTextures_.emplace(key, t).first->second;
}

void EntityRenderer::drawBlockEntities(const Terrain& terrain, const TileEntities& tiles, const Camera& cam, double time, float dt,
                                       const LightFn& light, const FogParams& fog, double maxDist) {
  // Las texturas de carteles y estandartes se tiran por montones al empezar un frame, nunca a mitad (los lotes las usan por número)
  if (dynamicTextures_.size() > 192) {
    for (auto& [k, t] : dynamicTextures_) glDeleteTextures(1, &t.id);
    dynamicTextures_.clear();
  }
  struct Batch {
    GLuint tex;
    std::vector<Vertex> v;
  };
  std::vector<Batch> batches;
  auto batchFor = [&](GLuint id) -> std::vector<Vertex>& {
    for (Batch& b : batches)
      if (b.tex == id) return b.v;
    batches.push_back({id, {}});
    return batches.back().v;
  };
  const glm::vec3 fwd = cam.forward();
  auto open = [&](const glm::ivec3& p) {
    return std::find(openChests_.begin(), openChests_.end(), p) != openChests_.end();
  };
  const glm::vec4 noOverlay(0);

  terrain.forEachEntityBlock(cam.pos, maxDist, [&](const glm::ivec3& p, BlockState s) {
    const int id = stateId(s), meta = stateMeta(s);
    const glm::vec3 rel = glm::vec3(glm::dvec3(p) - cam.pos);
    if (glm::dot(rel + 0.5f, fwd) < -3.0f) return;  // detrás de la cámara
    const glm::dvec3 center = glm::dvec3(p) + 0.5;
    glm::vec3 lc = light(center + glm::dvec3(0, 0.4, 0));

    if (id == 54 || id == 146 || id == 130) {
      // Cofres. Dos del mismo tipo uno al lado del otro (a lo ancho de lo que miran) forman uno doble: lo dibuja el de menor coordenada
      const bool alongX = meta == 2 || meta == 3;
      const glm::ivec3 step = alongX ? glm::ivec3(1, 0, 0) : glm::ivec3(0, 0, 1);
      auto same = [&](const glm::ivec3& q) {
        const BlockState o = terrain.world().block(q.x, q.y, q.z);
        return stateId(o) == id && stateMeta(o) == meta;
      };
      const bool dbl = id != 130 && (same(p + step) || same(p - step));
      if (dbl && same(p - step)) return;
      const glm::ivec3 other = dbl ? p + step : p;
      const std::string name = id == 130 ? "entity/chest/ender.png"
                               : id == 146 ? (dbl ? "entity/chest/trapped_double.png" : "entity/chest/trapped.png")
                                           : (dbl ? "entity/chest/normal_double.png" : "entity/chest/normal.png");
      const Tex tex = [&] {
        Tex t = texture(name);
        t.w = dbl ? 128.0f : 64.0f;
        t.h = 64.0f;
        return t;
      }();
      float& lid = lids_[{p.x, p.y, p.z}];
      const float target = (open(p) || open(other)) ? 1.0f : 0.0f;
      lid += std::clamp(target - lid, -dt * 3.0f, dt * 3.0f);
      const float ease = 1.0f - std::pow(1.0f - lid, 3.0f);
      glm::dvec3 mid = center;
      if (dbl) mid += glm::dvec3(step) * 0.5;
      glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(mid - cam.pos) + glm::vec3(0, -0.5f, 0));
      m = glm::rotate(m, wallYaw(meta), glm::vec3(0, 1, 0));
      m = glm::scale(m, glm::vec3(1.0f / 16.0f));
      Pose pose;
      pose.rot.assign(chestModel(dbl).parts.size(), glm::vec3(0));
      pose.rot[1].x = ease * kPi / 2;
      appendModel(batchFor(tex.id), chestModel(dbl), pose, m, tex, lc, true, noOverlay);
      // El cofre de ender y los dobles dejan de lado el resto; el que sobra no se dibuja de nuevo
      return;
    }

    if (isSignBlock(id)) {
      const auto it = tiles.signs.find({p.x, p.y, p.z});
      const bool standing = id == kSignBlock;
      glm::mat4 m = glm::translate(glm::mat4(1.0f), rel + glm::vec3(0.5f, 0.5f, 0.5f));
      float yaw;
      if (standing) {
        yaw = floorYaw(meta);
      } else {
        yaw = wallYaw(meta);
      }
      m = glm::rotate(m, yaw, glm::vec3(0, 1, 0));
      if (!standing) m = glm::translate(m, glm::vec3(0, -0.3333f, 0.4375f));
      m = glm::scale(m, glm::vec3(kSignScale));
      const Tex& tex = texture("entity/sign.png");
      appendModel(batchFor(tex.id), signModel(standing), Pose{}, m, tex, lc, true, noOverlay);
      if (it != tiles.signs.end() && !it->second.empty()) {
        // La cara de delante de la tabla con el texto, un pelo por delante de la tabla
        const Tex& face = signTexture(it->second);
        std::vector<Vertex>& out = batchFor(face.id);
        const float k = 1.0f;
        const float x0 = -12, x1 = 12, y0 = 2, y1 = 14, z = -1.02f;
        const glm::vec3 c[4] = {{x1, y1, z}, {x1, y0, z}, {x0, y0, z}, {x0, y1, z}};
        const float us[4] = {0, 0, 1, 1}, vs[4] = {0, 1, 1, 0};
        // (los vértices van en el orden del modelo: arriba-izq, abajo-izq, abajo-der, arriba-der vistos desde delante)
        Vertex q[4];
        const u8 cr = static_cast<u8>(std::clamp(lc.r * k, 0.0f, 1.0f) * 255), cg = static_cast<u8>(std::clamp(lc.g * k, 0.0f, 1.0f) * 255),
                 cb = static_cast<u8>(std::clamp(lc.b * k, 0.0f, 1.0f) * 255);
        for (int j = 0; j < 4; j++) {
          const glm::vec4 w = m * glm::vec4(c[j], 1.0f);
          q[j] = {w.x, w.y, w.z, us[j], vs[j], cr, cg, cb, 255, 0, 0, 0, 0};
        }
        out.insert(out.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
      }
      return;
    }

    if (isBannerBlock(id)) {
      const auto it = tiles.banners.find({p.x, p.y, p.z});
      const BannerData data = it != tiles.banners.end() ? it->second : BannerData{};
      const bool standing = id == kStandingBannerBlock;
      glm::mat4 m = glm::translate(glm::mat4(1.0f), rel + glm::vec3(0.5f, standing ? 0.0f : 0.0f, 0.5f));
      if (standing) {
        m = glm::rotate(m, floorYaw(meta), glm::vec3(0, 1, 0));
      } else {
        m = glm::rotate(m, wallYaw(meta), glm::vec3(0, 1, 0));
        m = glm::translate(m, glm::vec3(0, 1.0f - 44.0f * kSignScale, 0.4375f));
      }
      m = glm::scale(m, glm::vec3(kSignScale));
      const Tex& tex = bannerTexture(data);
      Pose pose;
      pose.rot.assign(bannerModel(standing).parts.size(), glm::vec3(0));
      // La tela se mece despacio (un ciclo cada 5 segundos), cada estandarte con su fase
      const float phase = static_cast<float>(std::fmod(time / 5.0 + (p.x * 7 + p.z * 13) * 0.137, 1.0));
      pose.rot.back().x = (-0.0125f + 0.01f * std::cos(2.0f * kPi * phase)) * kPi;
      appendModel(batchFor(tex.id), bannerModel(standing), pose, m, tex, lc, true, noOverlay);
      return;
    }

    if (id == kSkullBlock) {
      const auto it = tiles.skulls.find({p.x, p.y, p.z});
      const SkullData data = it != tiles.skulls.end() ? it->second : SkullData{};
      glm::mat4 m = glm::translate(glm::mat4(1.0f), rel);
      if (meta == 1 || meta == 0) {
        m = glm::translate(m, glm::vec3(0.5f, 0.0f, 0.5f));
        m = glm::rotate(m, floorYaw(data.rot), glm::vec3(0, 1, 0));
      } else {
        static const glm::vec3 kWall[6] = {{}, {}, {0.5f, 0.25f, 0.74f}, {0.5f, 0.25f, 0.26f}, {0.74f, 0.25f, 0.5f}, {0.26f, 0.25f, 0.5f}};
        m = glm::translate(m, kWall[meta & 7]);
        m = glm::rotate(m, wallYaw(meta), glm::vec3(0, 1, 0));
      }
      m = glm::scale(m, glm::vec3(1.0f / 16.0f));
      static const char* kTex[5] = {"entity/skeleton/skeleton.png", "entity/skeleton/wither_skeleton.png", "entity/zombie/zombie.png",
                                    "entity/steve.png", "entity/creeper/creeper.png"};
      const int type = std::clamp<int>(data.type, 0, 4);
      Tex tex = type == 3 ? resolveSkin(SkinRef{}).tex : texture(kTex[type]);
      appendModel(batchFor(tex.id), skullModel(type == 3, tex.h), Pose{}, m, tex, lc, true, noOverlay);
    }
  });

  if (batches.empty()) return;
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  for (const Batch& b : batches) draw(b.v, b.tex, cam.viewProj, &fog);
}

}  // namespace mcw
