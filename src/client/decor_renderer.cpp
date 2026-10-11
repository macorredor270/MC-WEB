// Marcos, cuadros y soportes para armadura. Medidas de 1.8 en píxeles (1/16 de bloque); dibujo propio.
#include <algorithm>
#include <cmath>

#include "client/camera.h"
#include "client/entity_renderer.h"
#include "game/armor.h"

namespace mcw {
namespace {
constexpr float kPi = 3.14159265358979f;
}

std::vector<BlockDraw> EntityRenderer::drawDecor(const std::vector<Decor>& decors, const Camera& cam, float partial, const LightFn& light,
                                                 const FogParams& fog) {
  std::vector<BlockDraw> items;
  if (decors.empty()) return items;
  const Tex& wood = texture("blocks/planks_birch.png");
  const Tex& backTex = texture("blocks/itemframe_background.png");
  const Tex& paint = texture("painting/paintings_kristoffer_zetterstrand.png");
  const Tex& stand = texture("entity/armorstand/wood.png");
  // Las coordenadas de textura de marcos y cuadros van en píxeles de 1.8 (16x16 el bloque y la tela, 256x256 el atlas de cuadros)
  auto atlas = [&](const Tex& t) { return t.id == paint.id ? 256.0f : 16.0f; };
  std::vector<Vertex> vWood, vBack, vPaint, vStand;
  std::unordered_map<GLuint, std::vector<Vertex>> armorBatches;

  for (const Decor& d : decors) {
    if (d.dead) continue;
    const glm::dvec3 where = d.kind == DecorKind::ArmorStand ? d.pos : d.center();
    const glm::vec3 rel(where - cam.pos);
    if (glm::dot(rel, rel) > 80.0f * 80.0f) continue;

    if (d.kind == DecorKind::ArmorStand) {
      const glm::vec3 lc = light(d.pos + glm::dvec3(0, 1.0, 0));
      glm::mat4 m = glm::translate(glm::mat4(1.0f), rel);
      m = glm::rotate(m, d.yaw, glm::vec3(0, 1, 0));
      if (d.hurtTime > 0) m = glm::rotate(m, std::sin((static_cast<float>(d.hurtTime) - partial) * 1.7f) * 0.06f, glm::vec3(0, 0, 1));
      const float k = d.small ? 0.5f : 1.0f;
      m = glm::scale(m, glm::vec3(k / 16.0f));
      u32 layers = ~0u & ~static_cast<u32>(kStandArms | kStandBase);
      if (d.arms) layers |= kStandArms;
      if (!d.noBase) layers |= kStandBase;
      const EntityModel& model = armorStandModel();
      Pose pose;
      pose.rot.assign(model.parts.size(), glm::vec3(0));
      appendModel(vStand, model, pose, m, stand, lc, true, glm::vec4(0), layers);
      // La armadura puesta (el modelo de jugador a 0,9375: el soporte mide 30 píxeles como él)
      std::array<i16, 4> armor{};
      bool any = false;
      for (int i = 0; i < 4; i++) {
        const ItemStack& s = d.equip[static_cast<std::size_t>(i + 1)];
        if (!s.empty() && isArmor(s.id)) {
          armor[static_cast<std::size_t>(i)] = s.id;
          any = true;
        }
      }
      if (any) {
        const MobModel& mm = playerModel(false);
        Pose pp;
        pp.rot.assign(mm.model.parts.size(), glm::vec3(0));
        glm::mat4 am = glm::translate(glm::mat4(1.0f), rel);
        am = glm::rotate(am, d.yaw, glm::vec3(0, 1, 0));
        am = glm::scale(am, glm::vec3(0.9375f * k / 16.0f));
        appendArmor(armorBatches, armor, pp, am, lc, glm::vec4(0));
      }
      // Casco que no es de armadura (calabaza o cabeza): se dibuja encima, como bloque
      const ItemStack& head = d.equip[4];
      if (!head.empty() && !isArmor(head.id)) {
        BlockDraw b;
        b.block = head;
        b.m = glm::scale(glm::translate(glm::rotate(glm::translate(glm::mat4(1.0f), rel), d.yaw, glm::vec3(0, 1, 0)),
                                         glm::vec3(0, (1.5f + 0.25f) * k, 0)),
                         glm::vec3(0.5f * k));
        b.light = lc;
        items.push_back(b);
      }
      continue;
    }

    // Marco o cuadro: un sistema local con +Z hacia quien mira, X a su derecha y Y arriba; en píxeles, con la pared en z = -0.5
    const glm::ivec3 n = decorNormal(d.dir);
    const float theta = std::atan2(static_cast<float>(n.x), static_cast<float>(n.z));
    glm::mat4 m = glm::translate(glm::mat4(1.0f), rel);
    m = glm::rotate(m, theta, glm::vec3(0, 1, 0));
    m = glm::scale(m, glm::vec3(1.0f / 16.0f));
    const glm::vec3 lc = light(where);
    const u8 cr = static_cast<u8>(std::clamp(lc.r, 0.0f, 1.0f) * 255), cg = static_cast<u8>(std::clamp(lc.g, 0.0f, 1.0f) * 255),
             cb = static_cast<u8>(std::clamp(lc.b, 0.0f, 1.0f) * 255);
    auto quad = [&](std::vector<Vertex>& out, const Tex& tex, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec4 uv,
                    float shade) {
      const glm::vec3 ps[4] = {p0, p1, p2, p3};
      const float us[4] = {uv.x, uv.x, uv.z, uv.z}, vs[4] = {uv.y, uv.w, uv.w, uv.y};
      const u8 r = static_cast<u8>(cr * shade), g = static_cast<u8>(cg * shade), b = static_cast<u8>(cb * shade);
      Vertex q[4];
      for (int j = 0; j < 4; j++) {
        const glm::vec4 p = m * glm::vec4(ps[j], 1.0f);
        q[j] = {p.x, p.y, p.z, us[j] / atlas(tex), vs[j] / atlas(tex), r, g, b, 255, 0, 0, 0, 0};
      }
      out.insert(out.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
    };
    // Una caja entera [lo, hi] con la misma región de textura en todas sus caras
    auto box = [&](std::vector<Vertex>& out, const Tex& tex, glm::vec3 lo, glm::vec3 hi, glm::vec4 uv, bool front = true) {
      if (front) quad(out, tex, {lo.x, hi.y, hi.z}, {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, uv, 1.0f);  // delante (+Z)
      quad(out, tex, {hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, uv, 0.8f);           // detrás
      quad(out, tex, {lo.x, hi.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, uv, 0.8f);           // izquierda
      quad(out, tex, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, uv, 0.8f);           // derecha
      quad(out, tex, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, uv, 1.0f);           // arriba
      quad(out, tex, {lo.x, lo.y, hi.z}, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, uv, 0.6f);           // abajo
    };

    if (d.kind == DecorKind::ItemFrame) {
      const glm::vec4 uv(2, 13, 14, 14);  // un listón de la madera
      box(vWood, wood, {-6, -6, -0.5f}, {6, -5, 0.5f}, uv);
      box(vWood, wood, {-6, 5, -0.5f}, {6, 6, 0.5f}, uv);
      box(vWood, wood, {-6, -5, -0.5f}, {-5, 5, 0.5f}, uv);
      box(vWood, wood, {5, -5, -0.5f}, {6, 5, 0.5f}, uv);
      quad(vBack, backTex, {-5, 5, 0}, {-5, -5, 0}, {5, -5, 0}, {5, 5, 0}, {3, 3, 13, 13}, 1.0f);
      if (!d.item.empty()) {
        BlockDraw b;
        b.block = d.item;
        // El objeto a medio bloque, un poco delante del fondo y girado en pasos de 45º
        glm::mat4 im = glm::translate(glm::mat4(1.0f), rel);
        im = glm::rotate(im, theta, glm::vec3(0, 1, 0));
        im = glm::translate(im, glm::vec3(0, 0, 0.5f / 16.0f));
        im = glm::rotate(im, -static_cast<float>(d.rotation) * kPi / 4.0f, glm::vec3(0, 0, 1));
        im = glm::scale(im, glm::vec3(0.5f));
        b.m = im;
        b.light = lc;
        items.push_back(b);
      }
    } else {
      const PaintingMotive& pm = paintingMotives()[std::min<std::size_t>(d.motive, paintingMotives().size() - 1)];
      const float hw = pm.w * 8.0f, hh = pm.h * 8.0f;  // medio lienzo en píxeles
      const glm::vec4 front(pm.u, pm.v, pm.u + pm.w * 16.0f, pm.v + pm.h * 16.0f);
      quad(vPaint, paint, {-hw, hh, 0.5f}, {-hw, -hh, 0.5f}, {hw, -hh, 0.5f}, {hw, hh, 0.5f}, front, 1.0f);
      box(vPaint, paint, {-hw, -hh, -0.5f}, {hw, hh, 0.5f}, {192, 0, 208, 16}, false);  // el canto y el reverso
    }
  }
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  if (!vWood.empty()) draw(vWood, wood.id, cam.viewProj, &fog);
  if (!vBack.empty()) draw(vBack, backTex.id, cam.viewProj, &fog);
  if (!vPaint.empty()) draw(vPaint, paint.id, cam.viewProj, &fog);
  if (!vStand.empty()) draw(vStand, stand.id, cam.viewProj, &fog);
  for (const auto& [t, av] : armorBatches) draw(av, t, cam.viewProj, &fog);
  return items;
}

}  // namespace mcw
