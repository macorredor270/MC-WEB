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
bool isCold(int b) { return b == Biome::ice_plains || b == Biome::cold_taiga || b == Biome::frozen_ocean || b == Biome::cold_beach; }
bool isSandy(int b) { return b == Biome::desert || b == Biome::beach || b == Biome::cold_beach; }

enum class TreeKind { Oak, Birch, Spruce };

}  // namespace

GeneratorSettings GeneratorSettings::fromLevel(std::string_view name, std::string_view options, bool structures) {
  GeneratorSettings g;
  g.structures = structures;
  if (name == "flat") g.type = WorldType::Flat;
  else if (name == "largeBiomes") g.type = WorldType::LargeBiomes;
  else if (name == "amplified") g.type = WorldType::Amplified;
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

std::string GeneratorSettings::generatorName() const {
  switch (type) {
    case WorldType::Flat: return "flat";
    case WorldType::LargeBiomes: return "largeBiomes";
    case WorldType::Amplified: return "amplified";
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
      continental_(seed ^ 0x1111, 5),
      detail_(seed ^ 0x2222, 4),
      rugged_(seed ^ 0x3333, 4, 0.55),
      hills_(seed ^ 0x4444, 3),
      temperature_(seed ^ 0x5555, 3),
      humidity_(seed ^ 0x6666, 3),
      river_(seed ^ 0x7777, 4),
      cave1_(seed ^ 0x8888, 2),
      cave2_(seed ^ 0x9999, 2),
      cavern_(seed ^ 0xAAAA, 2),
      surface_(seed ^ 0xBBBB, 2) {
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
  light::computeInitial(*chunk);
  return chunk;
}

std::array<int, 3> TerrainGenerator::findSpawn() const {
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

ColumnInfo TerrainGenerator::column(int x, int z) const {
  if (settings_.type == WorldType::Flat) {
    ColumnInfo f;
    f.height = flatHeight_;
    f.biome = settings_.flatBiome;
    return f;
  }
  // Biomas grandes: todo lo que decide biomas y continentes se estira x4
  const int bx = settings_.type == WorldType::LargeBiomes ? x / 4 : x, bz = settings_.type == WorldType::LargeBiomes ? z / 4 : z;
  const double c = continental_.noise2(bx / 900.0, bz / 900.0) * 1.7;
  const double d = detail_.noise2(x / 120.0, z / 120.0);
  const double m = hills_.noise2(bx / 380.0, bz / 380.0) * 1.6;
  const double t = temperature_.noise2(bx / 1100.0, bz / 1100.0) * 1.8;
  const double h = humidity_.noise2(bx / 800.0, bz / 800.0) * 1.8;
  const double r = std::abs(river_.noise2(bx / 520.0, bz / 520.0));

  // Tierra firme: llanuras onduladas que suben hacia el interior del continente.
  double land = 65.0 + d * 7.0 + smoothstep(0.0, 0.7, c) * 9.0;
  const double mountain = smoothstep(0.25, 0.6, m);
  if (mountain > 0) {
    const double rg = rugged_.noise2(x / 55.0, z / 55.0);
    land += mountain * (22.0 + 38.0 * (rg * 0.5 + 0.5));
  }
  // Amplificado: todo el relieve de tierra firme se multiplica (montañas de más de 200 bloques)
  if (settings_.type == WorldType::Amplified && land > 64.0) land = 64.0 + (land - 64.0) * 2.6;
  // Océano: el fondo baja cuanto más negativo es el valor continental.
  const double ocean = smoothstep(-0.05, -0.3, c);
  const double oceanFloor = 57.0 - std::clamp(-(c + 0.3), 0.0, 0.7) * 30.0 + d * 3.0;
  double height = land + (oceanFloor - land) * ocean;

  // Ríos: un canal donde el ruido del río pasa cerca de cero.
  bool river = false;
  if (ocean < 0.5 && r < 0.045) {
    const double k = smoothstep(0.045, 0.012, r) * (1.0 - mountain * 0.7);
    height = height + (57.5 - height) * k;
    river = r < 0.028 && height < kSeaLevel;
  }

  ColumnInfo out;
  out.height = std::clamp(static_cast<int>(std::floor(height)), 8, settings_.type == WorldType::Amplified ? 250 : 240);
  out.mountain = static_cast<float>(mountain);
  out.river = river;

  int biome;
  if (out.height < 58 && !river) {
    biome = t < -0.55 ? Biome::frozen_ocean : (out.height < 46 ? Biome::deep_ocean : Biome::ocean);
  } else if (river) {
    biome = Biome::river;
  } else if (out.height <= 64 && ocean > 0.02) {
    biome = t < -0.5 ? Biome::cold_beach : (mountain > 0.3 ? Biome::stone_beach : Biome::beach);
  } else if (mountain > 0.5) {
    biome = Biome::extreme_hills;
  } else if (t > 0.5) {
    biome = h < 0.05 ? Biome::desert : (h < 0.35 ? Biome::savanna : Biome::forest);
  } else if (t < -0.5) {
    biome = h > 0.1 ? Biome::cold_taiga : Biome::ice_plains;
  } else if (t < -0.18) {
    biome = Biome::taiga;
  } else if (h > 0.45 && out.height < 68) {
    biome = Biome::swamp;
  } else if (h > 0.12) {
    biome = t < 0.05 ? Biome::birch_forest : Biome::forest;
  } else {
    biome = Biome::plains;
  }
  out.biome = biome;
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
  if (isSandy(b)) { top = kSand; filler = kSand; }
  if (b == Biome::stone_beach) { top = kStone; filler = kStone; }
  if (b == Biome::extreme_hills && h > 96 + static_cast<int>(sn * 6)) { top = kStone; filler = kStone; }
  if (underwater) {
    if (b == Biome::river) { top = sn > 0.35 ? kClay : (sn < -0.25 ? kGravel : kSand); filler = kDirt; }
    else if (isOcean(b)) { top = sn < -0.3 ? kGravel : (sn > 0.45 ? kClay : kSand); filler = top == kClay ? kDirt : top; }
    else { top = h >= kSeaLevel - 3 ? kSand : kDirt; filler = kDirt; }
  }

  for (int y = 0; y < h; y++) {
    BlockState s = kStone;
    if (y == 0) s = kBedrock;
    else if (y <= 4 && rng.nextInt(5) >= y) s = kBedrock;
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
    if (id == B::air || id == B::tallgrass || id == B::snow_layer || (log && (id == B::leaves))) c.setBlock(lx, y, lz, s);
  };

  // Los árboles de los chunks vecinos pueden asomar en este: se recalculan con su semilla.
  for (int ncz = cz - 1; ncz <= cz + 1; ncz++) {
    for (int ncx = cx - 1; ncx <= cx + 1; ncx++) {
      Random rng(cellSeed(seed_, ncx, ncz, kSaltTrees));
      const ColumnInfo center = column(ncx * 16 + 8, ncz * 16 + 8);
      int count = 0;
      switch (center.biome) {
        case Biome::forest: case Biome::birch_forest: count = 7 + rng.nextInt(4); break;
        case Biome::taiga: case Biome::cold_taiga: count = 6 + rng.nextInt(4); break;
        case Biome::swamp: count = 2; break;
        case Biome::extreme_hills: count = rng.nextInt(3); break;
        case Biome::savanna: count = rng.nextInt(2); break;
        case Biome::plains: count = rng.chance(0.3) ? 1 : 0; break;
        case Biome::ice_plains: count = rng.chance(0.15) ? 1 : 0; break;
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
        if (isSandy(b) || isOcean(b) || b == Biome::stone_beach) continue;
        if (b == Biome::extreme_hills && col.height > 96) continue;

        TreeKind kind = TreeKind::Oak;
        if (b == Biome::birch_forest || (b == Biome::forest && variant < 20)) kind = TreeKind::Birch;
        if (b == Biome::taiga || b == Biome::cold_taiga || b == Biome::ice_plains || (b == Biome::extreme_hills && variant < 50))
          kind = TreeKind::Spruce;

        const int woodType = kind == TreeKind::Birch ? 2 : (kind == TreeKind::Spruce ? 1 : 0);
        const BlockState logS = makeState(B::log, woodType), leafS = makeState(B::leaves, woodType);
        const int base = col.height;  // primer bloque de tronco
        Random shape(shapeSeed);

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
        } else {
          const int trunk = (kind == TreeKind::Birch ? 5 : 4) + std::min(extra, 2);
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
        }
        // Bajo el tronco, la hierba se convierte en tierra (como pasa en el juego)
        const int lx = wx - cx * 16, lz = wz - cz * 16;
        if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && c.block(lx, base - 1, lz) == kGrass) c.setBlock(lx, base - 1, lz, kDirt);
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
      const int b = col.biome;

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

      if (ground == kGrass) {
        double grassP = 0.05, flowerP = 0.004, fernP = 0.0, doubleP = 0.0;
        switch (b) {
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
          c.setBlock(lx, y, lz, makeState(B::double_plant, 2));
          c.setBlock(lx, y + 1, lz, makeState(B::double_plant, 8));
        } else if (roll < doubleP + flowerP) {
          const BlockState f = b == Biome::swamp ? makeState(B::red_flower, 1) : kFlowers[rng.nextInt(b == Biome::plains ? 8 : 2)];
          c.setBlock(lx, y, lz, f);
        } else if (roll < doubleP + flowerP + fernP) {
          c.setBlock(lx, y, lz, makeState(B::tallgrass, 2));
        } else if (roll < doubleP + flowerP + fernP + grassP) {
          c.setBlock(lx, y, lz, makeState(B::tallgrass, 1));
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
  light::computeInitial(*chunk);
  return chunk;
}

}  // namespace mcw
