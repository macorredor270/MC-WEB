#include "world/generator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/random.h"
#include "data/biomes.h"
#include "world/light.h"

namespace mcw {
namespace {

constexpr u32 kSaltOres = 101, kSaltTrees = 202, kSaltPlants = 303, kSaltBedrock = 404, kSaltSurface = 505;

double smoothstep(double e0, double e1, double x) {
  const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

const BlockState kAir = 0;
const BlockState kStone = makeState(B::stone);
const BlockState kDirt = makeState(B::dirt);
const BlockState kGrass = makeState(B::grass);
const BlockState kSand = makeState(B::sand);
const BlockState kSandstone = makeState(B::sandstone);
const BlockState kGravel = makeState(B::gravel);
const BlockState kClay = makeState(B::clay);
const BlockState kWater = makeState(B::water);
const BlockState kLava = makeState(B::lava);
const BlockState kIce = makeState(B::ice);
const BlockState kBedrock = makeState(B::bedrock);
const BlockState kSnowLayer = makeState(B::snow_layer);

bool isOcean(int b) { return b == Biome::ocean || b == Biome::deep_ocean || b == Biome::frozen_ocean; }
bool isCold(int b) { return biomeInfo(b).temperature < 0.15f; }
bool isSandy(int b) { return baseBiome(b) == Biome::desert || b == Biome::beach || b == Biome::cold_beach; }
bool isMesa(int b) { return baseBiome(b) == Biome::mesa; }

// Bloques de setas gigantes (ids de 1.8: 99 marrón, 100 roja)
constexpr int kBrownMushroomBlock = 99, kRedMushroomBlock = 100;

enum class TreeKind { Oak, Birch, Spruce, Jungle, JungleBig, DarkOak, MegaSpruce };

}  // namespace

GeneratorSettings GeneratorSettings::fromLevel(std::string_view name, std::string_view options, bool structures) {
  GeneratorSettings g;
  g.structures = structures;
  if (name == "flat") g.type = WorldType::Flat;
  else if (name == "largeBiomes") g.type = WorldType::LargeBiomes;
  else if (name == "amplified") g.type = WorldType::Amplified;
  else if (name == "nether") g.type = WorldType::Nether;
  else if (name == "end") g.type = WorldType::End;
  if (g.type != WorldType::Flat) return g;
  // "versión;capa,capa,...;bioma;estructuras". Capa: [N*]nombre[:meta] (también N x id de mundos viejos)
  std::string_view o = options.empty() ? std::string_view(kDefaultFlat) : options;
  std::vector<std::string_view> parts;
  for (std::size_t start = 0;;) {
    const std::size_t semi = o.find(';', start);
    parts.push_back(o.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start));
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  std::string_view layers = parts.size() >= 2 ? parts[1] : parts[0];
  for (std::size_t start = 0; start <= layers.size();) {
    const std::size_t comma = layers.find(',', start);
    std::string_view layer = layers.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
    start = comma == std::string_view::npos ? layers.size() + 1 : comma + 1;
    if (layer.empty()) continue;
    int count = 1;
    if (const std::size_t star = layer.find_first_of("*x"); star != std::string_view::npos && star > 0 &&
                                                             std::isdigit(static_cast<unsigned char>(layer[0]))) {
      count = std::max(1, std::atoi(std::string(layer.substr(0, star)).c_str()));
      layer = layer.substr(star + 1);
    }
    int meta = 0;
    if (layer.starts_with("minecraft:")) layer.remove_prefix(10);
    if (const std::size_t colon = layer.find(':'); colon != std::string_view::npos) {
      meta = std::atoi(std::string(layer.substr(colon + 1)).c_str());
      layer = layer.substr(0, colon);
    }
    int id = std::isdigit(static_cast<unsigned char>(layer.empty() ? 'x' : layer[0])) ? std::atoi(std::string(layer).c_str())
                                                                                      : blockIdByName(layer);
    if (id < 0) continue;
    g.flatLayers.emplace_back(makeState(id, meta), std::min(count, 256));
  }
  if (parts.size() >= 3 && !parts[2].empty()) g.flatBiome = std::atoi(std::string(parts[2]).c_str());
  if (g.flatLayers.empty()) return fromLevel("flat", kDefaultFlat, structures);
  return g;
}

GeneratorSettings GeneratorSettings::forDimension(int dimension, const GeneratorSettings& base) {
  if (dimension == 0) return base;
  GeneratorSettings g;
  g.type = dimension < 0 ? WorldType::Nether : WorldType::End;
  g.structures = base.structures;
  return g;
}

std::string GeneratorSettings::generatorName() const {
  switch (type) {
    case WorldType::Flat: return "flat";
    case WorldType::LargeBiomes: return "largeBiomes";
    case WorldType::Amplified: return "amplified";
    case WorldType::Nether: return "nether";
    case WorldType::End: return "end";
    default: return "default";
  }
}

std::string GeneratorSettings::flatOptions() const {
  if (type != WorldType::Flat) return "";
  std::string s = "3;";
  for (std::size_t i = 0; i < flatLayers.size(); i++) {
    if (i) s += ',';
    if (flatLayers[i].second > 1) s += std::to_string(flatLayers[i].second) + '*';
    s += "minecraft:" + std::string(blockInfo(stateId(flatLayers[i].first)).name);
    if (stateMeta(flatLayers[i].first)) s += ':' + std::to_string(stateMeta(flatLayers[i].first));
  }
  s += ';' + std::to_string(flatBiome) + (structures ? ";village" : ";");
  return s;
}

TerrainGenerator::TerrainGenerator(u64 seed, GeneratorSettings settings)
    : seed_(seed),
      settings_(std::move(settings)),
      terrain_(std::make_unique<legacy::Terrain>(seed, settings_.type == WorldType::LargeBiomes)),
      detail_(seed ^ 0x2222, 4),
      cave1_(seed ^ 0x8888, 2),
      cave2_(seed ^ 0x9999, 2),
      cavern_(seed ^ 0xAAAA, 2),
      surface_(seed ^ 0xBBBB, 2) {
  {  // Mesa: arcilla endurecida con franjas de colores (naranja, amarillo, marrón, rojo, blanco y gris claro)
    mesaBands_.fill(makeState(B::hardened_clay));
    Random br(seed ^ 0xBA0DBA0D);
    static constexpr int kColors[] = {1, 1, 1, 4, 12, 14, 0, 8};
    for (int i = 0; i < 70; i++) {
      const int y = br.nextInt(256), thick = 1 + br.nextInt(3), color = kColors[br.nextInt(8)];
      for (int k = 0; k < thick && y + k < 256; k++) mesaBands_[static_cast<std::size_t>(y + k)] = makeState(B::stained_hardened_clay, color);
    }
  }
  if (settings_.type == WorldType::Default || settings_.type == WorldType::LargeBiomes || settings_.type == WorldType::Amplified)
    strongholds_ = strongholdPositions(seed, settings_.type == WorldType::LargeBiomes);
  if (settings_.type == WorldType::Flat) {
    flatHeight_ = 0;
    for (const auto& [st, n] : settings_.flatLayers) flatHeight_ += n;
    flatHeight_ = std::min(flatHeight_, kChunkHeight - 1);
  }
}

std::unique_ptr<Chunk> TerrainGenerator::generateFlat(int cx, int cz) const {
  auto chunk = std::make_unique<Chunk>(cx, cz);
  int y = 0;
  for (const auto& [st, n] : settings_.flatLayers)
    for (int i = 0; i < n && y < kChunkHeight; i++, y++)
      for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) chunk->setBlock(x, y, z, st);
  for (int z = 0; z < 16; z++)
    for (int x = 0; x < 16; x++) chunk->setBiome(x, z, settings_.flatBiome);
  if (settings_.structures && settings_.flatVillages) {
    ColumnInfo cols[256];
    for (int i = 0; i < 256; i++) cols[i] = column(cx * 16 + (i & 15), cz * 16 + (i >> 4));
    placeVillages(*chunk, cols);
  }
  light::computeInitial(*chunk);
  return chunk;
}

std::array<int, 3> TerrainGenerator::findSpawn() const {
  if (settings_.type == WorldType::Nether) return {0, 64, 0};
  if (settings_.type == WorldType::End) return {100, 49, 0};
  if (settings_.type != WorldType::Flat) {
    // Como en 1.8: un bioma de bosque, llanura, taiga o jungla cerca del origen y, desde ahí, paseos al azar hasta dar con suelo de hierba
    const legacy::StructPos p = legacy::worldSpawn(seed_, terrain_->layers(), [&](int x, int z) { return column(x, z).height - 1; });
    const ColumnInfo c = column(p.x, p.z);
    if (c.height > kSeaLevel + 1 && !c.river) return {p.x, c.height, p.z};
  }
  for (int r = 0; r < 64; r++)
    for (int i = -r; i <= r; i++) {
      const std::pair<int, int> candidates[] = {{i * 16, -r * 16}, {i * 16, r * 16}, {-r * 16, i * 16}, {r * 16, i * 16}};
      for (auto [x, z] : candidates) {
        const ColumnInfo c = column(x, z);
        if (c.height > kSeaLevel + 1 && !c.river && c.biome != Biome::beach) return {x, c.height, z};
      }
    }
  return {0, 100, 0};
}

int TerrainGenerator::biomeAt(int x, int z) const {
  if (settings_.type == WorldType::Flat) return settings_.flatBiome;
  return terrain_->biomeAt(x, z);
}

ColumnInfo TerrainGenerator::column(int x, int z) const {
  if (settings_.type == WorldType::Flat) {
    ColumnInfo f;
    f.height = flatHeight_;
    f.biome = settings_.flatBiome;
    return f;
  }
  // Biomas y relieve: el algoritmo de 1.8 (ver world/legacy_gen.h)
  const auto ct = terrain_->chunk(x >> 4, z >> 4);
  const std::size_t k = static_cast<std::size_t>((x & 15) + (z & 15) * 16);
  double top = ct->top[k];
  // Amplificado: todo el relieve de tierra firme se multiplica (montañas de más de 200 bloques)
  if (settings_.type == WorldType::Amplified && top > kSeaLevel) top = kSeaLevel + (top - kSeaLevel) * 2.6;
  ColumnInfo out;
  out.height = std::clamp(static_cast<int>(std::floor(top)) + 1, 1, settings_.type == WorldType::Amplified ? 250 : 240);
  out.biome = ct->biome[k];
  out.river = out.biome == Biome::river || out.biome == Biome::frozen_river;
  return out;
}

void TerrainGenerator::fillColumn(Chunk& c, int lx, int lz, int wx, int wz, const ColumnInfo& col) const {
  const int h = col.height;
  Random rng(cellSeed(seed_, wx, wz, kSaltBedrock));
  const double sn = surface_.noise2(wx / 12.0, wz / 12.0);
  const int soil = 3 + static_cast<int>((sn * 0.5 + 0.5) * 2.0);
  const bool underwater = h < kSeaLevel;

  BlockState top = kGrass, filler = kDirt;
  int b = col.biome;
  const int base = baseBiome(b);
  if (isSandy(b)) { top = kSand; filler = kSand; }
  if (b == Biome::stone_beach) { top = kStone; filler = kStone; }
  if (base == Biome::extreme_hills && h > 96 + static_cast<int>(sn * 6)) { top = kStone; filler = kStone; }
  if (b == Biome::mushroom_island || b == Biome::mushroom_shore) top = makeState(B::mycelium);
  if (base == Biome::mega_taiga && sn > -0.1) top = makeState(B::dirt, 2);  // podzol en manchas
  if (b == Biome::ice_spikes) top = makeState(B::snow);
  const bool mesa = isMesa(b) && !underwater;
  if (underwater) {
    if (b == Biome::river) { top = sn > 0.35 ? kClay : (sn < -0.25 ? kGravel : kSand); filler = kDirt; }
    else if (isOcean(b)) { top = sn < -0.3 ? kGravel : (sn > 0.45 ? kClay : kSand); filler = top == kClay ? kDirt : top; }
    else { top = h >= kSeaLevel - 3 ? kSand : kDirt; filler = kDirt; }
  }

  for (int y = 0; y < h; y++) {
    BlockState s = kStone;
    if (y == 0) s = kBedrock;
    else if (y <= 4 && rng.nextInt(5) >= y) s = kBedrock;
    else if (mesa && y >= 50) s = y >= h - 3 ? makeState(B::sand, 1) : mesaBands_[static_cast<std::size_t>(y)];  // arena roja y franjas de arcilla
    else if (y == h - 1) s = top;
    else if (y >= h - 1 - soil) s = filler;
    else if (filler == kSand && y >= h - 1 - soil - 3) s = kSandstone;
    c.setBlock(lx, y, lz, s);
  }
  for (int y = h; y < kSeaLevel; y++) c.setBlock(lx, y, lz, (y == kSeaLevel - 1 && isCold(b)) ? kIce : kWater);
  c.setBiome(lx, lz, b);
}

void TerrainGenerator::carveCaves(Chunk& c, const ColumnInfo* cols) const {
  const int bx = c.pos().x * 16, bz = c.pos().z * 16;
  // El ruido de las cuevas cambia despacio (escala de 26-75 bloques): se evalúa en una rejilla de
  // 4x4x4 y se interpola en medio, como hace el generador de 1.8. Los puntos de la rejilla caen en
  // múltiplos de 4 del mundo, así que las cuevas siguen sin cortes de un chunk a otro.
  int maxY = 0;
  for (int i = 0; i < 256; i++) maxY = std::max(maxY, cols[i].height < kSeaLevel ? cols[i].height - 8 : cols[i].height - 5);
  if (maxY <= 5) return;
  constexpr int kStep = 4, kN = 16 / kStep + 1;
  const int ny = (maxY + kStep - 1) / kStep + 1;  // puntos en y: 0, 4, 8... hasta pasar maxY
  std::vector<float> ga(static_cast<std::size_t>(kN * kN * ny)), gb(ga.size()), gc(ga.size());
  auto gi = [&](int ix, int iy, int iz) { return (static_cast<std::size_t>(iy) * kN + iz) * kN + ix; };
  for (int iy = 0; iy < ny; iy++) {
    const double y = iy * kStep;
    for (int iz = 0; iz < kN; iz++)
      for (int ix = 0; ix < kN; ix++) {
        const double wx = bx + ix * kStep, wz = bz + iz * kStep;
        const std::size_t k = gi(ix, iy, iz);
        ga[k] = static_cast<float>(cave1_.noise3(wx / 42.0, y / 26.0, wz / 42.0));
        gb[k] = static_cast<float>(cave2_.noise3(wx / 42.0, y / 26.0, wz / 42.0));
        gc[k] = y < 52 ? static_cast<float>(cavern_.noise3(wx / 75.0, y / 38.0, wz / 75.0)) : -1.0f;
      }
  }
  auto sample = [&](const std::vector<float>& g, int lx, int y, int lz) {
    const int ix = lx / kStep, iy = y / kStep, iz = lz / kStep;
    const float fx = (lx % kStep) / static_cast<float>(kStep), fy = (y % kStep) / static_cast<float>(kStep),
                fz = (lz % kStep) / static_cast<float>(kStep);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00 = lerp(g[gi(ix, iy, iz)], g[gi(ix + 1, iy, iz)], fx);
    const float c10 = lerp(g[gi(ix, iy + 1, iz)], g[gi(ix + 1, iy + 1, iz)], fx);
    const float c01 = lerp(g[gi(ix, iy, iz + 1)], g[gi(ix + 1, iy, iz + 1)], fx);
    const float c11 = lerp(g[gi(ix, iy + 1, iz + 1)], g[gi(ix + 1, iy + 1, iz + 1)], fx);
    return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
  };

  for (int lz = 0; lz < 16; lz++) {
    for (int lx = 0; lx < 16; lx++) {
      const ColumnInfo& col = cols[lz * 16 + lx];
      // No tocar la superficie (los árboles se apoyan en ella) ni bajo el agua.
      const int colMax = col.height < kSeaLevel ? col.height - 8 : col.height - 5;
      for (int y = 5; y < colMax; y++) {
        const float a = sample(ga, lx, y, lz), b2 = sample(gb, lx, y, lz);
        bool air = a * a + b2 * b2 < 0.0032f;
        if (!air && y < 48) air = sample(gc, lx, y, lz) > 0.36f + std::max(0.0f, (y - 30) / 60.0f);
        if (!air) continue;
        const int id = stateId(c.block(lx, y, lz));
        if (id == B::bedrock || isFluid(id)) continue;
        c.setBlock(lx, y, lz, y <= 10 ? kLava : kAir);
      }
    }
  }
}

void TerrainGenerator::placeOres(Chunk& c) const {
  Random rng(cellSeed(seed_, c.pos().x, c.pos().z, kSaltOres));
  struct Ore { BlockState state; int veins; int size; int minY; int maxY; };
  const Ore ores[] = {
      {makeState(B::dirt), 10, 32, 0, 255},     {makeState(B::gravel), 8, 32, 0, 255},
      {makeState(B::stone, 1), 10, 32, 0, 80},  {makeState(B::stone, 3), 10, 32, 0, 80},
      {makeState(B::stone, 5), 10, 32, 0, 80},  {makeState(B::coal_ore), 20, 16, 0, 127},
      {makeState(B::iron_ore), 20, 8, 0, 63},   {makeState(B::gold_ore), 2, 8, 0, 31},
      {makeState(B::redstone_ore), 8, 7, 0, 15}, {makeState(B::diamond_ore), 1, 7, 0, 15},
      {makeState(B::lapis_ore), 1, 6, 0, 31},
  };
  for (const Ore& ore : ores) {
    for (int v = 0; v < ore.veins; v++) {
      int x = rng.nextInt(16), z = rng.nextInt(16);
      int y = ore.state == makeState(B::lapis_ore) ? rng.nextInt(16) + rng.nextInt(16) : rng.range(ore.minY, ore.maxY);
      // Paseo aleatorio en 3D: vetas con forma irregular. Se recortan en los bordes del chunk.
      for (int i = 0; i < ore.size; i++) {
        if (x >= 0 && x < 16 && z >= 0 && z < 16 && y > 0 && y < kChunkHeight && c.block(x, y, z) == kStone)
          c.setBlock(x, y, z, ore.state);
        switch (rng.nextInt(6)) {
          case 0: x++; break;
          case 1: x--; break;
          case 2: y++; break;
          case 3: y--; break;
          case 4: z++; break;
          default: z--; break;
        }
      }
    }
  }
  // Esmeraldas sueltas en las colinas extremas
  if (c.biome(8, 8) == Biome::extreme_hills) {
    for (int i = 3 + rng.nextInt(6); i > 0; i--) {
      const int x = rng.nextInt(16), z = rng.nextInt(16), y = 4 + rng.nextInt(28);
      if (c.block(x, y, z) == kStone) c.setBlock(x, y, z, makeState(B::emerald_ore));
    }
  }
}

void TerrainGenerator::placeTrees(Chunk& c) const {
  const int cx = c.pos().x, cz = c.pos().z;
  auto put = [&](int wx, int y, int wz, BlockState s, bool log) {
    const int lx = wx - cx * 16, lz = wz - cz * 16;
    if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || y <= 0 || y >= kChunkHeight) return;
    const int id = stateId(c.block(lx, y, lz));
    if (id == B::air || id == B::tallgrass || id == B::snow_layer || (log && (id == B::leaves || id == B::leaves2))) c.setBlock(lx, y, lz, s);
  };
  // Un disco de hojas centrado en (ox, oz) (en medios bloques si el árbol es de 2x2) con ese radio, sin las esquinas si `round`
  auto disc = [&](double ox, double oz, int y, double radius, BlockState leaf, Random* skip) {
    const int r = static_cast<int>(std::ceil(radius));
    for (int dz = -r - 1; dz <= r + 1; dz++)
      for (int dx = -r - 1; dx <= r + 1; dx++) {
        const double ddx = dx - (ox - std::floor(ox)), ddz = dz - (oz - std::floor(oz));
        if (ddx * ddx + ddz * ddz > radius * radius + 0.3) continue;
        if (skip && ddx * ddx + ddz * ddz > (radius - 1) * (radius - 1) && skip->nextInt(3) == 0) continue;
        put(static_cast<int>(std::floor(ox)) + dx, y, static_cast<int>(std::floor(oz)) + dz, leaf, false);
      }
  };
  // Lianas colgando de los lados de un tronco: `meta` dice a qué cara se pegan (1 sur, 2 oeste, 4 norte, 8 este)
  auto vines = [&](int wx, int wz, int y0, int y1, Random& rng) {
    static constexpr int kDx[4] = {0, -1, 0, 1}, kDz[4] = {1, 0, -1, 0}, kMeta[4] = {1, 2, 4, 8};
    for (int y = y0; y <= y1; y++)
      for (int d = 0; d < 4; d++)
        if (rng.nextInt(3) == 0) put(wx - kDx[d], y, wz - kDz[d], makeState(B::vine, kMeta[d]), false);
  };

