// Pack libre (CC0): dibujos propios de todos los objetos de 1.8 que no tienen uno en cc0_pack.cpp.
// Los archivos usan los nombres de textura del formato de 1.8 para que un resource pack encima
// (Faithful, etc.) los sustituya aunque no haya jar.
#include <cmath>
#include <functional>

#include "assets/cc0_internal.h"
#include "core/random.h"
#include "data/items.h"

namespace mcw::cc0 {

std::string itemTex18(const std::string& n) {
  static const std::map<std::string, std::string> fixed = {
      {"golden_apple", "apple_golden"}, {"golden_carrot", "carrot_golden"}, {"wheat_seeds", "seeds_wheat"},
      {"pumpkin_seeds", "seeds_pumpkin"}, {"melon_seeds", "seeds_melon"}, {"porkchop", "porkchop_raw"},
      {"cooked_porkchop", "porkchop_cooked"}, {"beef", "beef_raw"}, {"cooked_beef", "beef_cooked"},
      {"chicken", "chicken_raw"}, {"cooked_chicken", "chicken_cooked"}, {"mutton", "mutton_raw"},
      {"cooked_mutton", "mutton_cooked"}, {"rabbit", "rabbit_raw"}, {"cooked_rabbit", "rabbit_cooked"},
      {"fish", "fish_cod_raw"}, {"cooked_fish", "fish_cod_cooked"}, {"baked_potato", "potato_baked"},
      {"poisonous_potato", "potato_poisonous"}, {"bow", "bow_standby"}, {"redstone", "redstone_dust"},
      {"slime_ball", "slimeball"}, {"book", "book_normal"}, {"writable_book", "book_writable"},
      {"written_book", "book_written"}, {"enchanted_book", "book_enchanted"}, {"bucket", "bucket_empty"},
      {"water_bucket", "bucket_water"}, {"lava_bucket", "bucket_lava"}, {"milk_bucket", "bucket_milk"},
      {"minecart", "minecart_normal"}, {"chest_minecart", "minecart_chest"}, {"furnace_minecart", "minecart_furnace"},
      {"tnt_minecart", "minecart_tnt"}, {"hopper_minecart", "minecart_hopper"},
      {"command_block_minecart", "minecart_command_block"}, {"wooden_door", "door_wood"}, {"iron_door", "door_iron"},
      {"spruce_door", "door_spruce"}, {"birch_door", "door_birch"}, {"jungle_door", "door_jungle"},
      {"acacia_door", "door_acacia"}, {"dark_oak_door", "door_dark_oak"}, {"fishing_rod", "fishing_rod_uncast"},
      {"filled_map", "map_filled"}, {"map", "map_empty"}, {"potion", "potion_bottle_drinkable"},
      {"glass_bottle", "potion_bottle_empty"}, {"fermented_spider_eye", "spider_eye_fermented"},
      {"speckled_melon", "melon_speckled"}, {"fire_charge", "fireball"}, {"firework_charge", "fireworks_charge"},
      {"armor_stand", "wooden_armorstand"}, {"golden_horse_armor", "gold_horse_armor"},
  };
  if (auto it = fixed.find(n); it != fixed.end()) return it->second;
  if (n.rfind("wooden_", 0) == 0) return "wood_" + n.substr(7);
  if (n.rfind("golden_", 0) == 0) return "gold_" + n.substr(7);
  if (n.rfind("dye_", 0) == 0) return "dye_powder_" + n.substr(4);
  return n;
}

void putItem(MemoryPack& pack, const std::string& name, const Image& img) {
  const std::string t = itemTex18(name);
  pack.putImage(kTex + "items/" + t + ".png", img);
  pack.putJson("assets/minecraft/models/item/" + name + ".json", {{"parent", "builtin/generated"}, {"textures", {{"layer0", "items/" + t}}}});
}

namespace {

void px(Image& img, int x, int y, u32 c) {
  if (x >= 0 && y >= 0 && x < 16 && y < 16) img.set(x, y, c);
}

/// Contorno oscuro alrededor de lo dibujado (como los sprites del juego).
void outline(Image& img, u32 c = 0xFF1E1E1E) {
  Image src = img;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      if (src.get(x, y) >> 24) continue;
      bool near = false;
      for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
        const int nx = x + dx, ny = y + dy;
        if (nx >= 0 && ny >= 0 && nx < 16 && ny < 16 && (src.get(nx, ny) >> 24)) near = true;
      }
      if (near) img.set(x, y, c);
    }
}

