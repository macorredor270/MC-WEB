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
  // La silla: las mismas piezas un poco más grandes (la textura es transparente salvo la silla)
  {
    Build o;
    const int head = o.part(0, 12, -6);
    o.box(head, 0, 0, -4, -4, -8, 8, 8, 8, 0.5f);
    o.box(head, 16, 16, -2, 0, -9, 4, 3, 1, 0.5f);
    const int body = o.part(0, 11, 2);
    o.box(body, 28, 8, -5, -10, -7, 10, 16, 8, 0.5f);
    o.rest(body, kPi / 2, 0, 0);
    const float y = 18;
    const int legs[4] = {o.part(-3, y, 7), o.part(3, y, 7), o.part(-3, y, -5), o.part(3, y, -5)};
    for (int l : legs) o.box(l, 0, 16, -2, 0, -2, 4, 6, 4, 0.5f);
    mm.overlayModel = o.m;
  }
  mm.texture = "entity/pig/pig.png";
  mm.overlay = "entity/pig/pig_saddle.png";
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

MobModel makeSpider(const char* texture = "entity/spider/spider.png") {
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
  mm.texture = texture;
  mm.overlay = "entity/spider_eyes.png";
  mm.overlayModel = b.m;
  mm.overlayEmissive = true;
  return mm;
}


/// Enderman: patas y brazos de 30 píxeles; la capa de ojos y boca (se ve a oscuras) comparte esqueleto.
MobModel makeEnderman() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  // Medidas del modelo clásico con el origen 4 píxeles más arriba para que los pies queden en el suelo (y hacia abajo)
  r.head = b.part(0, -18, 0);
  b.box(r.head, 0, 0, -4, -8, -4, 8, 8, 8);
  b.box(r.head, 0, 16, -4, -8, -4, 8, 8, 8, -0.5f);  // mandíbula (con la boca abierta se ve)
  r.body = b.part(0, -18, 0);
  b.box(r.body, 32, 16, -4, 0, -2, 8, 12, 4);
  r.rightArm = b.part(-5, -16, 0);
  b.box(r.rightArm, 56, 0, -1, -2, -1, 2, 30, 2);
  r.leftArm = b.part(5, -16, 0);
  b.box(r.leftArm, 56, 0, -1, -2, -1, 2, 30, 2, 0, true);
  r.legs[0] = b.part(-2, -6, 0);
  b.box(r.legs[0], 56, 0, -1, 0, -1, 2, 30, 2);
  r.legs[1] = b.part(2, -6, 0);
  b.box(r.legs[1], 56, 0, -1, 0, -1, 2, 30, 2, 0, true);
  mm.model = b.m;
  mm.texture = "entity/enderman/enderman.png";
  mm.overlay = "entity/enderman/enderman_eyes.png";
  mm.overlayModel = b.m;
  mm.overlayEmissive = true;
  return mm;
}

/// Ghast: un cubo de 16 con nueve tentáculos colgando; a escala 4 mide lo que su caja de colisión.
MobModel makeGhast() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 16, 0);
  b.box(r.head, 0, 0, -8, -8, -8, 16, 16, 16);
  r.body = r.head;
  for (int i = 0; i < 9; i++) {
    const float x = (i % 3 - 1) * 5.0f, z = (i / 3 - 1) * 5.0f;
    const int t = b.part(x, 24, z);
    b.box(t, 0, 0, -1, 0, -1, 2, 8 + (i * 5 % 4), 2);
    r.extra.push_back(t);
  }
  mm.model = b.m;
  mm.texture = "entity/ghast/ghast.png";
  mm.altTexture = "entity/ghast/ghast_shooting.png";
  mm.scale = 4.0f;
  return mm;
}

/// Blaze: la cabeza y doce varillas en tres anillos que giran.
MobModel makeBlaze() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  r.head = b.part(0, -4, 0);
  b.box(r.head, 0, 0, -4, -8, -4, 8, 8, 8);
  const float ringY[3] = {18.0f, 10.0f, 2.0f}, ringR[3] = {9.0f, 7.0f, 5.0f};
  for (int ring = 0; ring < 3; ring++)
    for (int k = 0; k < 4; k++) {
      const float a = k * kPi / 2 + ring * kPi / 4;
      const int p = b.part(0, ringY[ring], 0);
      b.box(p, 0, 16, ringR[ring] * std::cos(a) - 1, 0, ringR[ring] * std::sin(a) - 1, 2, 8, 2);
      r.extra.push_back(p);
    }
  mm.model = b.m;
  mm.texture = "entity/blaze.png";
  return mm;
}

/// Slime y cubo de magma: el núcleo, y alrededor un cubo más grande semitransparente (slime).
MobModel makeSlime() {
  MobModel mm;
  {
    Build b(64, 32);
    const int core = b.part(0, 24, 0);
    b.box(core, 0, 16, -3, -8, -3, 6, 6, 6);        // cuerpo interior
    b.box(core, 32, 0, -3.3f, -7, -3.5f, 2, 2, 2);  // ojos
    b.box(core, 32, 4, 1.3f, -7, -3.5f, 2, 2, 2);
    b.box(core, 32, 8, 0, -4, -3.5f, 1, 1, 1);      // boca
    mm.model = b.m;
  }
  {
    Build b(64, 32);
    const int outer = b.part(0, 24, 0);
    b.box(outer, 0, 0, -4, -8.5f, -4, 8, 8, 8);
    mm.overlayModel = b.m;
  }
  mm.texture = "entity/slime/slime.png";
  mm.overlay = "entity/slime/slime.png";
  mm.overlayBlend = true;
  return mm;
}

