#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "client/enchant_books.h"
#include "data/items.h"
#include "game/armor.h"
#include "game/enchant_effects.h"
#include "game/enchantments.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/light.h"
#include "world/world.h"

using namespace mcw;

namespace {

/// Mundo plano de 3x3 chunks: piedra, tierra y hierba en y=63 (suelo a y=64).
struct EfxWorld : WorldAccess {
  World w;
  EfxWorld() {
    ChunkSet mod;
    for (int cz = -1; cz <= 1; cz++)
      for (int cx = -1; cx <= 1; cx++) {
        auto c = std::make_unique<Chunk>(cx, cz);
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) {
            c->setBlock(x, 0, z, makeState(B::bedrock));
            for (int y = 1; y < 60; y++) c->setBlock(x, y, z, makeState(B::stone));
            for (int y = 60; y < 63; y++) c->setBlock(x, y, z, makeState(B::dirt));
            c->setBlock(x, 63, z, makeState(B::grass));
          }
        light::computeInitial(*c);
        w.insert(std::move(c), mod);
      }
  }
  World& world() override { return w; }
  void setBlock(int x, int y, int z, BlockState s) override {
    ChunkSet mod;
    w.setBlock(x, y, z, s, mod);
  }
};

TickInput idle(double time = 6000) {
  TickInput in;
  in.worldTime = time;
  return in;
}

ItemStack enchanted(int item, std::initializer_list<std::pair<int, int>> list) {
  ItemStack s(item);
  for (const auto& [id, level] : list) s.addEnchant(id, level);
  return s;
}

/// Las cuatro piezas de armadura de hierro con ese encantamiento.
void wearAll(Player& p, int ench, int level) {
  p.inventory.armor(0) = enchanted(ItemId::iron_boots, {{ench, level}});
  p.inventory.armor(1) = enchanted(ItemId::iron_leggings, {{ench, level}});
  p.inventory.armor(2) = enchanted(ItemId::iron_chestplate, {{ench, level}});
  p.inventory.armor(3) = enchanted(ItemId::iron_helmet, {{ench, level}});
}

}  // namespace

TEST_CASE("Protección: los puntos de cada encantamiento de armadura son los de 1.8") {
  using enchfx::protectionPoints;
  // Protección: floor((6 + n²) / 3 × 0,75) -> 1, 2, 3, 5
  CHECK(protectionPoints(Ench::Protection, 1, DamageKind::Melee) == 1);
  CHECK(protectionPoints(Ench::Protection, 2, DamageKind::Melee) == 2);
  CHECK(protectionPoints(Ench::Protection, 3, DamageKind::Melee) == 3);
  CHECK(protectionPoints(Ench::Protection, 4, DamageKind::Melee) == 5);
  CHECK(protectionPoints(Ench::Protection, 4, DamageKind::Fall) == 5);  // vale contra casi todo
  CHECK(protectionPoints(Ench::Protection, 4, DamageKind::Void) == 0);  // menos el vacío
  CHECK(protectionPoints(Ench::Protection, 4, DamageKind::Starvation) == 0);
  // Cada especialidad solo vale contra lo suyo
  CHECK(protectionPoints(Ench::FireProtection, 4, DamageKind::Fire) == 9);
  CHECK(protectionPoints(Ench::FireProtection, 4, DamageKind::Melee) == 0);
  CHECK(protectionPoints(Ench::FeatherFalling, 4, DamageKind::Fall) == 18);
  CHECK(protectionPoints(Ench::FeatherFalling, 4, DamageKind::Explosion) == 0);
  CHECK(protectionPoints(Ench::BlastProtection, 4, DamageKind::Explosion) == 11);
  CHECK(protectionPoints(Ench::ProjectileProtection, 4, DamageKind::Projectile) == 11);
  CHECK(protectionPoints(Ench::ProjectileProtection, 4, DamageKind::Explosion) == 0);
  CHECK(protectionPoints(Ench::Sharpness, 5, DamageKind::Melee) == 0);  // los demás no protegen
}