Image blank() { return Image(16, 16, 0); }

Image armor(const std::string& piece, u32 c) {
  Image i = blank();
  const u32 d = shade(c, -45), l = shade(c, 30);
  if (piece == "helmet") {
    fillRect(i, 3, 4, 10, 3, c);
    fillRect(i, 3, 7, 2, 4, c);
    fillRect(i, 11, 7, 2, 4, c);
    fillRect(i, 4, 4, 8, 1, l);
  } else if (piece == "chestplate") {
    fillRect(i, 2, 2, 4, 3, c); fillRect(i, 10, 2, 4, 3, c);
    fillRect(i, 4, 4, 8, 10, c);
    fillRect(i, 6, 2, 4, 2, 0);
    for (int y = 5; y < 14; y++) i.set(8, y, d);
    fillRect(i, 4, 4, 8, 1, l);
  } else if (piece == "leggings") {
    fillRect(i, 3, 2, 10, 3, c);
    fillRect(i, 3, 5, 4, 9, c); fillRect(i, 9, 5, 4, 9, c);
    fillRect(i, 3, 2, 10, 1, l);
  } else {
    fillRect(i, 2, 7, 4, 5, c); fillRect(i, 10, 7, 4, 5, c);
    fillRect(i, 1, 11, 5, 2, d); fillRect(i, 10, 11, 5, 2, d);
  }
  outline(i);
  return i;
}

Image chainArmor(const std::string& piece) {
  Image i = armor(piece, rgb(150, 150, 155));
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
      if ((x + y) % 2 == 0 && (i.get(x, y) >> 24) && i.get(x, y) != 0xFF1E1E1E) i.set(x, y, rgb(90, 90, 95));
  return i;
}

Image bucket(u32 content, bool hasContent) {
  Image i = blank();
  const u32 metal = rgb(190, 190, 195);
  for (int y = 5; y < 14; y++) {
    const int w = 5 - (y - 5) / 3;
    fillRect(i, 8 - w, y, 2 * w, 1, shade(metal, (y % 3) * 6 - 6));
  }
  fillRect(i, 3, 4, 10, 2, shade(metal, 20));
  if (hasContent) fillRect(i, 4, 4, 8, 2, content);
  line(i, 3, 4, 5, 1, rgb(120, 120, 125)); line(i, 12, 4, 10, 1, rgb(120, 120, 125)); line(i, 5, 1, 10, 1, rgb(120, 120, 125));
  outline(i);
  return i;
}

Image minecart(u32 cargo, bool hasCargo) {
  Image i = blank();
  const u32 metal = rgb(140, 140, 150);
  fillRect(i, 1, 6, 14, 6, metal);
  fillRect(i, 2, 7, 12, 3, shade(metal, -30));
  if (hasCargo) fillRect(i, 3, 3, 10, 5, cargo);
  disc(i, 4.5, 12.5, 1.8, rgb(60, 60, 60), rgb(30, 30, 30));
  disc(i, 11.5, 12.5, 1.8, rgb(60, 60, 60), rgb(30, 30, 30));
  outline(i);
  return i;
}

Image bottle(u32 liquid, bool full) {
  Image i = blank();
  const u32 glass = rgb(200, 220, 240, 200);
  fillRect(i, 6, 1, 4, 2, rgb(150, 110, 70));  // tapón
  fillRect(i, 6, 3, 4, 3, glass);
  disc(i, 7.5, 10.0, 4.6, full ? liquid : glass, rgb(170, 190, 210));
  if (full) { px(i, 5, 8, rgb(255, 255, 255, 200)); px(i, 5, 9, rgb(255, 255, 255, 200)); }
  outline(i);
  return i;
}

Image bookImg(u32 cover, bool feather, bool glow) {
  Image i = blank();
  fillRect(i, 3, 2, 10, 12, cover);
  fillRect(i, 11, 3, 2, 10, rgb(235, 230, 215));
  fillRect(i, 3, 2, 1, 12, shade(cover, -40));
  if (glow) for (int y = 3; y < 13; y += 3) fillRect(i, 5, y, 5, 1, rgb(210, 120, 230));
  if (feather) { line(i, 8, 12, 14, 2, rgb(240, 240, 240)); line(i, 9, 12, 15, 3, rgb(210, 210, 210)); }
  outline(i);
  return i;
}