MobModel makeMagmaCube() {
  MobModel mm;
  Build b(64, 32);
  const int core = b.part(0, 24, 0);
  b.box(core, 24, 18, -2, -6, -2, 4, 4, 4);  // núcleo
  // Ocho lonchas apiladas (cada una con su fila de textura)
  for (int i = 0; i < 8; i++) {
    const int s = b.part(0, 24, 0);
    b.box(s, 0, i * 4 >= 28 ? 28 : i * 4, -4, -8.0f + i, -4, 8, 1, 8);
  }
  mm.model = b.m;
  mm.texture = "entity/slime/magmacube.png";
  return mm;
}

/// Lepisma: siete segmentos en fila y tres capas de "alas" por encima.
MobModel makeSilverfish() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  const int sizes[7][3] = {{3, 2, 2}, {4, 3, 2}, {6, 4, 3}, {3, 3, 3}, {2, 2, 3}, {2, 2, 2}, {1, 1, 2}};
  const int uv[7][2] = {{0, 0}, {0, 4}, {0, 9}, {0, 16}, {0, 22}, {11, 0}, {13, 4}};
  float z = -3.5f;
  for (int i = 0; i < 7; i++) {
    const int w = sizes[i][0], h = sizes[i][1], d = sizes[i][2];
    const int p = b.part(0, 24 - h, z);
    b.box(p, uv[i][0], uv[i][1], -w / 2.0f, 0, -d / 2.0f, w, h, d);
    r.extra.push_back(p);
    if (i < 6) z += (d + sizes[i + 1][2]) / 2.0f;
  }
  const int wing[3][6] = {{20, 0, 10, 8, 3, 0}, {20, 11, 6, 4, 3, 2}, {20, 18, 6, 5, 2, 4}};
  for (int k = 0; k < 3; k++) {
    const int seg = r.extra[static_cast<std::size_t>(k == 0 ? 2 : k == 1 ? 4 : 1)];
    (void)seg;
    const int w = wing[k][2], h = wing[k][3], d = wing[k][4];
    const int p = b.part(0, 24 - h, k == 0 ? 0.0f : k == 1 ? 2.0f : -1.0f);
    b.box(p, wing[k][0], wing[k][1], -w / 2.0f, 0, -d / 2.0f, w, h, d);
    r.extra.push_back(p);
  }
  mm.model = b.m;
  mm.texture = "entity/silverfish.png";
  return mm;
}

/// El dragón del End: cuerpo, cuello de cinco piezas, cabeza con mandíbula, cola de doce, alas de dos tramos y cuatro patas.
/// Las medidas siguen el modelo clásico (y hacia abajo, el frente es -Z); a escala 1,6 mide lo que su caja de colisión.
MobModel makeDragon() {
  MobModel mm;
  Build b(256, 256);
  ModelRig& r = mm.rig;
  r.body = b.part(0, 0, 0);
  b.box(r.body, 0, 0, -12, -12, -8, 24, 24, 48);
  for (int i = 0; i < 3; i++) b.box(r.body, 220, 53, -1, -18, 2 + i * 14, 2, 6, 12);
  for (int i = 0; i < 5; i++) {  // cuello
    const int p = b.part(0, -2.0f - i, -12.0f - 9.0f * i);
    b.box(p, 192, 104, -5, -5, -5, 10, 10, 10);
    b.box(p, 48, 0, -1, -9, -3, 2, 4, 6);
    r.extra.push_back(p);
  }
  r.head = b.part(0, -6, -60);
  b.box(r.head, 112, 30, -8, -8, -10, 16, 16, 16);
  b.box(r.head, 176, 44, -6, -1, -24, 12, 5, 16);
  b.box(r.head, 0, 0, -5, -5, -18, 2, 4, 6);
  b.box(r.head, 0, 0, 3, -5, -18, 2, 4, 6);
  const int jaw = b.part(0, 4, -64);
  b.box(jaw, 176, 65, -6, 0, -16, 12, 4, 16);
  r.extra.push_back(jaw);  // índice 5: la mandíbula
  for (int i = 0; i < 12; i++) {  // cola
    const int p = b.part(0, 0.8f * i, 44.0f + 9.0f * i);
    b.box(p, 192, 104, -5, -5, -5, 10, 10, 10);
    b.box(p, 48, 0, -1, -9, -3, 2, 4, 6);
    r.extra.push_back(p);  // 6 .. 17
  }
  // Alas: brazo, membrana y la punta (colgando del brazo)
  auto wing = [&](float side) {
    const bool left = side > 0;
    const int arm = b.part(side * 12, -10, 2);
    b.box(arm, 112, 88, left ? 0 : -56, -4, -4, 56, 8, 8, 0, left);
    b.box(arm, 0, 150, left ? 0 : -56, -1, 2, 56, 1, 56, 0, left);
    const int tip = b.part(side * 56, 0, 0);
    b.m.parts[static_cast<std::size_t>(tip)].parent = arm;
    b.m.parts[static_cast<std::size_t>(tip)].pivot = {-side * 56.0f, 0, 0};
    b.box(tip, 112, 136, left ? 0 : -56, -2, -2, 56, 4, 4, 0, left);
    b.box(tip, 0, 150, left ? 0 : -56, -1, 2, 56, 1, 56, 0, left);
    r.extra.push_back(tip);
    return arm;
  };
  r.rightWing = wing(-1);
  r.leftWing = wing(1);
  // Patas
  for (int i = 0; i < 4; i++) {
    const bool front = i < 2;
    const float sx = i % 2 == 0 ? -1.0f : 1.0f;
    r.legs[static_cast<std::size_t>(i)] = b.part(sx * (front ? 12.0f : 16.0f), 12, front ? -4.0f : 34.0f);
    if (front) {
      b.box(r.legs[static_cast<std::size_t>(i)], 112, 104, -4, -4, -4, 8, 24, 8);
      b.box(r.legs[static_cast<std::size_t>(i)], 226, 138, -3, 18, -3, 6, 24, 6);
      b.box(r.legs[static_cast<std::size_t>(i)], 144, 104, -4, 40, -12, 8, 4, 16);
    } else {
      b.box(r.legs[static_cast<std::size_t>(i)], 0, 200, -8, -8, -8, 16, 32, 16);
      b.box(r.legs[static_cast<std::size_t>(i)], 196, 0, -6, 20, -6, 12, 32, 12);
      b.box(r.legs[static_cast<std::size_t>(i)], 112, 104, -9, 50, -14, 18, 6, 24);
    }
  }
  mm.model = b.m;
  mm.texture = "entity/enderdragon/dragon.png";
  mm.overlay = "entity/enderdragon/dragon_eyes.png";
  mm.overlayModel = b.m;
  mm.overlayEmissive = true;
  mm.scale = 1.6f;
  return mm;
}