TEST_CASE("Protección: Protección IV en las cuatro piezas deja entre un 20 y un 60 % del daño, y nunca baja de 2/5") {
  Player p;
  wearAll(p, Ench::Protection, 4);  // 4 × 5 = 20 puntos: de 10 a 20 tras el azar
  int lo = 99, hi = 0;
  double sum = 0;
  for (int i = 0; i < 4000; i++) {
    const int k = enchfx::protectionModifier(p.inventory, DamageKind::Melee, p.rng);
    lo = std::min(lo, k);
    hi = std::max(hi, k);
    sum += k;
  }
  CHECK(lo == 10);
  CHECK(hi == 20);
  CHECK(sum / 4000.0 == doctest::Approx(15.0).epsilon(0.03));
  CHECK(enchfx::afterProtection(10.0f, 20) == doctest::Approx(2.0f));  // 80 % menos
  CHECK(enchfx::afterProtection(10.0f, 0) == doctest::Approx(10.0f));
  // Con encantamientos de más (tope 25 antes del azar, 20 después)
  wearAll(p, Ench::FeatherFalling, 4);  // 4 × 18 = 72 -> 25 -> como mucho 20
  for (int i = 0; i < 500; i++) CHECK(enchfx::protectionModifier(p.inventory, DamageKind::Fall, p.rng) <= 20);
  // Sin encantamientos no hay modificador
  Player plain;
  plain.inventory.armor(2) = ItemStack(ItemId::iron_chestplate);
  CHECK(enchfx::protectionModifier(plain.inventory, DamageKind::Melee, plain.rng) == 0);
}

TEST_CASE("Protección: un golpe duele menos con la armadura encantada, y el hambre y el vacío se la saltan") {
  Player bare, safe;
  wearAll(safe, Ench::Protection, 4);
  bare.damage(10.0f, false, DamageKind::Melee);
  safe.damage(10.0f, false, DamageKind::Melee);
  CHECK(bare.health == doctest::Approx(10.0f));
  CHECK(safe.health >= 14.0f);  // 10 * (25 - k) / 25 con k de 10 a 20: de 2 a 6 de daño
  CHECK(safe.health <= 18.0f);
  Player starving, plain;
  wearAll(starving, Ench::Protection, 4);
  starving.damage(4.0f, false, DamageKind::Starvation);
  plain.damage(4.0f, false, DamageKind::Starvation);
  CHECK(starving.health == doctest::Approx(plain.health));
  Player voidP;
  wearAll(voidP, Ench::Protection, 4);
  voidP.damage(4.0f, false, DamageKind::Void);
  CHECK(voidP.health == doctest::Approx(16.0f));
}

TEST_CASE("Caída de pluma: las botas encantadas reducen el daño de una caída") {
  EfxWorld fw;
  auto fall = [&](bool boots) {
    Player p;
    p.pos = p.prevPos = {0.5, 80, 0.5};  // 16 bloques de caída: 13 de daño
    if (boots) p.inventory.armor(0) = enchanted(ItemId::iron_boots, {{Ench::FeatherFalling, 4}});
    for (int i = 0; i < 80; i++) p.tickMovement(fw.w, {}, false);
    return p.health;
  };
  const float without = fall(false), with = fall(true);
  CHECK(without == doctest::Approx(7.0f));
  CHECK(with > 12.0f);  // como mucho 13 * (25 - 9) / 25 = 8,3 de daño
  CHECK(with < 19.0f);  // y algo duele: no se anula
}

