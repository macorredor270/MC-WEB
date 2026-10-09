#include "client/entity_models.h"

#include <cmath>
#include <numbers>

#include "assets/skin.h"

namespace mcw {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

/// Ayudante para escribir los modelos con las medidas de siempre: pivote y caja en el sistema
/// clásico de los modelos de entidades (y hacia abajo, origen a 24 píxeles del suelo). Aquí se
/// pasan al nuestro (y hacia arriba desde los pies): x -> -x, y -> 24 - y, z -> z. Es un giro de
/// 180º alrededor de Z, así que los ángulos en X e Y cambian de signo.
struct Build {
  EntityModel m;
  explicit Build(float texW = 64, float texH = 32) {
    m.texWidth = texW;
    m.texHeight = texH;
  }
  int part(float px, float py, float pz) {
    ModelPart p;
    p.pivot = {-px, 24.0f - py, pz};
    m.parts.push_back(p);
    return static_cast<int>(m.parts.size()) - 1;
  }
  void box(int part, float u, float v, float bx, float by, float bz, float w, float h, float d, float inflate = 0,
           bool mirror = false) {
    ModelBox b;
    b.min = {-(bx + w) - inflate, -(by + h) - inflate, bz - inflate};
    b.max = {-bx + inflate, -by + inflate, bz + d + inflate};
    b.size = {w, h, d};
    b.uv = {u, v};
    b.mirror = mirror;
    m.parts[part].boxes.push_back(b);
  }
  void rest(int part, float ax, float ay, float az) { m.parts[part].rot = {-ax, -ay, az}; }
  /// La última caja de la pieza pertenece a una capa que se puede ocultar.
  void layer(int part, u8 bit) { m.parts[part].boxes.back().layer = bit; }
};

MobModel biped(const std::string& texture, bool thinLimbs, float texH, bool hat) {
  MobModel mm;
  Build b(64, texH);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 0, 0);
  b.box(r.head, 0, 0, -4, -8, -4, 8, 8, 8);
  if (hat) b.box(r.head, 32, 0, -4, -8, -4, 8, 8, 8, 0.5f);
  r.body = b.part(0, 0, 0);
  b.box(r.body, 16, 16, -4, 0, -2, 8, 12, 4);
  const float lw = thinLimbs ? 2.0f : 4.0f;
  r.rightArm = b.part(-5, 2, 0);
  r.leftArm = b.part(5, 2, 0);
  r.legs[0] = b.part(thinLimbs ? -2.0f : -1.9f, 12, 0);  // derecha
  r.legs[1] = b.part(thinLimbs ? 2.0f : 1.9f, 12, 0);    // izquierda
  if (thinLimbs) {
    b.box(r.rightArm, 40, 16, -1, -2, -1, 2, 12, 2);
    b.box(r.leftArm, 40, 16, -1, -2, -1, 2, 12, 2, 0, true);
    b.box(r.legs[0], 0, 16, -1, 0, -1, 2, 12, 2);
    b.box(r.legs[1], 0, 16, -1, 0, -1, 2, 12, 2, 0, true);
  } else {
    b.box(r.rightArm, 40, 16, -3, -2, -2, lw, 12, 4);
    b.box(r.leftArm, 40, 16, -1, -2, -2, lw, 12, 4, 0, true);
    b.box(r.legs[0], 0, 16, -2, 0, -2, 4, 12, 4);
    b.box(r.legs[1], 0, 16, -2, 0, -2, 4, 12, 4, 0, true);
  }
  mm.model = b.m;
  mm.texture = texture;
  return mm;
}