// --- Criaturas de más (medidas de 1.8; el eje Y de los modelos va hacia abajo y el suelo está a 24 píxeles) ---------

/// Una pieza hija: `px, py, pz` absolutos (como `Build::part`); el pivote queda relativo al del padre.
int childPart(Build& b, int parent, float px, float py, float pz) {
  const int i = b.part(px, py, pz);
  b.m.parts[static_cast<std::size_t>(i)].pivot -= b.m.parts[static_cast<std::size_t>(parent)].pivot;
  b.m.parts[static_cast<std::size_t>(i)].parent = parent;
  return i;
}

MobModel makeSquid() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  r.body = b.part(0, 8, 0);
  b.box(r.body, 0, 0, -6, -8, -6, 12, 16, 12);
  for (int i = 0; i < 8; i++) {
    const float a = static_cast<float>(i) * kPi * 2.0f / 8.0f;
    const int t = b.part(std::cos(a) * 5.0f, 15, std::sin(a) * 5.0f);
    b.box(t, 48, 0, -1, 0, -1, 2, 18, 2);
    b.rest(t, 0, static_cast<float>(i) * kPi * -2.0f / 8.0f + kPi / 2.0f, 0);
    r.extra.push_back(t);
  }
  mm.model = b.m;
  mm.texture = "entity/squid.png";
  mm.offsetY = -0.7f;
  return mm;
}

MobModel makeBat() {
  MobModel mm;
  Build b(64, 64);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 8, 0);
  b.box(r.head, 0, 0, -3, -3, -3, 6, 6, 6);
  b.box(r.head, 24, 0, -4, -6, -2, 3, 4, 1);        // oreja derecha
  b.box(r.head, 24, 0, 1, -6, -2, 3, 4, 1, 0, true);  // oreja izquierda
  r.body = b.part(0, 8, 0);
  b.box(r.body, 0, 16, -3, 4, -3, 6, 12, 6);
  b.box(r.body, 0, 34, -5, 16, 0, 10, 6, 1);        // la membrana de abajo
  b.rest(r.body, kPi / 4.0f, 0, 0);
  r.rightWing = b.part(0, 8, 0);
  b.box(r.rightWing, 42, 0, -12, 1, 1.5f, 10, 16, 1);
  const int rTip = childPart(b, r.rightWing, -12, 9, 1.5f);
  b.box(rTip, 24, 16, -8, 1, 0, 8, 12, 1);
  r.leftWing = b.part(0, 8, 0);
  b.box(r.leftWing, 42, 0, 2, 1, 1.5f, 10, 16, 1, 0, true);
  const int lTip = childPart(b, r.leftWing, 12, 9, 1.5f);
  b.box(lTip, 24, 16, 0, 1, 0, 8, 12, 1, 0, true);
  r.extra = {rTip, lTip};
  mm.model = b.m;
  mm.texture = "entity/bat.png";
  mm.scale = 0.35f;
  mm.offsetY = 0.15f;
  return mm;
}

MobModel makeRabbit() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 16, -1);
  b.box(r.head, 32, 0, -2.5f, -4, -5, 5, 4, 5);
  b.box(r.head, 52, 0, -2.5f, -9, -1, 2, 5, 1);   // oreja derecha
  b.box(r.head, 58, 0, 0.5f, -9, -1, 2, 5, 1);    // oreja izquierda
  b.box(r.head, 32, 9, -0.5f, -2.5f, -5.5f, 1, 1, 1);  // nariz
  r.body = b.part(0, 19, 8);
  b.box(r.body, 0, 0, -3, -2, -10, 6, 5, 10);
  b.box(r.body, 52, 6, -1.5f, -1.5f, 0, 3, 3, 2);  // cola (con el cuerpo)
  b.rest(r.body, -0.349f, 0, 0);
  r.legs[0] = b.part(-3, 17.5f, 3.7f);  // muslo derecho (atrás) con su pie
  b.box(r.legs[0], 16, 15, -1, 0, 0, 2, 4, 5);
  b.box(r.legs[0], 8, 24, -1, 5.5f, -3.7f, 2, 1, 7);
  r.legs[1] = b.part(3, 17.5f, 3.7f);
  b.box(r.legs[1], 30, 15, -1, 0, 0, 2, 4, 5);
  b.box(r.legs[1], 26, 24, -1, 5.5f, -3.7f, 2, 1, 7);
  r.legs[2] = b.part(-3, 17, -1);  // brazo derecho
  b.box(r.legs[2], 0, 15, -1, 0, -1, 2, 7, 2);
  r.legs[3] = b.part(3, 17, -1);
  b.box(r.legs[3], 8, 15, -1, 0, -1, 2, 7, 2);
  mm.model = b.m;
  mm.texture = "entity/rabbit/brown.png";
  return mm;
}