  // Los árboles de los chunks vecinos pueden asomar en este: se recalculan con su semilla.
  for (int ncz = cz - 1; ncz <= cz + 1; ncz++) {
    for (int ncx = cx - 1; ncx <= cx + 1; ncx++) {
      Random rng(cellSeed(seed_, ncx, ncz, kSaltTrees));
      const ColumnInfo center = column(ncx * 16 + 8, ncz * 16 + 8);
      int count = 0;
      switch (center.biome) {
        case Biome::forest: case Biome::birch_forest: case Biome::forest_hills: case Biome::birch_forest_hills: count = 7 + rng.nextInt(4); break;
        case Biome::taiga: case Biome::cold_taiga: case Biome::taiga_hills: case Biome::cold_taiga_hills: count = 6 + rng.nextInt(4); break;
        case Biome::mega_taiga: case Biome::mega_taiga_hills: count = 5 + rng.nextInt(3); break;
        case Biome::roofed_forest: count = 14 + rng.nextInt(4); break;
        case Biome::jungle: case Biome::jungle_hills: count = 16 + rng.nextInt(6); break;
        case Biome::jungle_edge: count = 3 + rng.nextInt(3); break;
        case Biome::swamp: count = 2; break;
        case Biome::extreme_hills: count = rng.nextInt(3); break;
        case Biome::extreme_hills_plus: count = 4 + rng.nextInt(4); break;
        case Biome::savanna: case Biome::savanna_plateau: count = rng.nextInt(2); break;
        case Biome::plains: count = rng.chance(0.3) ? 1 : 0; break;
        case Biome::ice_plains: case Biome::ice_mountains: count = rng.chance(0.15) ? 1 : 0; break;
        case Biome::ice_spikes: count = 3 + rng.nextInt(4); break;  // (son pinchos de hielo)
        case Biome::mushroom_island: count = 2 + (rng.chance(0.3) ? 1 : 0); break;  // (son setas gigantes)
        default: count = 0;
      }
      for (int i = 0; i < count; i++) {
        const int wx = ncx * 16 + rng.nextInt(16), wz = ncz * 16 + rng.nextInt(16);
        const int variant = rng.nextInt(100);
        const int extra = rng.nextInt(4);
        const u32 shapeSeed = rng.nextU32();
        const ColumnInfo col = column(wx, wz);
        if (col.height <= kSeaLevel || col.river || col.height > 200) continue;
        const int b = col.biome;
        const int bb = baseBiome(b);
        if (isSandy(b) || isOcean(b) || b == Biome::stone_beach || isMesa(b) || b == Biome::mushroom_shore) continue;
        if (bb == Biome::extreme_hills && col.height > 96) continue;
        const int base = col.height;  // primer bloque de tronco
        Random shape(shapeSeed);

        if (b == Biome::ice_spikes) {  // pinchos de hielo: conos de hielo compacto
          const bool big = variant < 12;
          const int height = big ? 24 + extra * 6 : 8 + shape.nextInt(14) + extra * 2;
          const double radius = big ? 3.4 : 1.6 + shape.nextInt(2);
          const BlockState ice = makeState(B::packed_ice);
          for (int y = 0; y < height; y++) {
            const double r = radius * (1.0 - static_cast<double>(y) / height);
            const int ri = static_cast<int>(std::ceil(r));
            for (int dz = -ri; dz <= ri; dz++)
              for (int dx = -ri; dx <= ri; dx++)
                if (dx * dx + dz * dz <= r * r + 0.2) put(wx + dx, base + y, wz + dz, ice, true);
          }
          continue;
        }
        if (b == Biome::mushroom_island || (bb == Biome::roofed_forest && variant < 12)) {  // setas gigantes
          const bool red = variant % 2 == 0;
          const int stem = 4 + extra;
          const BlockState stemS = makeState(red ? kRedMushroomBlock : kBrownMushroomBlock, 10);
          const BlockState capS = makeState(red ? kRedMushroomBlock : kBrownMushroomBlock, 14);
          for (int y = 0; y < stem; y++) put(wx, base + y, wz, stemS, false);
          if (red) {  // cúpula: dos anillos y una tapa
            for (int y = stem - 3; y < stem; y++)
              for (int dz = -2; dz <= 2; dz++)
                for (int dx = -2; dx <= 2; dx++)
                  if (std::max(std::abs(dx), std::abs(dz)) == 2 && !(std::abs(dx) == 2 && std::abs(dz) == 2)) put(wx + dx, base + y, wz + dz, capS, false);
            for (int dz = -1; dz <= 1; dz++)
              for (int dx = -1; dx <= 1; dx++) put(wx + dx, base + stem, wz + dz, capS, false);
          } else {  // sombrero plano de 7x7 sin esquinas
            for (int dz = -3; dz <= 3; dz++)
              for (int dx = -3; dx <= 3; dx++)
                if (!(std::abs(dx) >= 2 && std::abs(dz) >= 2 && std::abs(dx) + std::abs(dz) >= 5)) put(wx + dx, base + stem, wz + dz, capS, false);
          }
          continue;
        }

        TreeKind kind = TreeKind::Oak;
        if (bb == Biome::birch_forest || (bb == Biome::forest && variant < 20)) kind = TreeKind::Birch;
        if (bb == Biome::taiga || bb == Biome::cold_taiga || bb == Biome::ice_plains || (bb == Biome::extreme_hills && variant < 50) ||
            (b == Biome::extreme_hills_plus && variant < 50))
          kind = TreeKind::Spruce;
        if (bb == Biome::roofed_forest) kind = variant < 80 ? TreeKind::DarkOak : TreeKind::Oak;
        if (bb == Biome::jungle) kind = variant < 12 ? TreeKind::JungleBig : TreeKind::Jungle;
        if (b == Biome::jungle_edge) kind = variant < 50 ? TreeKind::Jungle : TreeKind::Oak;
        if (bb == Biome::mega_taiga) kind = variant < 60 ? TreeKind::MegaSpruce : TreeKind::Spruce;

        // Los de 2x2 piden el suelo casi llano
        const bool wide = kind == TreeKind::JungleBig || kind == TreeKind::DarkOak || kind == TreeKind::MegaSpruce;
        if (wide) {
          bool flat = true;
          for (int dz = 0; dz <= 1 && flat; dz++)
            for (int dx = 0; dx <= 1 && flat; dx++) {
              const ColumnInfo o = column(wx + dx, wz + dz);
              flat = std::abs(o.height - base) <= 1 && !o.river && o.height > kSeaLevel;
            }
          if (!flat) continue;
        }

        const int woodType = kind == TreeKind::Birch ? 2 : ((kind == TreeKind::Spruce || kind == TreeKind::MegaSpruce) ? 1 : (kind == TreeKind::Jungle || kind == TreeKind::JungleBig) ? 3 : 0);
        const bool dark = kind == TreeKind::DarkOak;
        const BlockState logS = dark ? makeState(B::log2, 1) : makeState(B::log, woodType);
        const BlockState leafS = dark ? makeState(B::leaves2, 1) : makeState(B::leaves, woodType);

        if (kind == TreeKind::Spruce) {
          const int trunk = 6 + extra + (variant % 3);
          const int top = base + trunk - 1;
          put(wx, top + 1, wz, leafS, false);
          int radius = 0;
          for (int y = top; y >= base + 2; y--) {
            const int layer = top - y;
            radius = layer == 0 ? 1 : (layer % 2 == 1 ? std::min(1 + layer / 3, 3) : std::max(1, std::min(layer / 3, 2)));
            for (int dz = -radius; dz <= radius; dz++)
              for (int dx = -radius; dx <= radius; dx++) {
                if (std::abs(dx) == radius && std::abs(dz) == radius && radius > 0) continue;
                put(wx + dx, y, wz + dz, leafS, false);
              }
          }
          for (int y = base; y <= top; y++) put(wx, y, wz, logS, true);
        } else if (kind == TreeKind::MegaSpruce) {
          const int trunk = 13 + extra * 2 + shape.nextInt(5);
          const int top = base + trunk - 1;
          for (int y = top - 1; y >= base + 4; y--) {
            const int layer = top - y;
            const double radius = std::min(1.0 + layer * 0.45, 4.2) - ((layer % 3 == 2) ? 1.0 : 0.0);
            disc(wx + 0.5, wz + 0.5, y, std::max(1.2, radius), leafS, nullptr);
          }
          disc(wx + 0.5, wz + 0.5, top, 1.4, leafS, nullptr);
          disc(wx + 0.5, wz + 0.5, top + 1, 0.9, leafS, nullptr);
          for (int y = base; y <= top; y++)
            for (int dz = 0; dz <= 1; dz++)
              for (int dx = 0; dx <= 1; dx++) put(wx + dx, y, wz + dz, logS, true);
        } else if (kind == TreeKind::DarkOak) {
          const int trunk = 6 + shape.nextInt(3);
          const int top = base + trunk - 1;
          disc(wx + 0.5, wz + 0.5, top + 1, 2.2, leafS, &shape);
          disc(wx + 0.5, wz + 0.5, top, 3.2, leafS, &shape);
          disc(wx + 0.5, wz + 0.5, top - 1, 3.2, leafS, &shape);
          disc(wx + 0.5, wz + 0.5, top - 2, 2.2, leafS, &shape);
          for (int y = base; y <= top; y++)
            for (int dz = 0; dz <= 1; dz++)
              for (int dx = 0; dx <= 1; dx++) put(wx + dx, y, wz + dz, logS, true);
        } else if (kind == TreeKind::JungleBig) {
          const int trunk = 12 + extra * 3 + shape.nextInt(6);
          const int top = base + trunk - 1;
          disc(wx + 0.5, wz + 0.5, top + 1, 2.2, leafS, &shape);
          disc(wx + 0.5, wz + 0.5, top, 4.2, leafS, &shape);
          disc(wx + 0.5, wz + 0.5, top - 1, 4.2, leafS, &shape);
          disc(wx + 0.5, wz + 0.5, top - 2, 3.2, leafS, &shape);
          for (int y = base; y <= top; y++)
            for (int dz = 0; dz <= 1; dz++)
              for (int dx = 0; dx <= 1; dx++) put(wx + dx, y, wz + dz, logS, true);
          for (int dz = 0; dz <= 1; dz++)
            for (int dx = 0; dx <= 1; dx++) vines(wx + dx, wz + dz, base + 2, top - 3, shape);
        } else {  // roble, abedul y jungla pequeña
          const bool jungle = kind == TreeKind::Jungle;
          const int trunk = jungle ? 4 + shape.nextInt(7) : (kind == TreeKind::Birch ? 5 : 4) + std::min(extra, 2);
          const int top = base + trunk - 1;
          for (int y = top - 2; y <= top + 1; y++) {
            const int radius = y >= top ? 1 : 2;
            for (int dz = -radius; dz <= radius; dz++)
              for (int dx = -radius; dx <= radius; dx++) {
                const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
                if (corner && (y == top + 1 || shape.nextInt(2) == 0)) continue;
                put(wx + dx, y, wz + dz, leafS, false);
              }
          }
          for (int y = base; y <= top; y++) put(wx, y, wz, logS, true);
          if (jungle) vines(wx, wz, base + 1, top - 1, shape);
        }
        // Bajo el tronco, la hierba se convierte en tierra (como pasa en el juego)
        for (int dz = 0; dz <= (wide ? 1 : 0); dz++)
          for (int dx = 0; dx <= (wide ? 1 : 0); dx++) {
            const int lx = wx + dx - cx * 16, lz = wz + dz - cz * 16;
            if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && c.block(lx, base - 1, lz) == kGrass) c.setBlock(lx, base - 1, lz, kDirt);
          }
      }
    }
  }
}