/// El jugador: la disposición de skin de 1.8. Cada brazo y pierna tiene su sitio en la textura (la
/// izquierda ya no es el espejo de la derecha) y encima va la segunda capa, un poco más grande.
MobModel makePlayer(bool slim) {
  MobModel mm;
  Build b(64, 64);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 0, 0);
  b.box(r.head, 0, 0, -4, -8, -4, 8, 8, 8);
  b.box(r.head, 32, 0, -4, -8, -4, 8, 8, 8, 0.5f);
  b.layer(r.head, kSkinHat);
  r.body = b.part(0, 0, 0);
  b.box(r.body, 16, 16, -4, 0, -2, 8, 12, 4);
  b.box(r.body, 16, 32, -4, 0, -2, 8, 12, 4, 0.25f);
  b.layer(r.body, kSkinJacket);
  r.rightArm = b.part(-5, 2, 0);
  r.leftArm = b.part(5, 2, 0);
  r.legs[0] = b.part(-1.9f, 12, 0);  // derecha
  r.legs[1] = b.part(1.9f, 12, 0);   // izquierda
  const float armW = slim ? 3.0f : 4.0f, rightX = slim ? -2.0f : -3.0f;
  b.box(r.rightArm, 40, 16, rightX, -2, -2, armW, 12, 4);
  b.box(r.rightArm, 40, 32, rightX, -2, -2, armW, 12, 4, 0.25f);
  b.layer(r.rightArm, kSkinRightSleeve);
  b.box(r.leftArm, 32, 48, -1, -2, -2, armW, 12, 4);
  b.box(r.leftArm, 48, 48, -1, -2, -2, armW, 12, 4, 0.25f);
  b.layer(r.leftArm, kSkinLeftSleeve);
  b.box(r.legs[0], 0, 16, -2, 0, -2, 4, 12, 4);
  b.box(r.legs[0], 0, 32, -2, 0, -2, 4, 12, 4, 0.25f);
  b.layer(r.legs[0], kSkinRightPants);
  b.box(r.legs[1], 16, 48, -2, 0, -2, 4, 12, 4);
  b.box(r.legs[1], 0, 48, -2, 0, -2, 4, 12, 4, 0.25f);
  b.layer(r.legs[1], kSkinLeftPants);
  mm.model = b.m;
  mm.texture = slim ? "entity/alex.png" : "entity/steve.png";
  return mm;
}

/// Armadura del jugador: las mismas piezas que `makePlayer`, con cajas más grandes y la disposición de
/// textura de 64x32 de las armaduras. La capa 1 lleva casco, pechera y botas; la 2, los pantalones.
MobModel makeArmor(int layer) {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  const float g = layer == 1 ? 1.0f : 0.5f;
  r.head = b.part(0, 0, 0);
  if (layer == 1) {
    b.box(r.head, 0, 0, -4, -8, -4, 8, 8, 8, g);
    b.layer(r.head, kArmorHelmet);
  }
  r.body = b.part(0, 0, 0);
  b.box(r.body, 16, 16, -4, 0, -2, 8, 12, 4, g);
  b.layer(r.body, layer == 1 ? kArmorChest : kArmorLeggings);
  r.rightArm = b.part(-5, 2, 0);
  r.leftArm = b.part(5, 2, 0);
  if (layer == 1) {
    b.box(r.rightArm, 40, 16, -3, -2, -2, 4, 12, 4, g);
    b.layer(r.rightArm, kArmorChest);
    b.box(r.leftArm, 40, 16, -1, -2, -2, 4, 12, 4, g, true);
    b.layer(r.leftArm, kArmorChest);
  }
  r.legs[0] = b.part(-1.9f, 12, 0);
  r.legs[1] = b.part(1.9f, 12, 0);
  b.box(r.legs[0], 0, 16, -2, 0, -2, 4, 12, 4, g);
  b.layer(r.legs[0], layer == 1 ? kArmorBoots : kArmorLeggings);
  b.box(r.legs[1], 0, 16, -2, 0, -2, 4, 12, 4, g, true);
  b.layer(r.legs[1], layer == 1 ? kArmorBoots : kArmorLeggings);
  mm.model = b.m;
  return mm;
}