MobModel makeWolf() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  r.head = b.part(-1, 13.5f, -7);
  b.box(r.head, 0, 0, -3, -3, -2, 6, 6, 4);
  b.box(r.head, 16, 14, -3, -5, 0, 2, 2, 1);   // orejas
  b.box(r.head, 16, 14, 1, -5, 0, 2, 2, 1);
  b.box(r.head, 0, 10, -1.5f, 0, -5, 3, 3, 4);  // hocico
  r.body = b.part(0, 14, 2);
  b.box(r.body, 18, 14, -4, -2, -3, 6, 9, 6);
  b.rest(r.body, kPi / 2, 0, 0);
  const int mane = b.part(-1, 14, -3);
  b.box(mane, 21, 0, -4, -3, -3, 8, 6, 7);
  b.rest(mane, kPi / 2, 0, 0);
  r.legs[0] = b.part(-2.5f, 16, 7);
  r.legs[1] = b.part(0.5f, 16, 7);
  r.legs[2] = b.part(-2.5f, 16, -4);
  r.legs[3] = b.part(0.5f, 16, -4);
  for (int i = 0; i < 4; i++) b.box(r.legs[static_cast<std::size_t>(i)], 0, 18, -1, 0, -1, 2, 8, 2);
  const int tail = b.part(-1, 12, 8);
  b.box(tail, 9, 18, -1, 0, -1, 2, 8, 2);
  b.rest(tail, 0.6283f, 0, 0);
  r.extra = {mane, tail};
  mm.model = b.m;
  mm.texture = "entity/wolf/wolf.png";
  return mm;
}

MobModel makeOcelot() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 15, -9);
  b.box(r.head, 0, 0, -2.5f, -2, -3, 5, 4, 5);
  b.box(r.head, 0, 24, -1.5f, 0, -4, 3, 2, 2);   // hocico
  b.box(r.head, 0, 10, -2, -3, 0, 1, 1, 2);      // orejas
  b.box(r.head, 6, 10, 1, -3, 0, 1, 1, 2);
  r.body = b.part(0, 12, -10);
  b.box(r.body, 20, 0, -2, 3, -8, 4, 16, 6);
  b.rest(r.body, kPi / 2, 0, 0);
  const int tail1 = b.part(0, 15, 8);
  b.box(tail1, 0, 15, -0.5f, 0, 0, 1, 8, 1);
  b.rest(tail1, 0.9f, 0, 0);
  const int tail2 = b.part(0, 20, 14);
  b.box(tail2, 4, 15, -0.5f, 0, 0, 1, 8, 1);
  r.legs[0] = b.part(-1.1f, 18, 5);
  b.box(r.legs[0], 8, 13, -1, 0, 1, 2, 6, 2);
  r.legs[1] = b.part(1.1f, 18, 5);
  b.box(r.legs[1], 8, 13, -1, 0, 1, 2, 6, 2);
  r.legs[2] = b.part(-1.2f, 13.8f, -5);
  b.box(r.legs[2], 40, 0, -1, 0, 0, 2, 10, 2);
  r.legs[3] = b.part(1.2f, 13.8f, -5);
  b.box(r.legs[3], 40, 0, -1, 0, 0, 2, 10, 2);
  r.extra = {tail1, tail2};
  mm.model = b.m;
  mm.texture = "entity/cat/ocelot.png";
  mm.scale = 0.8f;
  return mm;
}

MobModel makeHorse() {
  MobModel mm;
  Build b(128, 128);
  ModelRig& r = mm.rig;
  r.body = b.part(0, 11, 9);
  b.box(r.body, 0, 34, -5, -8, -19, 10, 10, 24);
  // La cola
  const int tail = b.part(0, 3, 14);
  b.box(tail, 44, 0, -1, -1, 0, 2, 2, 3);
  b.box(tail, 38, 7, -1.5f, -2, 3, 3, 4, 7);
  b.box(tail, 24, 3, -1.5f, -4.5f, 9, 3, 4, 7);
  b.rest(tail, -1.134f, 0, 0);
  // Cabeza y cuello (giran juntos)
  r.head = b.part(0, 4, -10);
  b.box(r.head, 0, 0, -2.5f, -10, -1.5f, 5, 5, 7);          // cabeza
  b.box(r.head, 24, 18, -2, -10, -7, 4, 3, 6);               // hocico de arriba
  b.box(r.head, 24, 27, -2, -7, -6.5f, 4, 2, 5);             // hocico de abajo
  b.box(r.head, 0, 12, -2.05f, -9.8f, -2, 4, 14, 8);         // cuello
  b.box(r.head, 58, 0, -1, -11.5f, 5, 2, 16, 4);             // crin
  b.box(r.head, 19, 16, -2.55f, -12, 4, 2, 3, 1);            // orejas
  b.box(r.head, 19, 16, 0.55f, -12, 4, 2, 3, 1);
  b.rest(r.head, 0.5236f, 0, 0);
  // Patas: pierna, caña y casco (las de atrás y las de delante, izquierda y derecha)
  struct Leg { float x, z; int thigh[2], shin[2], hoof[2]; bool back; };
  const Leg legs[4] = {{-4, 11, {96, 29}, {96, 43}, {96, 51}, true}, {4, 11, {78, 29}, {78, 43}, {78, 51}, true},
                       {-4, -8, {60, 29}, {60, 41}, {60, 51}, false}, {4, -8, {44, 29}, {44, 41}, {44, 51}, false}};
  for (int i = 0; i < 4; i++) {
    const Leg& l = legs[i];
    const int top = b.part(l.x, 9, l.z);
    r.legs[static_cast<std::size_t>(i)] = top;
    if (l.back) {
      b.box(top, l.thigh[0], l.thigh[1], l.x > 0 ? -2.5f : -1.5f, -2, -2.5f, 4, 9, 5);
      const int shin = childPart(b, top, l.x, 16, l.z);
      b.box(shin, l.shin[0], l.shin[1], -2, 0, -1.5f, 3, 5, 3);
      const int hoof = childPart(b, top, l.x, 16, l.z);
      b.box(hoof, l.hoof[0], l.hoof[1], -2.5f, 5.1f, -2, 4, 3, 4);
    } else {
      b.box(top, l.thigh[0], l.thigh[1], l.x > 0 ? -1.9f : -1.1f, -1, -2.1f, 3, 8, 4);
      const int shin = childPart(b, top, l.x, 16, l.z);
      b.box(shin, l.shin[0], l.shin[1], -1.9f, 0, -1.6f, 3, 5, 3);
      const int hoof = childPart(b, top, l.x, 16, l.z);
      b.box(hoof, l.hoof[0], l.hoof[1], -2.4f, 5.1f, -2.1f, 4, 3, 4);
    }
  }
  r.extra = {tail};
  mm.model = b.m;
  mm.texture = "entity/horse/horse_brown.png";
  return mm;
}