TEST_CASE("Irrompibilidad: las herramientas se gastan menos y las armaduras algo menos") {
  Random rng(5);
  auto total = [&](const ItemStack& item, int trials) {
    long sum = 0;
    for (int i = 0; i < trials; i++) sum += enchfx::wearAfterUnbreaking(item, 1, rng);
    return static_cast<double>(sum) / trials;
  };
  const ItemStack plain(ItemId::iron_pickaxe);
  CHECK(total(plain, 100) == doctest::Approx(1.0));
  // Herramienta: cada punto se salva con probabilidad n / (n + 1): Irrompibilidad III gasta 1 de cada 4
  CHECK(total(enchanted(ItemId::iron_pickaxe, {{Ench::Unbreaking, 3}}), 8000) == doctest::Approx(0.25).epsilon(0.1));
  CHECK(total(enchanted(ItemId::iron_pickaxe, {{Ench::Unbreaking, 1}}), 8000) == doctest::Approx(0.5).epsilon(0.1));
  // Armadura: el 60 % de las veces cuenta siempre, y el 40 % restante como una herramienta: 0,6 + 0,4 / 4 = 0,7
  CHECK(total(enchanted(ItemId::iron_chestplate, {{Ench::Unbreaking, 3}}), 8000) == doctest::Approx(0.7).epsilon(0.05));
  // Desgastar y romper: una herramienta con un punto de vida desaparece
  ItemStack worn(ItemId::wooden_pickaxe, 1, itemInfo(ItemId::wooden_pickaxe).maxDurability - 1);
  CHECK(enchfx::wearItem(worn, 1, rng));
  CHECK(worn.empty());
}

TEST_CASE("Armas: Filo, Pesadez y Perdición de los artrópodos suman daño según la criatura") {
  const ItemStack sharp = enchanted(ItemId::diamond_sword, {{Ench::Sharpness, 5}});
  CHECK(enchfx::weaponBonus(sharp, MobType::Pig) == doctest::Approx(6.25f));
  const ItemStack smite = enchanted(ItemId::diamond_sword, {{Ench::Smite, 5}});
  CHECK(enchfx::weaponBonus(smite, MobType::Zombie) == doctest::Approx(12.5f));
  CHECK(enchfx::weaponBonus(smite, MobType::Skeleton) == doctest::Approx(12.5f));
  CHECK(enchfx::weaponBonus(smite, MobType::Spider) == doctest::Approx(0.0f));
  const ItemStack bane = enchanted(ItemId::diamond_sword, {{Ench::BaneOfArthropods, 2}});
  CHECK(enchfx::weaponBonus(bane, MobType::Spider) == doctest::Approx(5.0f));
  CHECK(enchfx::weaponBonus(bane, MobType::Zombie) == doctest::Approx(0.0f));
  CHECK(enchfx::weaponBonus(ItemStack(ItemId::diamond_sword), MobType::Zombie) == doctest::Approx(0.0f));
}

TEST_CASE("Herramientas: Eficiencia solo acelera si la herramienta ya es la buena") {
  const ItemStack eff = enchanted(ItemId::iron_pickaxe, {{Ench::Efficiency, 5}});
  CHECK(enchfx::efficiencySpeed(6.0f, eff) == doctest::Approx(6.0f + 26.0f));  // nivel² + 1
  CHECK(enchfx::efficiencySpeed(1.0f, eff) == doctest::Approx(1.0f));          // con la mano o la herramienta equivocada, nada
  CHECK(enchfx::efficiencySpeed(6.0f, ItemStack(ItemId::iron_pickaxe)) == doctest::Approx(6.0f));
  // Y se nota al picar: con Eficiencia V la piedra cae mucho antes
  auto ticks = [](const ItemStack& tool) {
    float p = 0;
    int t = 0;
    while (p < 0.9999f && t < 10000) {
      p += digProgressPerTick(makeState(B::stone), tool, true, false);
      t++;
    }
    return t;
  };
  CHECK(ticks(eff) * 3 < ticks(ItemStack(ItemId::iron_pickaxe)));
}