void TerrainGenerator::placePlants(Chunk& c, const ColumnInfo* cols) const {
  Random rng(cellSeed(seed_, c.pos().x, c.pos().z, kSaltPlants));
  static const BlockState kFlowers[] = {makeState(B::yellow_flower), makeState(B::red_flower, 0), makeState(B::red_flower, 3),
                                        makeState(B::red_flower, 4), makeState(B::red_flower, 5), makeState(B::red_flower, 6),
                                        makeState(B::red_flower, 7), makeState(B::red_flower, 8)};
  for (int lz = 0; lz < 16; lz++) {
    for (int lx = 0; lx < 16; lx++) {
      const ColumnInfo& col = cols[lz * 16 + lx];
      const int y = col.height;
      if (y >= kChunkHeight - 2) continue;
      const BlockState ground = c.block(lx, y - 1, lz);
      const BlockState above = c.block(lx, y, lz);
      const double roll = rng.next();
      const int b = baseBiome(col.biome);  // (las colinas llevan lo mismo que su bioma de base)

      if (above == kWater) {
        // Nenúfares en pantanos
        if (b == Biome::swamp && y >= kSeaLevel - 3 && c.block(lx, kSeaLevel, lz) == kAir && roll < 0.04)
          c.setBlock(lx, kSeaLevel, lz, makeState(B::waterlily));
        continue;
      }
      if (above != kAir) continue;

      // Caña de azúcar junto al agua
      if ((ground == kGrass || ground == kSand || ground == kDirt) && y == kSeaLevel) {
        bool nearWater = false;
        constexpr std::pair<int, int> kDirs[] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto [dx, dz] : kDirs) {
          const int nx = lx + dx, nz = lz + dz;
          if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16 && c.block(nx, y - 1, nz) == kWater) nearWater = true;
        }
        if (nearWater && roll < 0.18) {
          const int hgt = 1 + rng.nextInt(3);
          for (int i = 0; i < hgt; i++) c.setBlock(lx, y + i, lz, makeState(B::reeds));
          continue;
        }
      }

      const BlockState kPodzol = makeState(B::dirt, 2);
      if (ground == kGrass || ground == kPodzol) {
        double grassP = 0.05, flowerP = 0.004, fernP = 0.0, doubleP = 0.0;
        switch (b) {
          case Biome::jungle: grassP = 0.22; fernP = 0.14; doubleP = 0.03; break;
          case Biome::jungle_edge: grassP = 0.2; fernP = 0.06; break;
          case Biome::roofed_forest: grassP = 0.05; flowerP = 0.004; break;
          case Biome::mega_taiga: grassP = 0.04; fernP = 0.16; doubleP = 0.02; break;
          case Biome::extreme_hills_plus: grassP = 0.04; break;
          case Biome::plains: grassP = 0.28; flowerP = 0.02; doubleP = 0.01; break;
          case Biome::forest: grassP = 0.07; flowerP = 0.01; break;
          case Biome::birch_forest: grassP = 0.07; flowerP = 0.008; break;
          case Biome::taiga: case Biome::cold_taiga: grassP = 0.03; fernP = 0.08; break;
          case Biome::savanna: grassP = 0.35; doubleP = 0.04; break;
          case Biome::swamp: grassP = 0.05; flowerP = 0.01; break;
          case Biome::ice_plains: grassP = 0.0; flowerP = 0.0; break;
          default: break;
        }
        if (roll < doubleP) {
          const int kind = (b == Biome::jungle || b == Biome::mega_taiga) ? 3 : 2;  // helecho grande o hierba alta
          c.setBlock(lx, y, lz, makeState(B::double_plant, kind));
          c.setBlock(lx, y + 1, lz, makeState(B::double_plant, 8));
        } else if (roll < doubleP + flowerP) {
          const BlockState f = b == Biome::swamp ? makeState(B::red_flower, 1) : kFlowers[rng.nextInt(b == Biome::plains ? 8 : 2)];
          c.setBlock(lx, y, lz, f);
        } else if (roll < doubleP + flowerP + fernP) {
          c.setBlock(lx, y, lz, makeState(B::tallgrass, 2));
        } else if (roll < doubleP + flowerP + fernP + grassP) {
          c.setBlock(lx, y, lz, makeState(B::tallgrass, 1));
        } else if (b == Biome::jungle && roll > 0.998) {
          c.setBlock(lx, y, lz, makeState(B::melon_block));
        } else if (b == Biome::roofed_forest && roll > 0.99) {
          c.setBlock(lx, y, lz, makeState(rng.nextInt(2) ? B::red_mushroom : B::brown_mushroom));
        }
      } else if (ground == makeState(B::mycelium)) {
        if (roll < 0.012) c.setBlock(lx, y, lz, makeState(rng.nextInt(2) ? B::red_mushroom : B::brown_mushroom));
      } else if (ground == makeState(B::sand, 1) && b == Biome::mesa) {
        if (roll < 0.02) c.setBlock(lx, y, lz, makeState(B::deadbush));
        else if (roll < 0.025 && lx > 0 && lx < 15 && lz > 0 && lz < 15) {
          const int hgt = 1 + rng.nextInt(3);
          for (int i = 0; i < hgt; i++) c.setBlock(lx, y + i, lz, makeState(B::cactus));
        }
      } else if (ground == kSand && b == Biome::desert) {
        if (roll < 0.004) {
          c.setBlock(lx, y, lz, makeState(B::deadbush));
        } else if (roll < 0.008 && lx > 0 && lx < 15 && lz > 0 && lz < 15) {
          const int hgt = 1 + rng.nextInt(3);
          for (int i = 0; i < hgt; i++) c.setBlock(lx, y + i, lz, makeState(B::cactus));
        }
      }
    }
  }
}