/// Aldeano (y base de la bruja): cabeza con nariz, cuerpo con túnica, brazos cruzados y piernas.
MobModel makeVillager(const char* texture, float texH, bool witch) {
  MobModel mm;
  Build b(64, texH);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 0, 0);
  b.box(r.head, 0, 0, -4, -10, -4, 8, 10, 8);
  b.box(r.head, 24, 0, -1, -3, -6, 2, 4, 2);  // nariz
  if (witch) {
    b.box(r.head, 0, 64, -5, -10.03f, -5, 10, 2, 10);        // ala del sombrero
    b.box(r.head, 0, 76, -3.5f, -14.03f, -3.5f, 7, 4, 7);    // cono de abajo
    b.box(r.head, 0, 87, -2, -17.03f, -2, 4, 4, 4);          // medio
    b.box(r.head, 0, 95, -1, -19.03f, -1, 2, 2, 2);          // punta
    b.box(r.head, 0, 0, 0, 0, 0, 0, 0, 0);                    // (sin verruga: se queda vacía)
    b.rest(r.head, 0, 0, 0);
  }
  r.body = b.part(0, 0, 0);
  b.box(r.body, 16, 20, -4, 0, -3, 8, 12, 6);
  b.box(r.body, 0, 38, -4, 0, -3, 8, 18, 6, 0.5f);   // la túnica
  const int arms = b.part(0, 3, -1);
  b.box(arms, 44, 22, -8, -2, -2, 4, 8, 4);
  b.box(arms, 44, 22, 4, -2, -2, 4, 8, 4, 0, true);
  b.box(arms, 40, 38, -4, 2, -2, 8, 4, 4);
  b.rest(arms, -0.75f, 0, 0);
  r.rightArm = arms;
  r.legs[0] = b.part(-2, 12, 0);
  b.box(r.legs[0], 0, 22, -2, 0, -2, 4, 12, 4);
  r.legs[1] = b.part(2, 12, 0);
  b.box(r.legs[1], 0, 22, -2, 0, -2, 4, 12, 4, 0, true);
  mm.model = b.m;
  mm.texture = texture;
  return mm;
}

MobModel makeIronGolem() {
  MobModel mm;
  Build b(128, 128);
  ModelRig& r = mm.rig;
  r.head = b.part(0, -7, -2);
  b.box(r.head, 0, 0, -4, -12, -5.5f, 8, 10, 8);
  b.box(r.head, 24, 0, -1, -5, -7.5f, 2, 4, 2);
  r.body = b.part(0, -7, 0);
  b.box(r.body, 0, 40, -9, -2, -6, 18, 12, 11);
  b.box(r.body, 0, 70, -4.5f, 10, -3, 9, 5, 6, 0.5f);
  r.rightArm = b.part(0, -7, 0);
  b.box(r.rightArm, 60, 21, -13, -2.5f, -3, 4, 30, 6);
  r.leftArm = b.part(0, -7, 0);
  b.box(r.leftArm, 60, 58, 9, -2.5f, -3, 4, 30, 6);
  r.legs[0] = b.part(-4, 11.5f, 0);
  b.box(r.legs[0], 37, 0, -3.5f, -3, -3, 6, 16, 5);
  r.legs[1] = b.part(5, 11.5f, 0);
  b.box(r.legs[1], 60, 0, -3.5f, -3, -3, 6, 16, 5, 0, true);
  mm.model = b.m;
  mm.texture = "entity/iron_golem.png";
  return mm;
}

MobModel makeSnowGolem() {
  MobModel mm;
  Build b(64, 64);
  ModelRig& r = mm.rig;
  r.head = b.part(0, 4, 0);
  b.box(r.head, 0, 0, -4, -8, -4, 8, 8, 8, -0.5f);
  r.body = b.part(0, 13, 0);
  b.box(r.body, 0, 16, -5, -10, -5, 10, 10, 10, -0.5f);
  const int low = b.part(0, 24, 0);
  b.box(low, 0, 36, -6, -12, -6, 12, 12, 12, -0.5f);
  r.rightArm = b.part(0, 6, 0);
  b.box(r.rightArm, 32, 0, -1, 0, -1, 12, 2, 2, -0.5f);
  b.rest(r.rightArm, 0, 0, 1.0f);
  r.leftArm = b.part(0, 6, 0);
  b.box(r.leftArm, 32, 0, -1, 0, -1, 12, 2, 2, -0.5f);
  b.rest(r.leftArm, 0, kPi, -1.0f);
  r.extra = {low};
  mm.model = b.m;
  mm.texture = "entity/snowman.png";
  return mm;
}