/// Cuadrúpedo (cerdo, vaca, oveja): patas de `legH` píxeles.
void quadLegs(Build& b, ModelRig& r, float legH, float x, float zBack, float zFront) {
  const float y = 24.0f - legH;
  r.legs[0] = b.part(-x, y, zBack);
  r.legs[1] = b.part(x, y, zBack);
  r.legs[2] = b.part(-x, y, zFront);
  r.legs[3] = b.part(x, y, zFront);
  for (int i = 0; i < 4; i++) b.box(r.legs[i], 0, 16, -2, 0, -2, 4, legH, 4);
}

MobModel makePig() {
  MobModel mm;
  Build b;
  ModelRig& r = mm.rig;
  r.head = b.part(0, 12, -6);
  b.box(r.head, 0, 0, -4, -4, -8, 8, 8, 8);
  b.box(r.head, 16, 16, -2, 0, -9, 4, 3, 1);  // hocico
  r.body = b.part(0, 11, 2);
  b.box(r.body, 28, 8, -5, -10, -7, 10, 16, 8);
  b.rest(r.body, kPi / 2, 0, 0);
  quadLegs(b, r, 6, 3, 7, -5);
  mm.model = b.m;
  mm.texture = "entity/pig/pig.png";
  return mm;
}

MobModel makeCow() {
  MobModel mm;
  Build b;
  ModelRig& r = mm.rig;
  r.head = b.part(0, 4, -8);
  b.box(r.head, 0, 0, -4, -4, -6, 8, 8, 6);
  b.box(r.head, 22, 0, -5, -5, -4, 1, 3, 1);  // cuernos
  b.box(r.head, 22, 0, 4, -5, -4, 1, 3, 1);
  r.body = b.part(0, 5, 2);
  b.box(r.body, 18, 4, -6, -10, -7, 12, 18, 10);
  b.box(r.body, 52, 0, -2, 2, -8, 4, 6, 1);  // ubre
  b.rest(r.body, kPi / 2, 0, 0);
  quadLegs(b, r, 12, 4, 7, -6);
  mm.model = b.m;
  mm.texture = "entity/cow/cow.png";
  return mm;
}

MobModel makeSheep() {
  MobModel mm;
  ModelRig& r = mm.rig;
  // Cuerpo esquilado
  {
    Build b;
    r.head = b.part(0, 6, -8);
    b.box(r.head, 0, 0, -3, -4, -6, 6, 6, 8);
    r.body = b.part(0, 5, 2);
    b.box(r.body, 28, 8, -4, -10, -7, 8, 16, 6);
    b.rest(r.body, kPi / 2, 0, 0);
    quadLegs(b, r, 12, 3, 7, -5);
    mm.model = b.m;
  }
  // Lana: mismas piezas, cajas algo más grandes (textura sheep_fur, teñida con el color)
  {
    Build b;
    const int head = b.part(0, 6, -8);
    b.box(head, 0, 0, -3, -4, -4, 6, 6, 6, 0.6f);
    const int body = b.part(0, 5, 2);
    b.box(body, 28, 8, -4, -10, -7, 8, 16, 6, 1.75f);
    b.rest(body, kPi / 2, 0, 0);
    const float y = 12;
    const int legs[4] = {b.part(-3, y, 7), b.part(3, y, 7), b.part(-3, y, -5), b.part(3, y, -5)};
    for (int l : legs) b.box(l, 0, 16, -2, 0, -2, 4, 6, 4, 0.5f);
    mm.overlayModel = b.m;
  }
  mm.texture = "entity/sheep/sheep.png";
  mm.overlay = "entity/sheep/sheep_fur.png";
  return mm;
}