Image doorItem(u32 c, bool window) {
  Image i = blank();
  fillRect(i, 4, 1, 8, 14, c);
  for (int y = 1; y < 15; y++) { i.set(4, y, shade(c, -35)); i.set(11, y, shade(c, -35)); }
  fillRect(i, 4, 7, 8, 1, shade(c, -35));
  if (window) { fillRect(i, 6, 2, 4, 4, rgb(160, 200, 230)); fillRect(i, 7, 2, 1, 4, c); }
  i.set(10, 9, rgb(40, 40, 40));
  outline(i);
  return i;
}

Image blob(u32 c, double r = 4.5) {
  Image i = blank();
  disc(i, 7.5, 8.0, r, c, shade(c, -45));
  px(i, 5, 6, shade(c, 50));
  px(i, 6, 5, shade(c, 40));
  return i;
}

Image dust(u32 c, u32 seed) {
  Image i = blank();
  Random r(seed);
  for (int k = 0; k < 34; k++) {
    const int x = 3 + r.nextInt(10), y = 6 + r.nextInt(8);
    if (std::abs(x - 7.5) + std::abs(y - 11) * 1.4 < 7) i.set(x, y, shade(c, r.nextInt(40) - 20));
  }
  return i;
}

Image rod(u32 c, u32 tip) {
  Image i = blank();
  line(i, 3, 13, 12, 4, c);
  line(i, 4, 13, 13, 4, shade(c, -30));
  if (tip) { px(i, 12, 3, tip); px(i, 13, 3, tip); px(i, 13, 4, tip); }
  return i;
}

Image recordImg(u32 label) {
  Image i = blank();
  disc(i, 7.5, 7.5, 6.8, rgb(30, 30, 35), rgb(15, 15, 18));
  disc(i, 7.5, 7.5, 2.2, label, shade(label, -40));
  return i;
}