TEST_CASE("Toque de seda y Fortuna: lo que suelta un bloque con una herramienta encantada") {
  Random rng(9);
  const ItemStack silk = enchanted(ItemId::iron_pickaxe, {{Ench::SilkTouch, 1}});
  auto drops = [&](int block, const ItemStack& tool, int meta = 0) { return blockDrops(makeState(block, meta), tool, rng); };
  // Con Toque de seda: el bloque entero
  auto d = drops(B::stone, silk);
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == B::stone);
  d = drops(B::diamond_ore, silk);
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == B::diamond_ore);
  d = drops(B::lit_redstone_ore, silk);
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == B::redstone_ore);  // (la encendida suelta la apagada)
  d = drops(B::glass, enchanted(ItemId::wooden_shovel, {{Ench::SilkTouch, 1}}));
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == B::glass);
  d = drops(B::grass, enchanted(ItemId::wooden_shovel, {{Ench::SilkTouch, 1}}));
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == B::grass);
  // Sin él, lo de siempre
  d = drops(B::stone, ItemStack(ItemId::iron_pickaxe));
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == B::cobblestone);
  d = drops(B::diamond_ore, ItemStack(ItemId::iron_pickaxe));
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == ItemId::diamond);
  // Con la herramienta que no vale el bloque no suelta nada, ni con Toque de seda
  CHECK(drops(B::stone, enchanted(ItemId::wooden_shovel, {{Ench::SilkTouch, 1}})).empty());

  // Fortuna: el carbón y los diamantes salen más (de 1 a 4 con Fortuna III; de media 2,2) y la piedra no cambia
  const ItemStack fortune = enchanted(ItemId::iron_pickaxe, {{Ench::Fortune, 3}});
  double sum = 0;
  int most = 0;
  for (int i = 0; i < 4000; i++) {
    int n = 0;
    for (const ItemStack& s : drops(B::coal_ore, fortune)) n += s.count;
    sum += n;
    most = std::max(most, n);
  }
  CHECK(sum / 4000.0 == doctest::Approx(2.2).epsilon(0.05));
  CHECK(most == 4);
  double plain = 0;
  for (int i = 0; i < 400; i++)
    for (const ItemStack& s : drops(B::coal_ore, ItemStack(ItemId::iron_pickaxe))) plain += s.count;
  CHECK(plain / 400.0 == doctest::Approx(1.0));
  d = drops(B::stone, fortune);
  REQUIRE(d.size() == 1);
  CHECK(d[0].id == B::cobblestone);
  CHECK(d[0].count == 1);
  // El mineral de redstone suma de 0 a nivel (4 o 5 de base)
  int redMin = 99, redMax = 0;
  for (int i = 0; i < 2000; i++) {
    int n = 0;
    for (const ItemStack& s : drops(B::redstone_ore, fortune)) n += s.count;
    redMin = std::min(redMin, n);
    redMax = std::max(redMax, n);
  }
  CHECK(redMin >= 4);
  CHECK(redMax <= 8);
  CHECK(redMax > 5);
  // La grava suelta pedernal siempre con Fortuna III
  for (int i = 0; i < 50; i++) {
    d = drops(B::gravel, fortune);
    REQUIRE(d.size() == 1);
    CHECK(d[0].id == ItemId::flint);
  }
}

TEST_CASE("Espinas: la probabilidad crece con el nivel y el daño va de 1 a 4") {
  Random rng(21);
  int hits = 0, mx = 0, mn = 99;
  for (int i = 0; i < 6000; i++) {
    const int d = enchfx::thornsDamage(3, rng);
    if (d > 0) {
      hits++;
      mx = std::max(mx, d);
      mn = std::min(mn, d);
    }
  }
  CHECK(hits / 6000.0 == doctest::Approx(0.45).epsilon(0.08));  // 15 % por nivel
  CHECK(mn == 1);
  CHECK(mx == 4);
  int none = 0;
  for (int i = 0; i < 100; i++) none += enchfx::thornsDamage(0, rng);
  CHECK(none == 0);
}