MobModel makeEndermite() {
  MobModel mm;
  Build b(64, 32);
  ModelRig& r = mm.rig;
  const int sizes[4][3] = {{4, 3, 2}, {6, 4, 5}, {3, 3, 1}, {1, 2, 1}};
  const int uv[4][2] = {{0, 0}, {0, 5}, {0, 14}, {0, 18}};
  float z = -3.5f;
  for (int i = 0; i < 4; i++) {
    const int w = sizes[i][0], h = sizes[i][1], d = sizes[i][2];
    const int p = b.part(0, 24 - h, z);
    b.box(p, uv[i][0], uv[i][1], -w / 2.0f, 0, -d / 2.0f, w, h, d);
    r.extra.push_back(p);
    if (i < 3) z += (d + sizes[i + 1][2]) / 2.0f;
  }
  mm.model = b.m;
  mm.texture = "entity/endermite.png";
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
    a[static_cast<int>(MobType::PigZombie)] = biped("entity/zombie_pigman.png", false, 64, true);
    a[static_cast<int>(MobType::Ghast)] = makeGhast();
    a[static_cast<int>(MobType::Blaze)] = makeBlaze();
    a[static_cast<int>(MobType::MagmaCube)] = makeMagmaCube();
    a[static_cast<int>(MobType::Slime)] = makeSlime();
    a[static_cast<int>(MobType::Enderman)] = makeEnderman();
    a[static_cast<int>(MobType::Silverfish)] = makeSilverfish();
    a[static_cast<int>(MobType::CaveSpider)] = makeSpider("entity/spider/cave_spider.png");
    a[static_cast<int>(MobType::CaveSpider)].scale = 0.7f;
    a[static_cast<int>(MobType::WitherSkeleton)] = biped("entity/skeleton/wither_skeleton.png", true, 32, false);
    a[static_cast<int>(MobType::WitherSkeleton)].scale = 1.2f;
    a[static_cast<int>(MobType::EnderDragon)] = makeDragon();
    a[static_cast<int>(MobType::Squid)] = makeSquid();
    a[static_cast<int>(MobType::Bat)] = makeBat();
    a[static_cast<int>(MobType::Rabbit)] = makeRabbit();
    a[static_cast<int>(MobType::Mooshroom)] = makeCow();
    a[static_cast<int>(MobType::Mooshroom)].texture = "entity/cow/mooshroom.png";
    a[static_cast<int>(MobType::Wolf)] = makeWolf();
    a[static_cast<int>(MobType::Ocelot)] = makeOcelot();
    a[static_cast<int>(MobType::Horse)] = makeHorse();
    a[static_cast<int>(MobType::Villager)] = makeVillager("entity/villager/villager.png", 64, false);
    a[static_cast<int>(MobType::IronGolem)] = makeIronGolem();
    a[static_cast<int>(MobType::SnowGolem)] = makeSnowGolem();
    a[static_cast<int>(MobType::Witch)] = makeVillager("entity/witch.png", 128, true);
    a[static_cast<int>(MobType::Endermite)] = makeEndermite();
    return a;
  }();
  return all;
}

}  // namespace

std::string mobTexture(const Mob& m) {
  switch (m.type) {
    case MobType::Wolf: return m.angry ? "entity/wolf/wolf_angry.png" : (m.tamed ? "entity/wolf/wolf_tame.png" : "entity/wolf/wolf.png");
    case MobType::Ocelot: {
      static const char* kCats[] = {"entity/cat/ocelot.png", "entity/cat/black.png", "entity/cat/red.png", "entity/cat/siamese.png"};
      return kCats[m.variant & 3];
    }
    case MobType::Rabbit: {
      static const char* kRabbits[] = {"brown", "white", "black", "white_splotched", "gold", "salt"};
      return m.variant == 99 ? "entity/rabbit/caerbannog.png" : std::string("entity/rabbit/") + kRabbits[m.variant % 6] + ".png";
    }
    case MobType::Horse: {
      static const char* kCoats[] = {"white", "creamy", "chestnut", "brown", "black", "gray", "darkbrown"};
      if (m.variant == 1) return "entity/horse/donkey.png";
      if (m.variant == 2) return "entity/horse/mule.png";
      return std::string("entity/horse/horse_") + kCoats[(m.coat & 255) % 7] + ".png";
    }
    case MobType::Villager: {
      static const char* kJobs[] = {"farmer", "librarian", "priest", "smith", "butcher"};
      return std::string("entity/villager/") + kJobs[m.variant % 5] + ".png";
    }
    default: return mobModel(m.type).texture;
  }
}