/// Dibujo de un objeto por su nombre de registro; imagen vacía si no hay.
Image draw(const std::string& n) {
  const u32 iron = rgb(215, 215, 220), gold = rgb(245, 205, 50), diamond = rgb(90, 230, 220), leather = rgb(150, 90, 50);
  for (const auto& [mat, c] : {std::pair{"leather", leather}, std::pair{"iron", iron}, std::pair{"golden", gold}, std::pair{"diamond", diamond}})
    for (const char* p : {"helmet", "chestplate", "leggings", "boots"})
      if (n == std::string(mat) + "_" + p) return armor(p, c);
  for (const char* p : {"helmet", "chestplate", "leggings", "boots"})
    if (n == std::string("chainmail_") + p) return chainArmor(p);
  if (n == "bucket") return bucket(0, false);
  if (n == "water_bucket") return bucket(rgb(50, 90, 220), true);
  if (n == "lava_bucket") return bucket(rgb(240, 120, 20), true);
  if (n == "milk_bucket") return bucket(rgb(250, 250, 250), true);
  if (n == "minecart") return minecart(0, false);
  if (n == "chest_minecart") return minecart(rgb(160, 110, 45), true);
  if (n == "furnace_minecart") return minecart(rgb(110, 110, 110), true);
  if (n == "tnt_minecart") return minecart(rgb(200, 50, 40), true);
  if (n == "hopper_minecart") return minecart(rgb(60, 60, 64), true);
  if (n == "command_block_minecart") return minecart(rgb(180, 130, 90), true);
  if (n == "potion") return bottle(rgb(60, 80, 220, 230), true);
  if (n == "glass_bottle") return bottle(0, false);
  if (n == "experience_bottle") return bottle(rgb(150, 230, 60, 230), true);
  if (n == "book") return bookImg(rgb(120, 70, 40), false, false);
  if (n == "writable_book") return bookImg(rgb(120, 70, 40), true, false);
  if (n == "written_book") return bookImg(rgb(90, 60, 40), false, false);
  if (n == "enchanted_book") return bookImg(rgb(110, 50, 140), false, true);
  if (n == "paper" || n == "map" || n == "filled_map") {
    Image i = blank();
    fillRect(i, 2, 2, 12, 12, rgb(235, 230, 210));
    if (n != "paper") { fillRect(i, 2, 2, 12, 1, rgb(150, 130, 100)); fillRect(i, 4, 4, 8, 8, n == "map" ? rgb(215, 205, 175) : rgb(120, 170, 90)); }
    if (n == "filled_map") { fillRect(i, 7, 5, 3, 4, rgb(60, 110, 200)); px(i, 6, 9, rgb(200, 30, 30)); }
    outline(i);
    return i;
  }
  static const std::map<std::string, std::pair<u32, bool>> doors = {
      {"wooden_door", {rgb(162, 130, 78), true}}, {"iron_door", {rgb(210, 210, 210), true}}, {"spruce_door", {rgb(115, 85, 49), true}},
      {"birch_door", {rgb(196, 179, 123), true}}, {"jungle_door", {rgb(160, 115, 80), true}}, {"acacia_door", {rgb(168, 90, 50), false}},
      {"dark_oak_door", {rgb(66, 43, 20), false}}};
  if (auto it = doors.find(n); it != doors.end()) return doorItem(it->second.first, it->second.second);
  if (n == "sign") {
    Image i = blank();
    fillRect(i, 1, 2, 14, 8, rgb(170, 135, 80));
    for (int y = 4; y < 9; y += 2) fillRect(i, 3, y, 10, 1, rgb(110, 85, 50));
    fillRect(i, 7, 10, 2, 5, rgb(105, 75, 40));
    outline(i);
    return i;
  }
  if (n == "bed") {
    Image i = blank();
    fillRect(i, 1, 6, 14, 4, rgb(170, 30, 30));
    fillRect(i, 1, 6, 4, 3, rgb(235, 235, 235));
    fillRect(i, 1, 10, 14, 2, rgb(162, 130, 78));
    fillRect(i, 1, 12, 2, 2, rgb(120, 90, 50)); fillRect(i, 13, 12, 2, 2, rgb(120, 90, 50));
    outline(i);
    return i;
  }
  if (n == "cake") {
    Image i = blank();
    fillRect(i, 2, 5, 12, 3, rgb(245, 240, 235));
    fillRect(i, 2, 8, 12, 5, rgb(170, 100, 55));
    for (int x = 3; x < 13; x += 3) px(i, x, 5, rgb(210, 30, 30));
    outline(i);
    return i;
  }
  if (n == "repeater" || n == "comparator") {
    Image i = blank();
    fillRect(i, 1, 9, 14, 4, rgb(160, 160, 160));
    fillRect(i, 1, 9, 14, 1, rgb(190, 190, 190));
    for (int x : (n == "repeater" ? std::vector<int>{5, 10} : std::vector<int>{4, 8, 12})) {
      fillRect(i, x, 4, 2, 5, rgb(110, 80, 45));
      fillRect(i, x, 3, 2, 2, rgb(230, 40, 30));
    }
    outline(i);
    return i;
  }
  if (n == "cauldron") {
    Image i = blank();
    fillRect(i, 2, 3, 12, 10, rgb(60, 60, 64));
    fillRect(i, 4, 3, 8, 3, rgb(30, 30, 32));
    fillRect(i, 2, 13, 3, 2, rgb(50, 50, 52)); fillRect(i, 11, 13, 3, 2, rgb(50, 50, 52));
    outline(i);
    return i;
  }
  if (n == "brewing_stand") {
    Image i = blank();
    fillRect(i, 7, 1, 2, 12, rgb(200, 180, 60));
    fillRect(i, 2, 13, 12, 2, rgb(110, 110, 110));
    disc(i, 3.5, 10.5, 2.0, rgb(180, 200, 230), rgb(140, 160, 190));
    disc(i, 12.5, 10.5, 2.0, rgb(180, 200, 230), rgb(140, 160, 190));
    return i;
  }
  if (n == "flower_pot") {
    Image i = blank();
    fillRect(i, 4, 7, 8, 7, rgb(150, 80, 55));
    fillRect(i, 3, 6, 10, 2, rgb(170, 95, 65));
    fillRect(i, 5, 6, 6, 1, rgb(90, 60, 40));
    outline(i);
    return i;
  }
  if (n == "item_frame" || n == "painting") {
    Image i = blank();
    fillRect(i, 1, 1, 14, 14, rgb(130, 90, 50));
    if (n == "item_frame") fillRect(i, 3, 3, 10, 10, rgb(150, 110, 80));
    else {
      fillRect(i, 3, 3, 10, 10, rgb(120, 180, 230));
      fillRect(i, 3, 9, 10, 4, rgb(90, 150, 60));
      disc(i, 10.5, 5.5, 1.6, rgb(250, 230, 100), rgb(250, 210, 60));
    }
    outline(i);
    return i;
  }
  if (n == "skull") {
    Image i = blank();
    fillRect(i, 3, 3, 10, 10, rgb(200, 195, 180));
    fillRect(i, 4, 6, 3, 3, rgb(30, 30, 30)); fillRect(i, 9, 6, 3, 3, rgb(30, 30, 30));
    fillRect(i, 7, 10, 2, 1, rgb(60, 60, 60));
    outline(i);
    return i;
  }
  if (n == "banner") {
    Image i = blank();
    fillRect(i, 7, 1, 2, 14, rgb(120, 90, 50));
    fillRect(i, 2, 1, 12, 1, rgb(120, 90, 50));
    fillRect(i, 3, 2, 10, 11, rgb(235, 235, 235));
    outline(i);
    return i;
  }
  if (n == "armor_stand") {
    Image i = blank();
    fillRect(i, 7, 1, 2, 13, rgb(162, 130, 78));
    fillRect(i, 3, 4, 10, 2, rgb(162, 130, 78));
    fillRect(i, 5, 9, 6, 1, rgb(162, 130, 78));
    fillRect(i, 3, 14, 10, 2, rgb(150, 150, 150));
    return i;
  }
  if (n == "saddle") {
    Image i = blank();
    fillRect(i, 2, 5, 12, 5, rgb(120, 65, 35));
    fillRect(i, 3, 4, 3, 1, rgb(140, 80, 45));
    fillRect(i, 7, 10, 2, 4, rgb(80, 45, 25));
    fillRect(i, 6, 13, 4, 2, rgb(190, 190, 190));
    outline(i);
    return i;
  }
  if (n == "boat") {
    Image i = blank();
    for (int y = 7; y < 12; y++) fillRect(i, 1 + (y - 7), y, 14 - 2 * (y - 7), 1, shade(rgb(150, 115, 65), (y % 2) * 12));
    fillRect(i, 1, 6, 14, 1, rgb(110, 80, 45));
    outline(i);
    return i;
  }
  if (n == "compass" || n == "clock") {
    Image i = blank();
    disc(i, 7.5, 7.5, 6.0, n == "compass" ? rgb(170, 170, 175) : gold, rgb(70, 70, 70));
    disc(i, 7.5, 7.5, 4.2, n == "compass" ? rgb(90, 90, 95) : rgb(60, 110, 200), rgb(60, 60, 60));
    if (n == "compass") { line(i, 7, 7, 7, 3, rgb(220, 40, 30)); line(i, 8, 8, 8, 12, rgb(230, 230, 230)); }
    else { fillRect(i, 3, 8, 10, 4, rgb(40, 50, 80)); disc(i, 10.5, 5.5, 1.3, rgb(250, 230, 100), rgb(250, 210, 60)); }
    return i;
  }
  if (n == "fishing_rod" || n == "carrot_on_a_stick") {
    Image i = rod(rgb(120, 85, 45), 0);
    line(i, 12, 4, 13, 13, rgb(230, 230, 230));
    if (n == "carrot_on_a_stick") { fillRect(i, 12, 12, 2, 3, rgb(240, 140, 30)); px(i, 13, 11, rgb(70, 160, 50)); }
    else px(i, 13, 13, rgb(170, 170, 175));
    return i;
  }
  if (n == "blaze_rod") return rod(rgb(240, 190, 40), rgb(255, 240, 150));
  if (n == "flint_and_steel") {
    Image i = blank();
    disc(i, 5.5, 10.5, 3.0, rgb(60, 60, 65), rgb(35, 35, 40));
    line(i, 8, 4, 13, 4, rgb(200, 200, 205)); line(i, 13, 4, 13, 9, rgb(200, 200, 205)); line(i, 8, 5, 12, 5, rgb(150, 150, 155));
    return i;
  }
  if (n == "lead") {
    Image i = blank();
    for (int k = 0; k < 20; k++) px(i, 2 + (k * 11) / 19, 3 + static_cast<int>(5 + std::sin(k * 0.6) * 4), rgb(170, 140, 100));
    disc(i, 11.5, 11.5, 2.5, 0, rgb(170, 140, 100));
    return i;
  }
  if (n == "name_tag") {
    Image i = blank();
    fillRect(i, 4, 3, 9, 9, rgb(225, 210, 170));
    line(i, 3, 12, 5, 14, rgb(170, 140, 100));
    disc(i, 11.0, 4.5, 1.0, rgb(90, 90, 90), rgb(90, 90, 90));
    outline(i);
    return i;
  }
  if (n == "bowl" || n == "mushroom_stew" || n == "rabbit_stew") {
    Image i = blank();
    for (int y = 7; y < 13; y++) fillRect(i, 2 + (y - 7) / 2, y, 12 - (y - 7), 1, rgb(140, 100, 60));
    if (n != "bowl") fillRect(i, 3, 7, 10, 2, n == "mushroom_stew" ? rgb(190, 140, 110) : rgb(170, 110, 60));
    if (n == "rabbit_stew") { px(i, 5, 7, rgb(240, 140, 30)); px(i, 9, 7, rgb(90, 160, 60)); }
    outline(i);
    return i;
  }
  if (n.find("horse_armor") != std::string::npos) {
    const u32 c = n.rfind("iron", 0) == 0 ? iron : (n.rfind("golden", 0) == 0 ? gold : diamond);
    Image i = blank();
    fillRect(i, 2, 5, 10, 6, c);
    fillRect(i, 10, 2, 4, 5, c);
    fillRect(i, 2, 5, 10, 1, shade(c, 30));
    fillRect(i, 3, 11, 2, 3, shade(c, -40)); fillRect(i, 9, 11, 2, 3, shade(c, -40));
    outline(i);
    return i;
  }
  if (n == "fireworks") {
    Image i = blank();
    fillRect(i, 6, 5, 4, 8, rgb(200, 40, 40));
    fillRect(i, 6, 7, 4, 1, rgb(230, 230, 230));
    fillRect(i, 7, 3, 2, 2, rgb(150, 150, 150));
    line(i, 8, 13, 8, 15, rgb(120, 85, 45));
    outline(i);
    return i;
  }
  if (n == "spawn_egg") {
    Image i = blank();
    for (int y = 3; y < 14; y++) {
      const double half = std::sqrt(std::max(0.0, 1.0 - std::pow((y - 8.5) / 5.6, 2))) * (y < 8 ? 3.4 : 4.2);
      line(i, static_cast<int>(7.5 - half), y, static_cast<int>(7.5 + half), y, rgb(210, 190, 140));
    }
    speckle(i, rgb(90, 130, 70), 6, 2, 3);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (!(i.get(x, y) >> 24)) i.set(x, y, 0);
    outline(i);
    return i;
  }
  if (n == "nether_star") {
    Image i = blank();
    for (int k = 0; k < 4; k++) {
      line(i, 7, 7, 7 + (k % 2 ? 6 : -6), 7 + (k < 2 ? 6 : -6), rgb(240, 240, 250));
    }
    fillRect(i, 5, 5, 5, 5, rgb(250, 250, 255));
    line(i, 7, 1, 7, 14, rgb(220, 220, 235));
    line(i, 1, 7, 14, 7, rgb(220, 220, 235));
    return i;
  }
  if (n == "quartz" || n == "prismarine_crystals" || n == "prismarine_shard" || n == "ghast_tear") {
    Image i = blank();
    const u32 c = n == "quartz" ? rgb(235, 230, 222) : (n == "prismarine_shard" ? rgb(90, 160, 140) : (n == "ghast_tear" ? rgb(200, 230, 240) : rgb(180, 230, 210)));
    if (n == "ghast_tear") { disc(i, 7.5, 9.5, 3.2, c, shade(c, -40)); line(i, 7, 3, 7, 7, c); }
    else if (n == "prismarine_shard") { for (int y = 2; y < 14; y++) fillRect(i, 8 - (14 - y) / 3, y, 1 + (14 - y) / 3, 1, shade(c, (y % 3) * 10)); }
    else { for (int k = 0; k < 3; k++) { fillRect(i, 3 + k * 4, 5 + (k % 2) * 3, 3, 3, c); px(i, 3 + k * 4, 5 + (k % 2) * 3, shade(c, 25)); } }
    outline(i);
    return i;
  }
  if (n == "netherbrick") {
    Image i = blank();
    for (int y = 6; y < 11; y++) fillRect(i, 3 + (10 - y) / 2, y, 10 - (10 - y), 1, y == 6 ? rgb(90, 45, 50) : rgb(60, 25, 30));
    outline(i);
    return i;
  }
  if (n == "slime_ball") return blob(rgb(110, 190, 90, 230), 4.0);
  if (n == "ender_pearl") return blob(rgb(30, 90, 80), 4.2);
  if (n == "ender_eye") { Image i = blob(rgb(40, 110, 90), 4.2); disc(i, 7.5, 8.0, 1.5, rgb(10, 30, 25), rgb(10, 30, 25)); return i; }
  if (n == "magma_cream") return blob(rgb(210, 110, 40), 4.0);
  if (n == "fire_charge") { Image i = blob(rgb(60, 40, 30), 4.5); speckle(i, rgb(240, 140, 30), 5, 2, 5); for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (std::hypot(x - 7.5, y - 8) > 4.5) i.set(x, y, 0); return i; }
  if (n == "firework_charge") { Image i = blob(rgb(60, 60, 60), 4.2); return i; }
  if (n == "gold_nugget") { Image i = blank(); disc(i, 7.5, 8.5, 2.6, gold, shade(gold, -50)); return i; }
  if (n == "glowstone_dust") return dust(rgb(240, 210, 110), 21);
  if (n == "sugar") return dust(rgb(250, 250, 250), 22);
  if (n == "blaze_powder") return dust(rgb(245, 170, 40), 23);
  if (n == "nether_wart") { Image i = blank(); for (auto [x, y] : {std::pair{5, 7}, {9, 6}, {7, 10}, {11, 10}}) disc(i, x + 0.5, y + 0.5, 2.0, rgb(170, 30, 40), rgb(110, 15, 25)); return i; }
  if (n == "pumpkin_seeds" || n == "melon_seeds") {
    Image i = blank();
    const u32 c = n == "pumpkin_seeds" ? rgb(230, 225, 180) : rgb(40, 30, 25);
    for (auto [x, y] : {std::pair{4, 5}, {9, 4}, {6, 9}, {11, 9}, {8, 12}}) { px(i, x, y, c); px(i, x + 1, y, shade(c, -20)); px(i, x, y + 1, shade(c, -30)); }
    return i;
  }
  if (n == "melon" || n == "speckled_melon") {
    Image i = blank();
    for (int y = 4; y < 14; y++) for (int x = 2; x < 14; x++) {
      const double d = std::hypot(x - 7.5, y - 14.0);
      if (d < 10 && y < 14) i.set(x, y, d > 8.8 ? rgb(90, 140, 40) : (d > 8.0 ? rgb(230, 230, 180) : rgb(220, 50, 50)));
    }
    if (n == "speckled_melon") speckle(i, gold, 5, 2, 7);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) if (std::hypot(x - 7.5, y - 14.0) >= 10 || y >= 14) i.set(x, y, 0);
    outline(i);
    return i;
  }
  if (n == "cookie") { Image i = blob(rgb(200, 140, 70), 4.8); speckle(i, rgb(70, 40, 20), 5, 1, 9); return i; }
  if (n == "pumpkin_pie") { Image i = blank(); fillRect(i, 2, 6, 12, 6, rgb(200, 140, 70)); fillRect(i, 3, 6, 10, 2, rgb(220, 120, 40)); outline(i); return i; }
  if (n == "golden_carrot" || n == "poisonous_potato") {
    Image i = blank();
    const u32 c = n == "golden_carrot" ? gold : rgb(170, 190, 90);
    for (int y = 4; y < 14; y++) line(i, 8 - (14 - y) / 3, y, 8 + (14 - y) / 3, y, shade(c, (y % 2) * 12));
    if (n == "golden_carrot") for (int k = 0; k < 4; k++) line(i, 6 + k, 1, 7 + k / 2, 4, rgb(70, 160, 50));
    outline(i);
    return i;
  }
  if (n == "fish" || n == "cooked_fish") {
    Image i = blank();
    const u32 c = n == "fish" ? rgb(120, 150, 170) : rgb(170, 120, 70);
    for (int x = 2; x < 12; x++) {
      const int h = static_cast<int>(3 * std::sin((x - 1) / 11.0 * 3.14159)) + 1;
      fillRect(i, x, 8 - h, 1, 2 * h, shade(c, (x % 3) * 8));
    }
    for (int k = 0; k < 3; k++) { px(i, 12 + k, 6 - k, c); px(i, 12 + k, 9 + k, c); fillRect(i, 12, 7, 1, 2, c); }
    px(i, 4, 7, rgb(20, 20, 20));
    outline(i);
    return i;
  }
  if (n == "rabbit" || n == "cooked_rabbit") {
    Image i = blank();
    const u32 c = n == "rabbit" ? rgb(220, 150, 140) : rgb(190, 120, 70);
    disc(i, 7.5, 9.0, 4.2, c, shade(c, -45));
    line(i, 10, 5, 13, 2, rgb(235, 230, 215));
    return i;
  }
  if (n == "rabbit_foot") { Image i = blank(); fillRect(i, 5, 3, 5, 9, rgb(200, 170, 130)); fillRect(i, 4, 11, 7, 3, rgb(215, 190, 150)); outline(i); return i; }
  if (n == "rabbit_hide") { Image i = blank(); fillRect(i, 3, 3, 10, 10, rgb(180, 140, 100)); for (int k = 3; k < 13; k += 3) px(i, k, 3, 0); outline(i); return i; }
  if (n == "fermented_spider_eye") {
    Image i = blob(rgb(170, 40, 60), 4.2);
    fillRect(i, 4, 3, 7, 3, rgb(160, 100, 60));
    speckle(i, rgb(230, 230, 230), 3, 1, 4);
    return i;
  }
  return {};
}

}  // namespace