TEST_CASE("Respiración alarga el aire bajo el agua") {
  EfxWorld fw;
  auto airAfter = [&](int level) {
    Player p;
    p.pos = p.prevPos = {0.5, 64, 0.5};
    if (level) p.inventory.armor(3) = enchanted(ItemId::iron_helmet, {{Ench::Respiration, level}});
    p.headInWater = true;
    for (int i = 0; i < 200; i++) p.tickStatus(fw.w);
    return p.air;
  };
  CHECK(airAfter(0) == 100);       // 300 - 200
  CHECK(airAfter(3) > 200);        // gasta de media 1 de cada 4
  CHECK(airAfter(3) < 290);
  // Sin aire: sin Respiración se ahoga antes que con ella
  auto healthAfter = [&](int level) {
    Player p;
    p.pos = p.prevPos = {0.5, 64, 0.5};
    if (level) p.inventory.armor(3) = enchanted(ItemId::iron_helmet, {{Ench::Respiration, level}});
    p.headInWater = true;
    for (int i = 0; i < 500; i++) p.tickStatus(fw.w);
    return p.health;
  };
  CHECK(healthAfter(3) > healthAfter(0));
}

TEST_CASE("Agilidad acuática: con las botas se avanza más rápido nadando") {
  EfxWorld fw;
  for (int x = -10; x <= 10; x++)
    for (int z = -10; z <= 10; z++)
      for (int y = 64; y <= 68; y++) fw.setBlock(x, y, z, makeState(B::water));
  auto swim = [&](int level) {
    Player p;
    p.pos = p.prevPos = {0.5, 64.2, 0.5};
    if (level) p.inventory.armor(0) = enchanted(ItemId::iron_boots, {{Ench::DepthStrider, level}});
    MoveInput in;
    in.forward = 1.0f;
    for (int i = 0; i < 25; i++) p.tickMovement(fw.w, in, false);
    return std::hypot(p.pos.x - 0.5, p.pos.z - 0.5);
  };
  const double plain = swim(0), strider = swim(3);
  CHECK(plain > 0.5);
  CHECK(strider > plain * 1.3);
}

TEST_CASE("Lava y fuego: queman al jugador, el agua apaga y Protección contra el fuego ayuda") {
  EfxWorld fw;
  fw.setBlock(0, 64, 0, makeState(B::lava));
  auto burn = [&](int fireProtection) {
    Player p;
    p.pos = p.prevPos = {0.5, 64, 0.5};
    if (fireProtection) wearAll(p, Ench::FireProtection, fireProtection);
    p.tickStatus(fw.w);
    return p;
  };
  const Player plain = burn(0);
  CHECK(plain.health == doctest::Approx(20.0f - 4.0f));  // 4 de la lava
  CHECK(plain.fireTicks > 250);                          // y se queda ardiendo
  const Player guarded = burn(4);
  CHECK(guarded.health > plain.health);                  // (la armadura de hierro también cuenta)
  // Arder quita 1 por segundo...
  Player p;
  p.pos = p.prevPos = {5.5, 64, 5.5};
  p.fireTicks = 100;
  for (int i = 0; i < 100; i++) p.tickStatus(fw.w);
  CHECK(p.health == doctest::Approx(15.0f).epsilon(0.1));
  CHECK(p.fireTicks == 0);
  // ...y el agua lo apaga
  Player wet;
  wet.pos = wet.prevPos = {5.5, 64, 5.5};
  fw.setBlock(5, 64, 5, makeState(B::water));
  wet.fireTicks = 200;
  wet.tickStatus(fw.w);
  CHECK(wet.fireTicks == 0);
  // En creativo no pasa nada
  Player creative;
  creative.mode = GameMode::Creative;
  creative.pos = creative.prevPos = {0.5, 64, 0.5};
  creative.tickStatus(fw.w);
  CHECK(creative.health == doctest::Approx(20.0f));
  CHECK(creative.fireTicks == 0);
}