MobModel makeChicken() {
  MobModel mm;
  Build b;
  ModelRig& r = mm.rig;
  r.head = b.part(0, 15, -4);
  b.box(r.head, 0, 0, -2, -6, -2, 4, 6, 3);
  b.box(r.head, 14, 0, -2, -4, -4, 4, 2, 2);  // pico
  b.box(r.head, 14, 4, -1, -2, -3, 2, 2, 2);  // barba
  r.body = b.part(0, 16, 0);
  b.box(r.body, 0, 9, -3, -4, -3, 6, 8, 6);
  b.rest(r.body, kPi / 2, 0, 0);
  r.legs[0] = b.part(-2, 19, 1);
  r.legs[1] = b.part(1, 19, 1);
  b.box(r.legs[0], 26, 0, -1, 0, -3, 3, 5, 3);
  b.box(r.legs[1], 26, 0, -1, 0, -3, 3, 5, 3);
  r.rightWing = b.part(-4, 13, 0);
  b.box(r.rightWing, 24, 13, 0, 0, -3, 1, 4, 6);
  r.leftWing = b.part(4, 13, 0);
  b.box(r.leftWing, 24, 13, -1, 0, -3, 1, 4, 6);
  mm.model = b.m;
  mm.texture = "entity/chicken.png";
  return mm;
}

MobModel makeCreeper() {
  MobModel mm;
  Build b;
  ModelRig& r = mm.rig;
  r.head = b.part(0, 6, 0);
  b.box(r.head, 0, 0, -4, -8, -4, 8, 8, 8);
  r.body = b.part(0, 6, 0);
  b.box(r.body, 16, 16, -4, 0, -2, 8, 12, 4);
  r.legs[0] = b.part(-2, 18, 4);
  r.legs[1] = b.part(2, 18, 4);
  r.legs[2] = b.part(-2, 18, -4);
  r.legs[3] = b.part(2, 18, -4);
  for (int i = 0; i < 4; i++) b.box(r.legs[i], 0, 16, -2, 0, -2, 4, 6, 4);
  mm.model = b.m;
  mm.texture = "entity/creeper/creeper.png";
  return mm;
}

MobModel makeSpider() {
  MobModel mm;
  Build b;
  ModelRig& r = mm.rig;
  r.head = b.part(0, 15, -3);
  b.box(r.head, 32, 4, -4, -4, -8, 8, 8, 8);
  const int neck = b.part(0, 15, 0);
  b.box(neck, 0, 0, -3, -3, -3, 6, 6, 6);
  r.body = b.part(0, 15, 9);
  b.box(r.body, 0, 12, -5, -4, -6, 10, 8, 12);
  // 8 patas: pares (derecha, izquierda) de atrás hacia delante
  const float zs[4] = {2, 1, 0, -1};
  for (int i = 0; i < 4; i++) {
    r.legs[i * 2] = b.part(-4, 15, zs[i]);
    b.box(r.legs[i * 2], 18, 0, -15, -1, -1, 16, 2, 2);
    r.legs[i * 2 + 1] = b.part(4, 15, zs[i]);
    b.box(r.legs[i * 2 + 1], 18, 0, -1, -1, -1, 16, 2, 2);
  }
  // Reposo: patas abiertas hacia abajo y en abanico
  const float down[4] = {kPi / 4, kPi / 4 * 0.74f, kPi / 4 * 0.74f, kPi / 4};
  const float fan[4] = {kPi / 4, kPi / 8, -kPi / 8, -kPi / 4};
  for (int i = 0; i < 4; i++) {
    b.rest(r.legs[i * 2], 0, fan[i], -down[i]);
    b.rest(r.legs[i * 2 + 1], 0, -fan[i], down[i]);
  }
  mm.model = b.m;
  mm.texture = "entity/spider/spider.png";
  mm.overlay = "entity/spider_eyes.png";
  mm.overlayModel = b.m;
  mm.overlayEmissive = true;
  return mm;
}

const std::array<MobModel, static_cast<int>(MobType::Count)>& models() {
  static const std::array<MobModel, static_cast<int>(MobType::Count)> all = [] {
    std::array<MobModel, static_cast<int>(MobType::Count)> a;
    a[static_cast<int>(MobType::Pig)] = makePig();
    a[static_cast<int>(MobType::Cow)] = makeCow();
    a[static_cast<int>(MobType::Sheep)] = makeSheep();
    a[static_cast<int>(MobType::Chicken)] = makeChicken();
    a[static_cast<int>(MobType::Zombie)] = biped("entity/zombie/zombie.png", false, 64, true);
    a[static_cast<int>(MobType::Skeleton)] = biped("entity/skeleton/skeleton.png", true, 32, false);
    a[static_cast<int>(MobType::Creeper)] = makeCreeper();
    a[static_cast<int>(MobType::Spider)] = makeSpider();
    return a;
  }();
  return all;
}

}  // namespace