void TerrainGenerator::placeSnow(Chunk& c, const ColumnInfo* cols) const {
  for (int lz = 0; lz < 16; lz++) {
    for (int lx = 0; lx < 16; lx++) {
      const ColumnInfo& col = cols[lz * 16 + lx];
      const bool snowy = isCold(col.biome) || (col.biome == Biome::extreme_hills && col.height > 100);
      if (!snowy) continue;
      for (int y = std::min(col.height + 12, kChunkHeight - 2); y > 0; y--) {
        const BlockState s = c.block(lx, y, lz);
        if (s == kAir) continue;
        const int id = stateId(s);
        if (blockInfo(id).opaqueCube || id == B::leaves) c.setBlock(lx, y + 1, lz, kSnowLayer);
        break;
      }
    }
  }
}

std::unique_ptr<Chunk> TerrainGenerator::generate(int cx, int cz) const {
  if (settings_.type == WorldType::Flat) return generateFlat(cx, cz);
  if (settings_.type == WorldType::Nether) return generateNether(cx, cz);
  if (settings_.type == WorldType::End) return generateEnd(cx, cz);
  auto chunk = std::make_unique<Chunk>(cx, cz);
  ColumnInfo cols[256];
  for (int lz = 0; lz < 16; lz++)
    for (int lx = 0; lx < 16; lx++) {
      cols[lz * 16 + lx] = column(cx * 16 + lx, cz * 16 + lz);
      fillColumn(*chunk, lx, lz, cx * 16 + lx, cz * 16 + lz, cols[lz * 16 + lx]);
    }
  (void)kSaltSurface;
  placeOres(*chunk);
  carveCaves(*chunk, cols);
  placeTrees(*chunk);
  placePlants(*chunk, cols);
  placeSnow(*chunk, cols);
  if (settings_.structures) {
    placeVillages(*chunk, cols);
    placeTemples(*chunk, cols);
    placeMineshafts(*chunk, cols);
    placeDungeons(*chunk, cols);
    placeStrongholds(*chunk);
  }
  light::computeInitial(*chunk);
  return chunk;
}

}  // namespace mcw