TEST_CASE("Espada encantada: Pesadez mata a un zombi de un golpe y Aspecto ígneo lo prende") {
  auto strike = [&](const ItemStack& sword, MobType type) {
    EfxWorld fw;
    GameSession s(fw, 3);
    Player& p = s.player();
    p.pos = p.prevPos = {0.5, 64, 0.5};
    p.inventory.slot(0) = sword;
    s.spawnMob(type, {0.5, 64, -1.5});
    s.tick(idle());
    TickInput hit = idle();
    hit.yaw = 0;
    hit.pitch = -0.3f;
    hit.attack = hit.attackPressed = true;
    s.tick(hit);
    REQUIRE(s.mobs().size() == 1);
    return s.mobs()[0];
  };
  // Un zombi tiene 20 de vida: la espada de diamante hace 8, con Pesadez V suma 12,5
  const Mob plain = strike(ItemStack(ItemId::diamond_sword), MobType::Zombie);
  CHECK_FALSE(plain.dying());
  CHECK(plain.health == doctest::Approx(12.0f));
  const Mob smitten = strike(enchanted(ItemId::diamond_sword, {{Ench::Smite, 5}}), MobType::Zombie);
  CHECK(smitten.dying());
  // Pesadez no hace nada contra un cerdo; Filo sí
  const Mob pigSmite = strike(enchanted(ItemId::diamond_sword, {{Ench::Smite, 5}}), MobType::Pig);
  CHECK(pigSmite.health == doctest::Approx(2.0f));
  const Mob pigSharp1 = strike(enchanted(ItemId::diamond_sword, {{Ench::Sharpness, 1}}), MobType::Pig);
  CHECK(pigSharp1.health == doctest::Approx(0.75f));  // 8 + 1,25 de 10
  const Mob pigSharp2 = strike(enchanted(ItemId::diamond_sword, {{Ench::Sharpness, 2}}), MobType::Pig);
  CHECK(pigSharp2.dying());                            // 8 + 2,5 pasa de 10
  // Aspecto ígneo: 4 segundos ardiendo por nivel
  const Mob burning = strike(enchanted(ItemId::diamond_sword, {{Ench::FireAspect, 2}}), MobType::Pig);
  CHECK(burning.fireTicks >= 150);
  CHECK(plain.fireTicks == 0);
  // Retroceso empuja más que un golpe normal
  const Mob pushed = strike(enchanted(ItemId::diamond_sword, {{Ench::Knockback, 2}}), MobType::Pig);
  const Mob normal = strike(ItemStack(ItemId::diamond_sword), MobType::Pig);
  CHECK(std::abs(pushed.motion.z) > std::abs(normal.motion.z) + 0.3);
}

TEST_CASE("Botín: de media suelta más de lo que se esperaría sin él") {
  auto chops = [&](int looting) {
    EfxWorld fw;
    GameSession s(fw, 17);
    Player& p = s.player();
    p.pos = p.prevPos = {0.5, 64, 0.5};
    ItemStack sword = enchanted(ItemId::diamond_sword, {{Ench::Sharpness, 5}});  // (mata de un golpe)
    if (looting) sword.addEnchant(Ench::Looting, looting);
    p.inventory.slot(0) = sword;
    int total = 0;
    for (int i = 0; i < 60; i++) {
      p.hurtTime = 0;
      s.spawnMob(MobType::Pig, {0.5, 64, -1.5});
      s.tick(idle());
      TickInput hit = idle();
      hit.yaw = 0;
      hit.pitch = -0.3f;
      hit.attack = hit.attackPressed = true;
      s.tick(hit);
      for (int t = 0; t < 30; t++) s.tick(idle());
    }
    for (int i = 0; i < PlayerInventory::kSize; i++)
      if (p.inventory.slot(i).id == ItemId::porkchop) total += p.inventory.slot(i).count;
    for (const ItemEntity& e : s.items())
      if (e.stack.id == ItemId::porkchop) total += e.stack.count;
    return total;
  };
  const int plain = chops(0), lucky = chops(3);
  CHECK(plain > 60);                         // 1 a 3 chuletas por cerdo
  CHECK(lucky > plain * 1.3);                // Botín III suma de 0 a 3 más
}