const MobModel& mobModel(MobType t) { return models()[std::min(static_cast<int>(t), static_cast<int>(MobType::Count) - 1)]; }

const MobModel& playerModel(bool slim) {
  static const MobModel wide = makePlayer(false), thin = makePlayer(true);
  return slim ? thin : wide;
}

const MobModel& armorModel(int layer) {
  static const MobModel one = makeArmor(1), two = makeArmor(2);
  return layer == 1 ? one : two;
}

Pose poseFor(MobType t, const MobModel& mm, float swing, float amount, float headYawRel, float pitch, float age, bool onGround,
             int eatGrassTicks) {
  Pose p;
  p.rot.assign(mm.model.parts.size(), glm::vec3(0));
  const ModelRig& r = mm.rig;
  const float walk = std::cos(swing * 0.6662f) * 1.4f * amount;
  if (r.head >= 0) p.rot[r.head] = {pitch, headYawRel, 0};

  switch (t) {
    case MobType::Pig:
    case MobType::Cow:
    case MobType::Sheep:
    case MobType::Creeper:
      // Patas en diagonal: delante-izquierda con detrás-derecha
      p.rot[r.legs[0]].x = walk;
      p.rot[r.legs[1]].x = -walk;
      p.rot[r.legs[2]].x = -walk;
      p.rot[r.legs[3]].x = walk;
      if (t == MobType::Sheep && eatGrassTicks > 0) {
        // Comiendo: baja la cabeza y la mueve
        const float k = eatGrassTicks >= 4 && eatGrassTicks <= 36 ? 1.0f : (eatGrassTicks < 4 ? eatGrassTicks / 4.0f : (40 - eatGrassTicks) / 4.0f);
        p.rot[r.head].x = -(0.63f + 0.22f * std::sin(eatGrassTicks * 0.9f)) * k;
      }
      break;
    case MobType::Chicken: {
      p.rot[r.legs[0]].x = walk;
      p.rot[r.legs[1]].x = -walk;
      // Aletea en el aire
      const float flap = onGround ? 0.0f : (std::sin(age * 1.6f) + 1.0f) * 0.7f;
      p.rot[r.rightWing].z = flap;
      p.rot[r.leftWing].z = -flap;
      break;
    }
    case MobType::Zombie:
    case MobType::Skeleton: {
      p.rot[r.legs[0]].x = walk;
      p.rot[r.legs[1]].x = -walk;
      // Brazos hacia delante (zombi; el esqueleto apunta con el arco), con un leve vaivén
      const float sway = std::sin(age * 0.067f) * 0.05f, side = std::cos(age * 0.09f) * 0.05f + 0.05f;
      p.rot[r.rightArm] = {kPi / 2 + sway, 0, side};
      p.rot[r.leftArm] = {kPi / 2 - sway, 0, -side};
      break;
    }
    case MobType::Spider: {
      // Las patas se mueven en parejas alternas
      for (int i = 0; i < 4; i++) {
        const float ph = i * kPi / 2;
        const float yaw = std::cos(swing * 1.3324f + ph) * 0.4f * amount;
        const float lift = std::abs(std::sin(swing * 0.6662f + ph)) * 0.4f * amount;
        p.rot[r.legs[i * 2]] += glm::vec3(0, -yaw, lift);      // derecha: subir es acercarse a 0
        p.rot[r.legs[i * 2 + 1]] += glm::vec3(0, yaw, -lift);  // izquierda: al revés
      }
      break;
    }
    default: break;
  }
  return p;
}

}  // namespace mcw