std::vector<MobLayer> mobLayers(const Mob& m) {
  std::vector<MobLayer> out;
  if (m.type == MobType::Wolf && m.tamed) {
    // Collar del color de su tinte (14 = rojo)
    static const u32 kDye[16] = {0x1E1B1B, 0xB3312C, 0x3B511A, 0x51301A, 0x253192, 0x7B2FBE, 0x287697, 0x9A9A9A,
                                 0x434343, 0xD88198, 0x41CD34, 0xDECF2A, 0x6689D3, 0xC354CD, 0xEB8844, 0xF0F0F0};
    // (el color 0 es el tinte negro: en el juego los tintes se cuentan al revés que la lana)
    const u32 c = kDye[m.collar & 15];
    out.push_back({"entity/wolf/wolf_collar.png", glm::vec3(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f)});
  }
  if (m.type == MobType::Horse && m.variant == 0) {
    static const char* kMarks[] = {"", "horse_markings_white", "horse_markings_whitefield", "horse_markings_whitedots", "horse_markings_blackdots"};
    const int k = static_cast<int>((m.coat >> 8) & 255) % 5;
    if (k > 0) out.push_back({std::string("entity/horse/") + kMarks[k] + ".png", glm::vec3(1.0f)});
  }
  return out;
}

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
             int eatGrassTicks, u32 flags) {
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
    case MobType::Enderman: {
      p.rot[r.legs[0]].x = walk * 0.6f;
      p.rot[r.legs[1]].x = -walk * 0.6f;
      p.rot[r.rightArm].x = -walk * 0.5f;
      p.rot[r.leftArm].x = walk * 0.5f;
      break;
    }
    case MobType::EnderDragon: {
      const float flap = std::sin(age * 0.22f);
      p.rot[r.rightWing] = {0, 0, -0.25f - flap * 0.5f};
      p.rot[r.leftWing] = {0, 0, 0.25f + flap * 0.5f};
      for (std::size_t i = 0; i < r.extra.size(); i++) {
        if (i < 5) p.rot[r.extra[i]].x = std::sin(age * 0.1f + static_cast<float>(i) * 0.4f) * 0.05f;
        else if (i == 5) p.rot[r.extra[i]].x = 0.2f + (std::sin(age * 0.3f) + 1.0f) * 0.1f;
        else if (i < 18) p.rot[r.extra[i]].y = std::sin(age * 0.08f + static_cast<float>(i) * 0.35f) * 0.12f;
        else p.rot[r.extra[i]].z = (i == 18 ? -1.0f : 1.0f) * (0.1f + flap * 0.35f);  // las puntas de las alas
      }
      for (int i = 0; i < 4; i++) p.rot[r.legs[static_cast<std::size_t>(i)]].x = 0.5f + flap * 0.1f;
      break;
    }
    case MobType::Ghast:
      for (std::size_t i = 0; i < r.extra.size(); i++) p.rot[r.extra[i]].x = 0.2f * std::sin(age * 0.3f + static_cast<float>(i)) + 0.1f;
      break;
    case MobType::Blaze:
      for (std::size_t i = 0; i < r.extra.size(); i++) p.rot[r.extra[i]].y = age * (i < 4 ? 0.05f : i < 8 ? -0.07f : 0.09f);
      break;
    case MobType::Squid:
      for (std::size_t i = 0; i < r.extra.size(); i++)  // los tentáculos ondulan
        p.rot[r.extra[i]].x = std::sin(age * 0.25f + static_cast<float>(i) * 0.8f) * 0.35f + 0.3f;
      break;
    case MobType::Bat: {
      if (flags & kPoseHanging) {  // colgado boca abajo con las alas plegadas
        p.rot[r.body].x = kPi - kPi / 4.0f;
        p.rot[r.head].x = kPi;
        p.rot[r.rightWing].y = -1.3f;
        p.rot[r.leftWing].y = 1.3f;
        p.rot[r.extra[0]].y = -1.9f;
        p.rot[r.extra[1]].y = 1.9f;
      } else {
        const float f = age * 1.3f;
        p.rot[r.rightWing].y = std::cos(f) * kPi * 0.25f;
        p.rot[r.leftWing].y = -std::cos(f) * kPi * 0.25f;
        p.rot[r.extra[0]].y = p.rot[r.rightWing].y * 0.5f;
        p.rot[r.extra[1]].y = -p.rot[r.rightWing].y * 0.5f;
      }
      break;
    }
    case MobType::Rabbit: {
      // Salta: en el aire el cuerpo se alarga (patas de atrás atrás, de delante delante)
      const float hop = onGround ? 0.0f : 1.0f;
      p.rot[r.legs[0]].x = -walk * 0.2f - hop * 0.9f;
      p.rot[r.legs[1]].x = -walk * 0.2f - hop * 0.9f;
      p.rot[r.legs[2]].x = walk * 0.2f + hop * 0.8f;
      p.rot[r.legs[3]].x = walk * 0.2f + hop * 0.8f;
      break;
    }
    case MobType::Wolf: {
      const bool sit = (flags & kPoseSitting) != 0;
      p.rot[r.legs[0]].x = walk;
      p.rot[r.legs[1]].x = -walk;
      p.rot[r.legs[2]].x = -walk;
      p.rot[r.legs[3]].x = walk;
      // La cola: tiesa y baja si es salvaje, alta si es mansa y se menea si está enfadada
      p.rot[r.extra[1]].x = (flags & kPoseTamed ? 0.0f : 0.5f) + (sit ? 0.5f : 0.0f);
      p.rot[r.extra[1]].y = std::cos(swing * 0.6662f) * 1.4f * amount * 0.7f + ((flags & kPoseAngry) ? 0.0f : std::sin(age * 0.2f) * 0.1f);
      if (sit) {  // sentado: el cuerpo se inclina y las patas de atrás se doblan
        p.rot[r.body].x = -kPi / 4.0f;
        p.rot[r.extra[0]].x = -kPi / 4.0f;
        p.rot[r.legs[0]].x = -kPi / 2.0f;
        p.rot[r.legs[1]].x = -kPi / 2.0f;
        p.rot[r.legs[2]].x = -0.1f;
        p.rot[r.legs[3]].x = -0.1f;
      }
      break;
    }
    case MobType::Ocelot: {
      const bool sit = (flags & kPoseSitting) != 0;
      p.rot[r.legs[0]].x = walk;
      p.rot[r.legs[1]].x = -walk;
      p.rot[r.legs[2]].x = -walk;
      p.rot[r.legs[3]].x = walk;
      p.rot[r.extra[0]].x = 0.0f;
      p.rot[r.extra[1]].x = 0.0f + (sit ? 0.5f : 0.0f);
      if (sit) {
        p.rot[r.body].x = -kPi / 4.0f;
        p.rot[r.legs[0]].x = -kPi / 2.0f;
        p.rot[r.legs[1]].x = -kPi / 2.0f;
      }
      break;
    }
    case MobType::Horse:
      p.rot[r.legs[0]].x = -walk * 0.9f;  // detrás
      p.rot[r.legs[1]].x = walk * 0.9f;
      p.rot[r.legs[2]].x = walk * 0.9f;   // delante
      p.rot[r.legs[3]].x = -walk * 0.9f;
      p.rot[r.extra[0]].x = std::sin(age * 0.1f) * 0.08f + amount * 0.3f;  // la cola
      break;
    case MobType::Villager:
    case MobType::Witch:
      p.rot[r.legs[0]].x = walk * 0.7f;
      p.rot[r.legs[1]].x = -walk * 0.7f;
      break;
    case MobType::IronGolem:
      p.rot[r.legs[0]].x = -1.5f * std::cos(swing * 0.6662f) * 1.4f * amount * 0.7f;
      p.rot[r.legs[1]].x = 1.5f * std::cos(swing * 0.6662f) * 1.4f * amount * 0.7f;
      p.rot[r.rightArm].x = (-0.2f + 1.5f * std::cos(swing * 0.6662f)) * 0.8f * amount;
      p.rot[r.leftArm].x = (-0.2f - 1.5f * std::cos(swing * 0.6662f)) * 0.8f * amount;
      break;
    case MobType::SnowGolem:
      p.rot[r.head].y = headYawRel * 0.25f;
      p.rot[r.extra[0]].y = headYawRel * 0.25f;
      break;
    case MobType::Endermite:
      for (std::size_t i = 0; i < r.extra.size(); i++)
        p.rot[r.extra[i]].y = std::cos(age * 0.9f + static_cast<float>(i) * 0.15f * kPi) * kPi * 0.01f * (1.0f + std::abs(static_cast<float>(i) - 2.0f));
      break;
    case MobType::Silverfish:
      for (std::size_t i = 0; i < r.extra.size() && i < 7; i++)
        p.rot[r.extra[i]].y = std::cos(age * 0.9f + static_cast<float>(i) * 0.15f * kPi) * kPi * 0.01f * (1.0f + std::abs(static_cast<float>(i) - 2.0f));
      break;
    case MobType::PigZombie:
    case MobType::WitherSkeleton:
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
    case MobType::CaveSpider:
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

const EntityModel& crystalModel() {
  static const EntityModel model = [] {
    Build b(64, 32);
    const int base = b.part(0, 24, 0);
    b.box(base, 0, 16, -6, -4, -6, 12, 4, 12);
    const int glass = b.part(0, 14, 0);
    b.box(glass, 0, 0, -4, -4, -4, 8, 8, 8, 0.0f);
    const int core = b.part(0, 14, 0);
    b.box(core, 32, 0, -3, -3, -3, 6, 6, 6);
    return b.m;
  }();
  return model;
}

const EntityModel& armorStandModel() {
  static const EntityModel model = [] {
    // Medidas en el sistema clásico (y hacia abajo, suelo a 24 píxeles); textura de 64x64 de 1.8
    Build b(64, 64);
    const int head = b.part(0, 1, 0);
    b.box(head, 0, 0, -1, -7, -1, 2, 7, 2);
    const int shoulders = b.part(0, 0, 0);
    b.box(shoulders, 0, 26, -6, 0, -1.5f, 12, 3, 3);
    const int rightArm = b.part(-5, 2, 0);
    b.box(rightArm, 24, 0, -2, -2, -1, 2, 12, 2);
    b.layer(rightArm, kStandArms);
    b.rest(rightArm, -0.2618f, 0, 0.1745f);
    const int leftArm = b.part(5, 2, 0);
    b.box(leftArm, 32, 16, 0, -2, -1, 2, 12, 2, 0, true);
    b.layer(leftArm, kStandArms);
    b.rest(leftArm, -0.1745f, 0, -0.1745f);
    const int rightLeg = b.part(-1.9f, 12, 0);
    b.box(rightLeg, 8, 0, -1, 0, -1, 2, 11, 2);
    const int leftLeg = b.part(1.9f, 12, 0);
    b.box(leftLeg, 40, 16, -1, 0, -1, 2, 11, 2, 0, true);
    const int rodR = b.part(0, 0, 0);
    b.box(rodR, 16, 0, -3, 3, -1, 2, 7, 2);
    const int rodL = b.part(0, 0, 0);
    b.box(rodL, 48, 16, 1, 3, -1, 2, 7, 2);
    const int hips = b.part(0, 0, 0);
    b.box(hips, 0, 48, -4, 10, -1, 8, 2, 2);
    const int plate = b.part(0, 12, 0);
    b.box(plate, 0, 32, -6, 11, -6, 12, 1, 12);
    b.layer(plate, kStandBase);
    return b.m;
  }();
  return model;
}

const EntityModel& cartModel() {
  static const EntityModel model = [] {
    // Medidas en el sistema clásico (y hacia abajo) con el origen a 6 píxeles sobre la base de la vagoneta; `Build` mide
    // desde 24 píxeles sobre el suelo, así que se suman 18
    Build b(64, 32);
    auto part = [&](float px, float py, float pz) { return b.part(px, py + 18.0f, pz); };
    const int floor = part(0, 4, 0);
    b.box(floor, 0, 10, -10, -8, -1, 20, 16, 2);
    b.rest(floor, kPi / 2, 0, 0);
    const int inside = part(0, 4, 0);
    b.box(inside, 44, 10, -9, -7, -1, 18, 14, 1);
    b.rest(inside, -kPi / 2, 0, 0);
    const int back = part(-9, 4, 0);
    b.box(back, 0, 0, -8, -9, -1, 16, 8, 2);
    b.rest(back, 0, kPi * 1.5f, 0);
    const int front = part(9, 4, 0);
    b.box(front, 0, 0, -8, -9, -1, 16, 8, 2);
    b.rest(front, 0, kPi / 2, 0);
    const int left = part(0, 4, -7);
    b.box(left, 0, 0, -8, -9, -1, 16, 8, 2);
    b.rest(left, 0, kPi, 0);
    const int right = part(0, 4, 7);
    b.box(right, 0, 0, -8, -9, -1, 16, 8, 2);
    return b.m;
  }();
  return model;
}

}  // namespace mcw