TEST_CASE("Arco encantado: Poder, Golpe, Llama e Infinidad") {
  auto shoot = [&](const ItemStack& bow, bool creative = false) {
    EfxWorld fw;
    GameSession s(fw, 11);
    Player& p = s.player();
    if (creative) s.setMode(GameMode::Creative);
    p.pos = p.prevPos = {0.5, 64, 0.5};
    p.inventory.slot(0) = bow;
    p.inventory.slot(1) = ItemStack(ItemId::arrow, 5);
    s.tick(idle());
    TickInput in = idle();
    for (int i = 0; i < 20; i++) {
      in.use = true;
      in.usePressed = i == 0;
      s.tick(in);
    }
    in.use = in.usePressed = false;
    s.tick(in);
    struct Result {
      std::vector<Arrow> arrows;
      int arrowsLeft;
    };
    return Result{s.arrows(), s.arrowCount()};
  };
  const auto plain = shoot(ItemStack(ItemId::bow));
  REQUIRE(plain.arrows.size() == 1);
  CHECK(plain.arrows[0].damage == doctest::Approx(2.0f));
  CHECK(plain.arrows[0].punch == 0);
  CHECK_FALSE(plain.arrows[0].flame);
  CHECK(plain.arrowsLeft == 4);
  CHECK(plain.arrows[0].pickup);
  // Poder V: +0,5 por nivel y +0,5 más
  const auto power = shoot(enchanted(ItemId::bow, {{Ench::Power, 5}}));
  REQUIRE(power.arrows.size() == 1);
  CHECK(power.arrows[0].damage == doctest::Approx(5.0f));
  // Golpe y Llama
  const auto extras = shoot(enchanted(ItemId::bow, {{Ench::Punch, 2}, {Ench::Flame, 1}}));
  REQUIRE(extras.arrows.size() == 1);
  CHECK(extras.arrows[0].punch == 2);
  CHECK(extras.arrows[0].flame);
  // Infinidad: la flecha no se gasta (y no se puede recoger), pero hace falta tener una
  const auto infinite = shoot(enchanted(ItemId::bow, {{Ench::Infinity, 1}}));
  REQUIRE(infinite.arrows.size() == 1);
  CHECK(infinite.arrowsLeft == 5);
  CHECK_FALSE(infinite.arrows[0].pickup);
}

TEST_CASE("Libro de la mesa de encantamientos: flota, se abre y mira al jugador al acercarse, y se cierra al irse") {
  EfxWorld fw;
  fw.setBlock(4, 64, 4, makeState(116));
  EnchantBooks books;
  glm::dvec3 player(4.5, 64, 6.5);  // dos bloques al sur de la mesa
  for (int i = 0; i < 200; i++) books.update(fw.w, player, 0.05);
  REQUIRE(books.count() == 1);
  BookPose p = books.poses()[0];
  CHECK(p.open == doctest::Approx(1.0f));
  CHECK(p.pos.x == doctest::Approx(4.5));
  CHECK(p.pos.z == doctest::Approx(4.5));
  CHECK(p.pos.y > 64.75 + 0.2);  // flota sobre la mesa (que llega a 12/16)
  CHECK(p.pos.y < 64.75 + 0.5);
  CHECK(std::abs(p.yaw) < 0.05f);  // su cara mira hacia +Z, donde está el jugador
  // El jugador se pone al este: gira hacia él (yaw = atan2(dx, dz) = 90 grados)
  player = {6.5, 64, 4.5};
  for (int i = 0; i < 200; i++) books.update(fw.w, player, 0.05);
  CHECK(books.poses()[0].yaw == doctest::Approx(1.5708f).epsilon(0.02));
  // Pasa páginas mientras está abierto
  float lo = 1, hi = 0;
  for (int i = 0; i < 60; i++) {
    books.update(fw.w, player, 0.05);
    lo = std::min(lo, books.poses()[0].flip);
    hi = std::max(hi, books.poses()[0].flip);
  }
  CHECK(hi > lo);
  // Se aleja unos bloques (la mesa aún se ve): se cierra
  player = {4.5, 64, 12.5};
  for (int i = 0; i < 200; i++) books.update(fw.w, player, 0.05);
  REQUIRE(books.count() == 1);
  CHECK(books.poses()[0].open == doctest::Approx(0.0f));
  // Muy lejos ya no hay libro que dibujar
  player = {4.5, 64, 30.5};
  for (int i = 0; i < 20; i++) books.update(fw.w, player, 0.05);
  CHECK(books.count() == 0);
}
