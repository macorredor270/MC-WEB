// Nombres en español de todos los bloques y objetos de 1.8 (traducción propia).
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include "data/items.h"

namespace mcw {
namespace {

struct Names {
  std::unordered_map<std::string_view, std::string_view> byName;
  std::map<std::pair<int, int>, std::string> byVariant;
};

// Colores de los tintes de 1.8 (lana, cristal, arcilla, alfombras) en el orden de sus metadatos
constexpr std::array<const char*, 16> kColorM = {"blanco", "naranja", "magenta", "azul claro", "amarillo", "lima", "rosa", "gris",
                                                 "gris claro", "cian", "morado", "azul", "marrón", "verde", "rojo", "negro"};
constexpr std::array<const char*, 16> kColorF = {"blanca", "naranja", "magenta", "azul claro", "amarilla", "lima", "rosa", "gris",
                                                 "gris claro", "cian", "morada", "azul", "marrón", "verde", "roja", "negra"};
constexpr std::array<const char*, 6> kWoods = {"roble", "abeto", "abedul", "jungla", "acacia", "roble oscuro"};

const Names& names() {
  static const Names n = [] {
    Names r;
    auto& b = r.byName;
    b = {
        {"stone", "Piedra"}, {"grass", "Bloque de hierba"}, {"dirt", "Tierra"}, {"cobblestone", "Roca"},
        {"planks", "Tablones"}, {"sapling", "Brote"}, {"bedrock", "Piedra base"}, {"sand", "Arena"}, {"gravel", "Grava"},
        {"gold_ore", "Mena de oro"}, {"iron_ore", "Mena de hierro"}, {"coal_ore", "Mena de carbón"}, {"log", "Tronco"},
        {"leaves", "Hojas"}, {"sponge", "Esponja"}, {"glass", "Cristal"}, {"lapis_ore", "Mena de lapislázuli"},
        {"lapis_block", "Bloque de lapislázuli"}, {"dispenser", "Dispensador"}, {"sandstone", "Arenisca"},
        {"noteblock", "Bloque musical"}, {"golden_rail", "Raíl propulsor"}, {"detector_rail", "Raíl detector"},
        {"sticky_piston", "Pistón pegajoso"}, {"web", "Telaraña"}, {"tallgrass", "Hierba alta"}, {"deadbush", "Arbusto muerto"},
        {"piston", "Pistón"}, {"wool", "Lana"}, {"yellow_flower", "Diente de león"}, {"red_flower", "Amapola"},
        {"brown_mushroom", "Champiñón"}, {"red_mushroom", "Seta roja"}, {"gold_block", "Bloque de oro"},
        {"iron_block", "Bloque de hierro"}, {"stone_slab", "Losa de piedra"}, {"brick_block", "Ladrillos"}, {"tnt", "Dinamita"},
        {"bookshelf", "Librería"}, {"mossy_cobblestone", "Roca musgosa"}, {"obsidian", "Obsidiana"}, {"torch", "Antorcha"},
        {"mob_spawner", "Generador de monstruos"}, {"oak_stairs", "Escaleras de roble"}, {"chest", "Cofre"},
        {"diamond_ore", "Mena de diamante"}, {"diamond_block", "Bloque de diamante"}, {"crafting_table", "Mesa de trabajo"},
        {"farmland", "Tierra de cultivo"}, {"furnace", "Horno"}, {"ladder", "Escalera de mano"}, {"rail", "Raíles"},
        {"stone_stairs", "Escaleras de roca"}, {"lever", "Palanca"}, {"stone_pressure_plate", "Placa de presión de piedra"},
        {"wooden_pressure_plate", "Placa de presión de madera"}, {"redstone_ore", "Mena de redstone"},
        {"redstone_torch", "Antorcha de redstone"}, {"stone_button", "Botón de piedra"}, {"snow_layer", "Nieve"}, {"ice", "Hielo"},
        {"snow", "Bloque de nieve"}, {"cactus", "Cactus"}, {"clay", "Bloque de arcilla"}, {"jukebox", "Tocadiscos"},
        {"fence", "Valla de roble"}, {"pumpkin", "Calabaza"}, {"netherrack", "Netherrack"}, {"soul_sand", "Arena de almas"},
        {"glowstone", "Piedra luminosa"}, {"lit_pumpkin", "Calabaza de Halloween"}, {"stained_glass", "Cristal tintado"},
        {"trapdoor", "Trampilla de madera"}, {"monster_egg", "Bloque infestado"}, {"stonebrick", "Ladrillos de piedra"},
        {"brown_mushroom_block", "Bloque de champiñón"}, {"red_mushroom_block", "Bloque de seta roja"},
        {"iron_bars", "Barrotes de hierro"}, {"glass_pane", "Panel de cristal"}, {"melon_block", "Sandía"}, {"vine", "Enredaderas"},
        {"fence_gate", "Puerta de valla de roble"}, {"brick_stairs", "Escaleras de ladrillo"},
        {"stone_brick_stairs", "Escaleras de ladrillos de piedra"}, {"mycelium", "Micelio"}, {"waterlily", "Nenúfar"},
        {"nether_brick", "Ladrillos del Nether"}, {"nether_brick_fence", "Valla de ladrillos del Nether"},
        {"nether_brick_stairs", "Escaleras de ladrillos del Nether"}, {"enchanting_table", "Mesa de encantamientos"},
        {"end_portal_frame", "Marco del portal del End"}, {"end_stone", "Piedra del End"}, {"dragon_egg", "Huevo de dragón"},
        {"redstone_lamp", "Lámpara de redstone"}, {"wooden_slab", "Losa de madera"}, {"sandstone_stairs", "Escaleras de arenisca"},
        {"emerald_ore", "Mena de esmeralda"}, {"ender_chest", "Cofre de ender"}, {"tripwire_hook", "Gancho de cable trampa"},
        {"emerald_block", "Bloque de esmeralda"}, {"spruce_stairs", "Escaleras de abeto"}, {"birch_stairs", "Escaleras de abedul"},
        {"jungle_stairs", "Escaleras de jungla"}, {"command_block", "Bloque de comandos"}, {"beacon", "Faro"},
        {"cobblestone_wall", "Muro de roca"}, {"wooden_button", "Botón de madera"}, {"anvil", "Yunque"},
        {"trapped_chest", "Cofre trampa"}, {"light_weighted_pressure_plate", "Placa de presión de peso ligero"},
        {"heavy_weighted_pressure_plate", "Placa de presión de peso pesado"}, {"daylight_detector", "Sensor de luz solar"},
        {"redstone_block", "Bloque de redstone"}, {"quartz_ore", "Mena de cuarzo del Nether"}, {"hopper", "Tolva"},
        {"quartz_block", "Bloque de cuarzo"}, {"quartz_stairs", "Escaleras de cuarzo"}, {"activator_rail", "Raíl activador"},
        {"dropper", "Soltador"}, {"stained_hardened_clay", "Arcilla tintada"}, {"stained_glass_pane", "Panel de cristal tintado"},
        {"leaves2", "Hojas"}, {"log2", "Tronco"}, {"acacia_stairs", "Escaleras de acacia"},
        {"dark_oak_stairs", "Escaleras de roble oscuro"}, {"slime", "Bloque de slime"}, {"barrier", "Barrera"},
        {"iron_trapdoor", "Trampilla de hierro"}, {"prismarine", "Prismarina"}, {"sea_lantern", "Linterna del mar"},
        {"hay_block", "Bala de heno"}, {"carpet", "Alfombra"}, {"hardened_clay", "Arcilla endurecida"},
        {"coal_block", "Bloque de carbón"}, {"packed_ice", "Hielo compacto"}, {"double_plant", "Planta doble"},
        {"red_sandstone", "Arenisca roja"}, {"red_sandstone_stairs", "Escaleras de arenisca roja"},
        {"stone_slab2", "Losa de arenisca roja"}, {"spruce_fence_gate", "Puerta de valla de abeto"},
        {"birch_fence_gate", "Puerta de valla de abedul"}, {"jungle_fence_gate", "Puerta de valla de jungla"},
        {"dark_oak_fence_gate", "Puerta de valla de roble oscuro"}, {"acacia_fence_gate", "Puerta de valla de acacia"},
        {"spruce_fence", "Valla de abeto"}, {"birch_fence", "Valla de abedul"}, {"jungle_fence", "Valla de jungla"},
        {"dark_oak_fence", "Valla de roble oscuro"}, {"acacia_fence", "Valla de acacia"},
        // Objetos
        {"iron_shovel", "Pala de hierro"}, {"iron_pickaxe", "Pico de hierro"}, {"iron_axe", "Hacha de hierro"},
        {"flint_and_steel", "Mechero"}, {"apple", "Manzana"}, {"bow", "Arco"}, {"arrow", "Flecha"}, {"coal", "Carbón"},
        {"diamond", "Diamante"}, {"iron_ingot", "Lingote de hierro"}, {"gold_ingot", "Lingote de oro"},
        {"iron_sword", "Espada de hierro"}, {"wooden_sword", "Espada de madera"}, {"wooden_shovel", "Pala de madera"},
        {"wooden_pickaxe", "Pico de madera"}, {"wooden_axe", "Hacha de madera"}, {"stone_sword", "Espada de piedra"},
        {"stone_shovel", "Pala de piedra"}, {"stone_pickaxe", "Pico de piedra"}, {"stone_axe", "Hacha de piedra"},
        {"diamond_sword", "Espada de diamante"}, {"diamond_shovel", "Pala de diamante"}, {"diamond_pickaxe", "Pico de diamante"},
        {"diamond_axe", "Hacha de diamante"}, {"stick", "Palo"}, {"bowl", "Cuenco"}, {"mushroom_stew", "Estofado de champiñones"},
        {"golden_sword", "Espada de oro"}, {"golden_shovel", "Pala de oro"}, {"golden_pickaxe", "Pico de oro"},
        {"golden_axe", "Hacha de oro"}, {"string", "Cuerda"}, {"feather", "Pluma"}, {"gunpowder", "Pólvora"},
        {"wooden_hoe", "Azada de madera"}, {"stone_hoe", "Azada de piedra"}, {"iron_hoe", "Azada de hierro"},
        {"diamond_hoe", "Azada de diamante"}, {"golden_hoe", "Azada de oro"}, {"wheat_seeds", "Semillas"}, {"wheat", "Trigo"},
        {"bread", "Pan"}, {"leather_helmet", "Gorro de cuero"}, {"leather_chestplate", "Túnica de cuero"},
        {"leather_leggings", "Pantalones de cuero"}, {"leather_boots", "Botas de cuero"}, {"chainmail_helmet", "Casco de cota de malla"},
        {"chainmail_chestplate", "Pechera de cota de malla"}, {"chainmail_leggings", "Grebas de cota de malla"},
        {"chainmail_boots", "Botas de cota de malla"}, {"iron_helmet", "Casco de hierro"}, {"iron_chestplate", "Pechera de hierro"},
        {"iron_leggings", "Grebas de hierro"}, {"iron_boots", "Botas de hierro"}, {"diamond_helmet", "Casco de diamante"},
        {"diamond_chestplate", "Pechera de diamante"}, {"diamond_leggings", "Grebas de diamante"},
        {"diamond_boots", "Botas de diamante"}, {"golden_helmet", "Casco de oro"}, {"golden_chestplate", "Pechera de oro"},
        {"golden_leggings", "Grebas de oro"}, {"golden_boots", "Botas de oro"}, {"flint", "Pedernal"}, {"porkchop", "Chuleta cruda"},
        {"cooked_porkchop", "Chuleta cocinada"}, {"painting", "Cuadro"}, {"golden_apple", "Manzana dorada"}, {"sign", "Cartel"},
        {"wooden_door", "Puerta de roble"}, {"bucket", "Cubo"}, {"water_bucket", "Cubo de agua"}, {"lava_bucket", "Cubo de lava"},
        {"minecart", "Vagoneta"}, {"saddle", "Silla de montar"}, {"iron_door", "Puerta de hierro"}, {"redstone", "Redstone"},
        {"snowball", "Bola de nieve"}, {"boat", "Barca"}, {"leather", "Cuero"}, {"milk_bucket", "Cubo de leche"}, {"brick", "Ladrillo"},
        {"clay_ball", "Bola de arcilla"}, {"reeds", "Caña de azúcar"}, {"paper", "Papel"}, {"book", "Libro"},
        {"slime_ball", "Bola de slime"}, {"chest_minecart", "Vagoneta con cofre"}, {"furnace_minecart", "Vagoneta con horno"},
        {"egg", "Huevo"}, {"compass", "Brújula"}, {"fishing_rod", "Caña de pescar"}, {"clock", "Reloj"},
        {"glowstone_dust", "Polvo de piedra luminosa"}, {"fish", "Pescado crudo"}, {"cooked_fish", "Pescado cocinado"},
        {"dye", "Tinte"}, {"bone", "Hueso"}, {"sugar", "Azúcar"}, {"cake", "Pastel"}, {"bed", "Cama"},
        {"repeater", "Repetidor de redstone"}, {"cookie", "Galleta"}, {"filled_map", "Mapa"}, {"shears", "Tijeras"},
        {"melon", "Rodaja de sandía"}, {"pumpkin_seeds", "Semillas de calabaza"}, {"melon_seeds", "Semillas de sandía"},
        {"beef", "Filete crudo"}, {"cooked_beef", "Filete"}, {"chicken", "Pollo crudo"}, {"cooked_chicken", "Pollo asado"},
        {"rotten_flesh", "Carne podrida"}, {"ender_pearl", "Perla de ender"}, {"blaze_rod", "Vara de blaze"},
        {"ghast_tear", "Lágrima de ghast"}, {"gold_nugget", "Pepita de oro"}, {"nether_wart", "Verruga del Nether"},
        {"potion", "Poción"}, {"glass_bottle", "Frasco"}, {"spider_eye", "Ojo de araña"},
        {"fermented_spider_eye", "Ojo de araña fermentado"}, {"blaze_powder", "Polvo de blaze"}, {"magma_cream", "Crema de magma"},
        {"brewing_stand", "Soporte para pociones"}, {"cauldron", "Caldero"}, {"ender_eye", "Ojo de ender"},
        {"speckled_melon", "Rodaja de sandía reluciente"}, {"spawn_egg", "Huevo generador"}, {"experience_bottle", "Frasco de experiencia"},
        {"fire_charge", "Carga de fuego"}, {"writable_book", "Libro y pluma"}, {"written_book", "Libro escrito"},
        {"emerald", "Esmeralda"}, {"item_frame", "Marco"}, {"flower_pot", "Maceta"}, {"carrot", "Zanahoria"}, {"potato", "Patata"},
        {"baked_potato", "Patata asada"}, {"poisonous_potato", "Patata venenosa"}, {"map", "Mapa vacío"},
        {"golden_carrot", "Zanahoria dorada"}, {"skull", "Cabeza"}, {"carrot_on_a_stick", "Caña con zanahoria"},
        {"nether_star", "Estrella del Nether"}, {"pumpkin_pie", "Tarta de calabaza"}, {"fireworks", "Cohete de fuegos artificiales"},
        {"firework_charge", "Estrella de fuegos artificiales"}, {"enchanted_book", "Libro encantado"},
        {"comparator", "Comparador de redstone"}, {"netherbrick", "Ladrillo del Nether"}, {"quartz", "Cuarzo del Nether"},
        {"tnt_minecart", "Vagoneta con dinamita"}, {"hopper_minecart", "Vagoneta con tolva"},
        {"prismarine_shard", "Fragmento de prismarina"}, {"prismarine_crystals", "Cristales de prismarina"},
        {"rabbit", "Conejo crudo"}, {"cooked_rabbit", "Conejo cocinado"}, {"rabbit_stew", "Estofado de conejo"},
        {"rabbit_foot", "Pata de conejo"}, {"rabbit_hide", "Piel de conejo"}, {"armor_stand", "Soporte para armadura"},
        {"iron_horse_armor", "Armadura de hierro para caballo"}, {"golden_horse_armor", "Armadura de oro para caballo"},
        {"diamond_horse_armor", "Armadura de diamante para caballo"}, {"lead", "Rienda"}, {"name_tag", "Etiqueta"},
        {"command_block_minecart", "Vagoneta con bloque de comandos"}, {"mutton", "Cordero crudo"}, {"cooked_mutton", "Cordero asado"},
        {"banner", "Estandarte"}, {"spruce_door", "Puerta de abeto"}, {"birch_door", "Puerta de abedul"},
        {"jungle_door", "Puerta de jungla"}, {"acacia_door", "Puerta de acacia"}, {"dark_oak_door", "Puerta de roble oscuro"},
        {"record_13", "Disco"}, {"record_cat", "Disco"}, {"record_blocks", "Disco"}, {"record_chirp", "Disco"}, {"record_far", "Disco"},
        {"record_mall", "Disco"}, {"record_mellohi", "Disco"}, {"record_stal", "Disco"}, {"record_strad", "Disco"},
        {"record_ward", "Disco"}, {"record_11", "Disco"}, {"record_wait", "Disco"},
    };

    auto& v = r.byVariant;
    const char* stones[] = {"Piedra", "Granito", "Granito pulido", "Diorita", "Diorita pulida", "Andesita", "Andesita pulida"};
    for (int m = 0; m < 7; m++) v[{1, m}] = stones[m];
    v[{3, 0}] = "Tierra";
    v[{3, 1}] = "Tierra estéril";
    v[{3, 2}] = "Podsol";
    for (int m = 0; m < 6; m++) {
      v[{5, m}] = std::string("Tablones de ") + kWoods[m];
      v[{6, m}] = std::string("Brote de ") + kWoods[m];
      v[{126, m}] = std::string("Losa de ") + kWoods[m];
      v[{m < 4 ? 17 : 162, m % 4}] = std::string("Tronco de ") + kWoods[m];
      v[{m < 4 ? 18 : 161, m % 4}] = std::string("Hojas de ") + kWoods[m];
    }
    v[{12, 0}] = "Arena";
    v[{12, 1}] = "Arena roja";
    v[{19, 0}] = "Esponja";
    v[{19, 1}] = "Esponja mojada";
    v[{24, 0}] = "Arenisca";
    v[{24, 1}] = "Arenisca cincelada";
    v[{24, 2}] = "Arenisca lisa";
    v[{179, 0}] = "Arenisca roja";
    v[{179, 1}] = "Arenisca roja cincelada";
    v[{179, 2}] = "Arenisca roja lisa";
    v[{31, 0}] = "Arbusto";
    v[{31, 1}] = "Hierba";
    v[{31, 2}] = "Helecho";
    const char* flowers[] = {"Amapola", "Orquídea azul", "Allium", "Azure bluet", "Tulipán rojo", "Tulipán naranja",
                             "Tulipán blanco", "Tulipán rosa", "Margarita"};
    for (int m = 0; m < 9; m++) v[{38, m}] = flowers[m];
    const char* slabs[] = {"Losa de piedra", "Losa de arenisca", "Losa de madera", "Losa de roca", "Losa de ladrillos",
                           "Losa de ladrillos de piedra", "Losa de ladrillos del Nether", "Losa de cuarzo"};
    for (int m = 0; m < 8; m++) v[{44, m}] = slabs[m];
    const char* infested[] = {"Piedra infestada", "Roca infestada", "Ladrillos de piedra infestados", "Ladrillos musgosos infestados",
                              "Ladrillos agrietados infestados", "Ladrillos cincelados infestados"};
    for (int m = 0; m < 6; m++) v[{97, m}] = infested[m];
    const char* bricks[] = {"Ladrillos de piedra", "Ladrillos de piedra musgosos", "Ladrillos de piedra agrietados",
                            "Ladrillos de piedra cincelados"};
    for (int m = 0; m < 4; m++) v[{98, m}] = bricks[m];
    v[{139, 0}] = "Muro de roca";
    v[{139, 1}] = "Muro de roca musgosa";
    v[{145, 0}] = "Yunque";
    v[{145, 1}] = "Yunque algo dañado";
    v[{145, 2}] = "Yunque muy dañado";
    v[{155, 0}] = "Bloque de cuarzo";
    v[{155, 1}] = "Bloque de cuarzo cincelado";
    v[{155, 2}] = "Pilar de cuarzo";
    v[{168, 0}] = "Prismarina";
    v[{168, 1}] = "Ladrillos de prismarina";
    v[{168, 2}] = "Prismarina oscura";
    const char* plants[] = {"Girasol", "Lila", "Hierba alta", "Helecho grande", "Rosal", "Peonía"};
    for (int m = 0; m < 6; m++) v[{175, m}] = plants[m];
    for (int m = 0; m < 16; m++) {
      v[{35, m}] = std::string("Lana ") + kColorF[m];
      v[{95, m}] = std::string("Cristal tintado ") + kColorM[m];
      v[{159, m}] = std::string("Arcilla tintada ") + kColorF[m];
      v[{160, m}] = std::string("Panel de cristal tintado ") + kColorM[m];
      v[{171, m}] = std::string("Alfombra ") + kColorF[m];
      v[{425, m}] = std::string("Estandarte ") + kColorM[15 - m];  // los estandartes van al revés (0 = negro)
    }
    v[{263, 0}] = "Carbón";
    v[{263, 1}] = "Carbón vegetal";
    v[{322, 0}] = "Manzana dorada";
    v[{322, 1}] = "Manzana dorada encantada";
    const char* fish[] = {"Pescado crudo", "Salmón crudo", "Pez payaso", "Pez globo"};
    for (int m = 0; m < 4; m++) v[{349, m}] = fish[m];
    v[{350, 0}] = "Pescado cocinado";
    v[{350, 1}] = "Salmón cocinado";
    const char* dyes[] = {"Saco de tinta", "Tinte rojo", "Tinte verde", "Granos de cacao", "Lapislázuli", "Tinte morado",
                          "Tinte cian", "Tinte gris claro", "Tinte gris", "Tinte rosa", "Tinte lima", "Tinte amarillo",
                          "Tinte azul claro", "Tinte magenta", "Tinte naranja", "Polvo de hueso"};
    for (int m = 0; m < 16; m++) v[{351, m}] = dyes[m];
    const char* heads[] = {"Calavera de esqueleto", "Calavera de esqueleto wither", "Cabeza de zombi", "Cabeza", "Cabeza de creeper"};
    for (int m = 0; m < 5; m++) v[{397, m}] = heads[m];
    const std::pair<int, const char*> eggs[] = {
        {50, "Creeper"},   {51, "Esqueleto"},        {52, "Araña"},   {54, "Zombi"},         {55, "Slime"},
        {56, "Ghast"},     {57, "Cerdo zombi"},      {58, "Enderman"}, {59, "Araña de cueva"}, {60, "Lepisma"},
        {61, "Blaze"},     {62, "Cubo de magma"},    {65, "Murciélago"}, {66, "Bruja"},      {67, "Endermite"},
        {68, "Guardián"},  {90, "Cerdo"},            {91, "Oveja"},   {92, "Vaca"},          {93, "Gallina"},
        {94, "Calamar"},   {95, "Lobo"},             {96, "Champiñaca"}, {98, "Ocelote"},    {100, "Caballo"},
        {101, "Conejo"},   {120, "Aldeano"}};
    for (const auto& [m, name] : eggs) v[{383, m}] = std::string("Generar ") + name;
    return r;
  }();
  return n;
}

}  // namespace

std::string_view itemDisplayNameEs(int id, int meta) {
  const Names& n = names();
  if (auto it = n.byVariant.find({id, meta}); it != n.byVariant.end()) return it->second;
  if (auto it = n.byName.find(itemInfo(id).name); it != n.byName.end()) return it->second;
  return itemDisplayName(id, meta);
}

}  // namespace mcw