void addAllItems(MemoryPack& pack) {
  // Todos los objetos (id >= 256) que aún no tienen modelo
  for (int id = 256; id < 512; id++) {
    const ItemInfo& info = itemInfo(id);
    if (!info.exists) continue;
    const std::string name(info.name);
    if (pack.exists("assets/minecraft/models/item/" + name + ".json")) continue;
    Image img = draw(name);
    if (img.empty()) {
      // Sin dibujo propio: una bolita de color estable para que nunca salga el cuadro rosa
      img = blob(shade(rgb(150, 120, 90), static_cast<int>(seedOf(name) % 60) - 30));
    }
    putItem(pack, name, img);
  }
  // Tintes: 16 colores (nombres de modelo "dye_<color>" de 1.8)
  static const std::pair<const char*, u32> dyes[16] = {
      {"black", rgb(30, 28, 30)},   {"red", rgb(180, 40, 35)},     {"green", rgb(70, 95, 35)},  {"brown", rgb(100, 60, 35)},
      {"blue", rgb(40, 70, 200)},   {"purple", rgb(130, 60, 185)}, {"cyan", rgb(50, 120, 140)}, {"silver", rgb(160, 165, 165)},
      {"gray", rgb(80, 80, 80)},    {"pink", rgb(225, 140, 165)},  {"lime", rgb(110, 195, 50)}, {"yellow", rgb(235, 215, 50)},
      {"light_blue", rgb(110, 150, 220)}, {"magenta", rgb(190, 80, 195)}, {"orange", rgb(230, 130, 40)}, {"white", rgb(240, 240, 235)}};
  for (auto [color, c] : dyes) {
    const std::string name = std::string("dye_") + color;
    if (pack.exists("assets/minecraft/models/item/" + name + ".json")) continue;
    Image img;
    if (std::string(color) == "black") img = blob(c, 4.0);  // bolsa de tinta
    else if (std::string(color) == "brown") { img = blob(c, 3.2); }  // granos de cacao
    else if (std::string(color) == "white") img = dust(c, 31);  // polvo de hueso
    else img = dust(c, seedOf(name));
    putItem(pack, name, img);
  }
  // Bloques que en 1.8 se ven como sprite plano en la mano y el inventario
  static const std::pair<const char*, const char*> flatBlocks[] = {
      {"ladder", "blocks/ladder"},           {"lever", "blocks/lever"},
      {"redstone_torch", "blocks/redstone_torch_on"}, {"rail", "blocks/rail_normal"},
      {"golden_rail", "blocks/rail_golden"},  {"detector_rail", "blocks/rail_detector"},
      {"activator_rail", "blocks/rail_activator"}, {"vine", "blocks/vine"},
      {"iron_bars", "blocks/iron_bars"},      {"glass_pane", "blocks/glass"},
      {"tripwire_hook", "blocks/trip_wire_source"}, {"torch", "blocks/torch_on"}};
  for (auto [name, tex] : flatBlocks)
    pack.putJson(std::string("assets/minecraft/models/item/") + name + ".json",
                 {{"parent", "builtin/generated"}, {"textures", {{"layer0", tex}}}});
  for (const auto& [color, c] : dyeColors())
    pack.putJson("assets/minecraft/models/item/" + color + "_stained_glass_pane.json",
                 {{"parent", "builtin/generated"}, {"textures", {{"layer0", "blocks/glass_" + color}}}});
  // Textura de "falta" de los ítems: una barrera (círculo rojo tachado)
  Image barrier = blank();
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const double d = std::hypot(x - 7.5, y - 7.5);
      if ((d > 5.2 && d < 7.3) || (std::abs((x - 7.5) - (y - 7.5)) < 1.2 && d < 6)) barrier.set(x, y, rgb(220, 30, 30));
    }
  pack.putImage(kTex + "items/barrier.png", barrier);
  pack.putJson("assets/minecraft/models/item/barrier.json", {{"parent", "builtin/generated"}, {"textures", {{"layer0", "items/barrier"}}}});
}

}  // namespace mcw::cc0
