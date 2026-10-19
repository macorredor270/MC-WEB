// Basado en Cubiomes (MIT, © 2020 Cubitect), ver docs/THIRD_PARTY.md.
#include "world/legacy_gen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mcw::legacy {
namespace {

// Ids de bioma de 1.8 (los mismos números que en el juego)
enum : int {
  ocean = 0, plains = 1, desert = 2, mountains = 3, forest = 4, taiga = 5, swamp = 6, river = 7,
  frozen_ocean = 10, frozen_river = 11, snowy_tundra = 12, snowy_mountains = 13, mushroom_fields = 14, mushroom_field_shore = 15,
  beach = 16, desert_hills = 17, wooded_hills = 18, taiga_hills = 19, mountain_edge = 20, jungle = 21, jungle_hills = 22,
  jungle_edge = 23, deep_ocean = 24, stone_shore = 25, snowy_beach = 26, birch_forest = 27, birch_forest_hills = 28,
  dark_forest = 29, snowy_taiga = 30, snowy_taiga_hills = 31, giant_tree_taiga = 32, giant_tree_taiga_hills = 33,
  wooded_mountains = 34, savanna = 35, savanna_plateau = 36, badlands = 37, wooded_badlands_plateau = 38, badlands_plateau = 39,
  sunflower_plains = 129, desert_lakes = 130, gravelly_mountains = 131, flower_forest = 132, taiga_mountains = 133,
  swamp_hills = 134, ice_spikes = 140, modified_jungle = 149, modified_jungle_edge = 151, tall_birch_forest = 155,
  tall_birch_hills = 156, dark_forest_hills = 157, snowy_taiga_mountains = 158, giant_spruce_taiga = 160,
  giant_spruce_taiga_hills = 161, modified_gravelly_mountains = 162, shattered_savanna = 163, shattered_savanna_plateau = 164,
  eroded_badlands = 165, modified_wooded_badlands_plateau = 166, modified_badlands_plateau = 167,
};

// Categorías de temperatura que usan las capas de clima
enum : int { Oceanic = 0, Warm, Lush, Cold, Freezing, Special };

// --- Números aleatorios de las capas -----------------------------------------------------------------

inline u64 stepSeed(u64 s, u64 salt) { return s * (s * 6364136223846793005ULL + 1442695040888963407ULL) + salt; }
inline u64 ext(int v) { return static_cast<u64>(static_cast<i64>(v)); }
inline int firstInt(u64 s, int mod) {
  int r = static_cast<int>((static_cast<i64>(s) >> 24) % mod);
  if (r < 0) r += mod;
  return r;
}
inline bool firstIsZero(u64 s, int mod) { return static_cast<int>((static_cast<i64>(s) >> 24) % mod) == 0; }
inline u64 chunkSeed(u64 ss, int x, int z) {
  u64 cs = ss + ext(x);
  cs = stepSeed(cs, ext(z));
  cs = stepSeed(cs, ext(x));
  cs = stepSeed(cs, ext(z));
  return cs;
}
inline u64 layerSalt(u64 salt) {
  u64 ls = stepSeed(salt, salt);
  ls = stepSeed(ls, salt);
  return stepSeed(ls, salt);
}

// --- Categorías de bioma ---------------------------------------------------------------------------------

inline bool isShallowOcean(int id) { return id == ocean || id == frozen_ocean; }
inline bool isDeepOcean(int id) { return id == deep_ocean; }
inline bool isOceanic(int id) { return id == ocean || id == frozen_ocean || id == deep_ocean; }
inline bool isMesa(int id) {
  return id == badlands || id == eroded_badlands || id == modified_wooded_badlands_plateau || id == modified_badlands_plateau ||
         id == wooded_badlands_plateau || id == badlands_plateau;
}
inline bool isSnowy(int id) {
  return id == frozen_ocean || id == frozen_river || id == snowy_tundra || id == snowy_mountains || id == snowy_beach ||
         id == snowy_taiga || id == snowy_taiga_hills || id == ice_spikes || id == snowy_taiga_mountains;
}

int getMutated(int id) {
  switch (id) {
    case plains: return sunflower_plains;
    case desert: return desert_lakes;
    case mountains: return gravelly_mountains;
    case forest: return flower_forest;
    case taiga: return taiga_mountains;
    case swamp: return swamp_hills;
    case snowy_tundra: return ice_spikes;
    case jungle: return modified_jungle;
    case jungle_edge: return modified_jungle_edge;
    case birch_forest: return tall_birch_forest;
    case birch_forest_hills: return tall_birch_hills;
    case dark_forest: return dark_forest_hills;
    case snowy_taiga: return snowy_taiga_mountains;
    case giant_tree_taiga: return giant_spruce_taiga;
    case giant_tree_taiga_hills: return giant_spruce_taiga_hills;
    case wooded_mountains: return modified_gravelly_mountains;
    case savanna: return shattered_savanna;
    case savanna_plateau: return shattered_savanna_plateau;
    case badlands: return eroded_badlands;
    case wooded_badlands_plateau: return modified_wooded_badlands_plateau;
    case badlands_plateau: return modified_badlands_plateau;
    default: return -1;
  }
}

int getCategory(int id) {
  switch (id) {
    case beach: case snowy_beach: return beach;
    case desert: case desert_hills: case desert_lakes: return desert;
    case mountains: case mountain_edge: case wooded_mountains: case gravelly_mountains: case modified_gravelly_mountains: return mountains;
    case forest: case wooded_hills: case birch_forest: case birch_forest_hills: case dark_forest: case flower_forest:
    case tall_birch_forest: case tall_birch_hills: case dark_forest_hills: return forest;
    case snowy_tundra: case snowy_mountains: case ice_spikes: return snowy_tundra;
    case jungle: case jungle_hills: case jungle_edge: case modified_jungle: case modified_jungle_edge: return jungle;
    case badlands: case eroded_badlands: case modified_wooded_badlands_plateau: case modified_badlands_plateau:
    case wooded_badlands_plateau: case badlands_plateau: return badlands;  // (la categoría "mesa")
    case mushroom_fields: case mushroom_field_shore: return mushroom_fields;
    case stone_shore: return stone_shore;
    case ocean: case frozen_ocean: case deep_ocean: return ocean;
    case plains: case sunflower_plains: return plains;
    case river: case frozen_river: return river;
    case savanna: case savanna_plateau: case shattered_savanna: case shattered_savanna_plateau: return savanna;
    case swamp: case swamp_hills: return swamp;
    case taiga: case taiga_hills: case snowy_taiga: case snowy_taiga_hills: case giant_tree_taiga: case giant_tree_taiga_hills:
    case taiga_mountains: case snowy_taiga_mountains: case giant_spruce_taiga: case giant_spruce_taiga_hills: return taiga;
    default: return -1;
  }
}

bool areSimilar(int a, int b) {
  if (a == b) return true;
  if (a == wooded_badlands_plateau || a == badlands_plateau) return b == wooded_badlands_plateau || b == badlands_plateau;
  return getCategory(a) == getCategory(b);
}

inline bool isAny4(int id, int a, int b, int c, int d) { return id == a || id == b || id == c || id == d; }

// --- Números aleatorios de Java ----------------------------------------------------------------------

constexpr u64 kMask48 = (1ULL << 48) - 1;

struct JavaRandom {
  u64 s = 0;
  void setSeed(u64 v) { s = (v ^ 0x5deece66dULL) & kMask48; }
  int next(int bits) {
    s = (s * 0x5deece66dULL + 0xb) & kMask48;
    return static_cast<int>(static_cast<i64>(s) >> (48 - bits));
  }
  int nextInt(int n) {
    const int m = n - 1;
    if ((m & n) == 0) {
      const u64 x = static_cast<u64>(n) * static_cast<u64>(next(31));
      return static_cast<int>(static_cast<i64>(x) >> 31);
    }
    int bits, val;
    do {
      bits = next(31);
      val = bits % n;
    } while (static_cast<i32>(static_cast<u32>(bits) - static_cast<u32>(val) + static_cast<u32>(m)) < 0);
    return val;
  }
  double nextDouble() {
    u64 x = static_cast<u64>(next(26));
    x <<= 27;
    x += static_cast<u64>(next(27));
    return static_cast<double>(static_cast<i64>(x)) / static_cast<double>(1ULL << 53);
  }
  /// Avanza como si se hubieran hecho `n` llamadas a next().
  void skip(u64 n) {
    u64 m = 1, a = 0, im = 0x5deece66dULL, ia = 0xb;
    for (u64 k = n; k; k >>= 1) {
      if (k & 1) {
        m *= im;
        a = im * a + ia;
      }
      ia = (im + 1) * ia;
      im *= im;
    }
    s = (s * m + a) & kMask48;
  }
};

// --- Ruido de Perlin (el de 1.8, con sus particularidades) ---------------------------------------------

inline double lerp(double part, double from, double to) { return from + part * (to - from); }
inline double clampedLerp(double part, double from, double to) {
  if (part <= 0) return from;
  if (part >= 1) return to;
  return lerp(part, from, to);
}

inline double indexedLerp(u8 idx, double a, double b, double c) {
  switch (idx & 0xf) {
    case 0: return a + b;
    case 1: return -a + b;
    case 2: return a - b;
    case 3: return -a - b;
    case 4: return a + c;
    case 5: return -a + c;
    case 6: return a - c;
    case 7: return -a - c;
    case 8: return b + c;
    case 9: return -b + c;
    case 10: return b - c;
    case 11: return -b - c;
    case 12: return a + b;
    case 13: return -b + c;
    case 14: return -a + b;
    default: return -b - c;
  }
}

struct Perlin {
  u8 d[257]{};
  u8 h2 = 0;
  double a = 0, b = 0, c = 0, amplitude = 1, lacunarity = 1, d2 = 0, t2 = 0;

  void init(JavaRandom& rng) {
    a = rng.nextDouble() * 256.0;
    b = rng.nextDouble() * 256.0;
    c = rng.nextDouble() * 256.0;
    amplitude = 1.0;
    lacunarity = 1.0;
    for (int i = 0; i < 256; i++) d[i] = static_cast<u8>(i);
    for (int i = 0; i < 256; i++) {
      const int j = rng.nextInt(256 - i) + i;
      std::swap(d[i], d[j]);
    }
    d[256] = d[0];
    const double i2 = std::floor(b);
    const double f2 = b - i2;
    h2 = static_cast<u8>(static_cast<int>(i2));
    d2 = f2;
    t2 = f2 * f2 * f2 * (f2 * (f2 * 6.0 - 15.0) + 10.0);
  }

  double sample(double x, double y, double z, double yamp, double ymin) const {
    u8 hy;
    double ty;
    if (y == 0.0) {
      y = d2;
      hy = h2;
      ty = t2;
    } else {
      y += b;
      const double iy = std::floor(y);
      y -= iy;
      hy = static_cast<u8>(static_cast<int>(iy));
      ty = y * y * y * (y * (y * 6.0 - 15.0) + 10.0);
    }
    x += a;
    z += c;
    const double ix = std::floor(x), iz = std::floor(z);
    x -= ix;
    z -= iz;
    const u8 hx = static_cast<u8>(static_cast<int>(ix)), hz = static_cast<u8>(static_cast<int>(iz));
    const double tx = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
    const double tz = z * z * z * (z * (z * 6.0 - 15.0) + 10.0);
    if (yamp != 0.0) {
      const double yclamp = ymin < y ? ymin : y;
      y -= std::floor(yclamp / yamp) * yamp;
    }
    const u8 a1 = static_cast<u8>(d[hx] + hy), b1 = static_cast<u8>(d[hx + 1] + hy);
    const u8 a2 = static_cast<u8>(d[a1] + hz), b2 = static_cast<u8>(d[b1] + hz);
    const u8 a3 = static_cast<u8>(d[a1 + 1] + hz), b3 = static_cast<u8>(d[b1 + 1] + hz);
    double l1 = indexedLerp(d[a2], x, y, z);
    double l2 = indexedLerp(d[b2], x - 1, y, z);
    double l3 = indexedLerp(d[a3], x, y - 1, z);
    double l4 = indexedLerp(d[b3], x - 1, y - 1, z);
    double l5 = indexedLerp(d[a2 + 1], x, y, z - 1);
    double l6 = indexedLerp(d[b2 + 1], x - 1, y, z - 1);
    double l7 = indexedLerp(d[a3 + 1], x, y - 1, z - 1);
    double l8 = indexedLerp(d[b3 + 1], x - 1, y - 1, z - 1);
    l1 = lerp(tx, l1, l2);
    l3 = lerp(tx, l3, l4);
    l5 = lerp(tx, l5, l6);
    l7 = lerp(tx, l7, l8);
    l1 = lerp(ty, l1, l3);
    l5 = lerp(ty, l5, l7);
    return lerp(tz, l1, l5);
  }
};

/// Octavas de ruido que acaban en la octava 0 (omin + len - 1 == 0), el único caso que usa 1.8.
struct Octaves {
  std::vector<Perlin> o;
  void init(JavaRandom& rng, int len) {
    o.assign(static_cast<std::size_t>(len), Perlin{});
    double persist = 1.0 / static_cast<double>((1LL << len) - 1);
    double lacuna = 1.0;
    for (int i = 0; i < len; i++) {
      o[static_cast<std::size_t>(i)].init(rng);
      o[static_cast<std::size_t>(i)].amplitude = persist;
      o[static_cast<std::size_t>(i)].lacunarity = lacuna;
      persist *= 2.0;
      lacuna *= 0.5;
    }
  }
  /// Con `yDefault` el eje y queda fijo en el desplazamiento de cada octava.
  double sampleAmp(double x, double y, double z, double yamp, double ymin, bool yDefault) const {
    double v = 0;
    for (const Perlin& p : o) {
      const double lf = p.lacunarity;
      const double ay = yDefault ? -p.b : y * lf;
      v += p.amplitude * p.sample(x * lf, ay, z * lf, yamp * lf, ymin * lf);
    }
    return v;
  }
};

}  // namespace

// ======================================================================================================
// Capas de biomas
// ======================================================================================================

BiomeLayers::BiomeLayers(u64 worldSeed, bool largeBiomes) {
  layers_.reserve(64);
  // Cada línea: capa, sal base, padre(s). El orden y las sales son los de 1.8.
  int p = add(Kind::Continent, 1, -1);
  p = add(Kind::ZoomFuzzy, 2000, p);
  p = add(Kind::Land, 1, p);
  p = add(Kind::Zoom, 2001, p);
  p = add(Kind::Land, 2, p);
  p = add(Kind::Land, 50, p);
  p = add(Kind::Land, 70, p);
  p = add(Kind::Island, 2, p);
  p = add(Kind::Snow, 2, p);
  p = add(Kind::Land, 3, p);
  p = add(Kind::Cool, 2, p);
  p = add(Kind::Heat, 2, p);
  p = add(Kind::Special, 3, p);
  p = add(Kind::Zoom, 2002, p);
  p = add(Kind::Zoom, 2003, p);
  p = add(Kind::Land, 4, p);
  p = add(Kind::Mushroom, 5, p);
  const int deepOcean256 = p = add(Kind::DeepOcean, 4, p);
  p = add(Kind::Biome, 200, p);
  p = add(Kind::Zoom, 1000, p);
  p = add(Kind::Zoom, 1001, p);
  const int biomeEdge64 = p = add(Kind::BiomeEdge, 1000, p);
  const int riverInit256 = add(Kind::Noise, 100, deepOcean256);
  int hp = add(Kind::Zoom, 0, riverInit256);  // (las capas de las colinas no llevan sal)
  hp = add(Kind::Zoom, 0, hp);
  p = add(Kind::Hills, 1000, biomeEdge64, hp);
  p = add(Kind::Sunflower, 1001, p);
  p = add(Kind::Zoom, 1000, p);
  p = add(Kind::Land, 3, p);
  p = add(Kind::Zoom, 1001, p);
  p = add(Kind::Shore, 1000, p);
  p = add(Kind::Zoom, 1002, p);
  p = add(Kind::Zoom, 1003, p);
  if (largeBiomes) {
    p = add(Kind::Zoom, 1004, p);
    p = add(Kind::Zoom, 1005, p);
  }
  const int smooth4 = p = add(Kind::Smooth, 1000, p);
  int rv = add(Kind::Zoom, 1000, riverInit256);
  rv = add(Kind::Zoom, 1001, rv);
  rv = add(Kind::Zoom, 1000, rv);
  rv = add(Kind::Zoom, 1001, rv);
  rv = add(Kind::Zoom, 1002, rv);
  rv = add(Kind::Zoom, 1003, rv);
  rv = add(Kind::River, 1, rv);
  rv = add(Kind::Smooth, 1000, rv);
  p = add(Kind::RiverMix, 100, smooth4, rv);
  entry4_ = p;
  entry1_ = p = add(Kind::Voronoi, 10, p);

  // Sembrar todas las capas con la semilla del mundo
  for (Layer& l : layers_) {
    if (l.salt == 0) {
      l.startSalt = l.startSeed = 0;
      continue;
    }
    const u64 ls = layerSalt(l.salt);
    u64 st = worldSeed;
    st = stepSeed(st, ls);
    st = stepSeed(st, ls);
    st = stepSeed(st, ls);
    l.startSalt = st;
    l.startSeed = stepSeed(st, 0);
  }
}

int BiomeLayers::add(Kind kind, u64 saltBase, int p, int p2) {
  Layer l;
  l.kind = kind;
  l.p = p;
  l.p2 = p2;
  l.salt = saltBase;  // (se convierte en la sal de la capa al sembrar)
  layers_.push_back(l);
  return static_cast<int>(layers_.size()) - 1;
}

void BiomeLayers::area4(int x, int z, int w, int h, std::vector<int>& out) const { map(entry4_, x, z, w, h, out); }
void BiomeLayers::area1(int x, int z, int w, int h, std::vector<int>& out) const { map(entry1_, x, z, w, h, out); }

namespace {

inline std::size_t sz(int v) { return static_cast<std::size_t>(v); }

inline int select4(u32 cs, u32 st, int v00, int v01, int v10, int v11) {
  const int cv00 = (v00 == v10) + (v00 == v01) + (v00 == v11);
  const int cv10 = (v10 == v01) + (v10 == v11);
  const int cv01 = (v01 == v11);
  if (cv00 > cv10 && cv00 > cv01) return v00;
  if (cv10 > cv00) return v10;
  if (cv01 > cv00) return v01;
  cs *= cs * 1284865837u + 4150755663u;
  cs += st;
  const int r = (cs >> 24) & 3;
  return r == 0 ? v00 : r == 1 ? v10 : r == 2 ? v01 : v11;
}

inline bool isAll4JFTO(int a, int b, int c, int d) {
  auto ok = [](int v) { return getCategory(v) == jungle || v == forest || v == taiga || isOceanic(v); };
  return ok(a) && ok(b) && ok(c) && ok(d);
}
inline bool isAny4Oceanic(int a, int b, int c, int d) { return isOceanic(a) || isOceanic(b) || isOceanic(c) || isOceanic(d); }

}  // namespace

void BiomeLayers::map(int idx, int x, int z, int w, int h, std::vector<int>& out) const {
  const Layer& l = layers_[sz(idx)];
  const u64 st = l.startSalt, ss = l.startSeed;
  out.assign(sz(w) * sz(h), 0);
  std::vector<int> in;

  switch (l.kind) {
    case Kind::Continent: {
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) out[sz(j * w + i)] = firstIsZero(chunkSeed(ss, i + x, j + z), 10) ? 1 : 0;
      if (x > -w && x <= 0 && z > -h && z <= 0) out[sz(-z * w - x)] = 1;
      return;
    }

    case Kind::ZoomFuzzy:
    case Kind::Zoom: {
      const bool fuzzy = l.kind == Kind::ZoomFuzzy;
      const int pX = x >> 1, pZ = z >> 1;
      const int pW = ((x + w) >> 1) - pX + 1, pH = ((z + h) >> 1) - pZ + 1;
      map(l.p, pX, pZ, pW, pH, in);
      in.resize(sz(pW) * sz(pH + 1) + 1, 0);
      const int newW = pW * 2;
      std::vector<int> buf(sz(newW) * sz(pH) * 2 + 2, 0);
      const u32 st32 = static_cast<u32>(st), ss32 = static_cast<u32>(ss);
      for (int j = 0; j < pH; j++) {
        int idxb = (j * 2) * newW;
        int v00 = in[sz(j * pW)];
        int v01 = in[sz((j + 1) * pW)];
        for (int i = 0; i < pW; i++, v00 = in[sz(i + j * pW)], v01 = in[sz(i + (j + 1) * pW)]) {
          const int v10 = in[sz(i + 1 + j * pW)];
          const int v11 = in[sz(i + 1 + (j + 1) * pW)];
          if (v00 == v01 && v00 == v10 && v00 == v11) {
            buf[sz(idxb)] = v00;
            buf[sz(idxb + 1)] = v00;
            buf[sz(idxb + newW)] = v00;
            buf[sz(idxb + newW + 1)] = v00;
            idxb += 2;
            continue;
          }
          const int chunkX = (i + pX) * 2, chunkZ = (j + pZ) * 2;
          u32 cs = ss32;
          cs += static_cast<u32>(chunkX);
          cs *= cs * 1284865837u + 4150755663u;
          cs += static_cast<u32>(chunkZ);
          cs *= cs * 1284865837u + 4150755663u;
          cs += static_cast<u32>(chunkX);
          cs *= cs * 1284865837u + 4150755663u;
          cs += static_cast<u32>(chunkZ);
          buf[sz(idxb)] = v00;
          buf[sz(idxb + newW)] = ((cs >> 24) & 1) ? v01 : v00;
          idxb++;
          cs *= cs * 1284865837u + 4150755663u;
          cs += st32;
          buf[sz(idxb)] = ((cs >> 24) & 1) ? v10 : v00;
          if (fuzzy) {
            cs *= cs * 1284865837u + 4150755663u;
            cs += st32;
            const int r = (cs >> 24) & 3;
            buf[sz(idxb + newW)] = r == 0 ? v00 : r == 1 ? v10 : r == 2 ? v01 : v11;
          } else {
            buf[sz(idxb + newW)] = select4(cs, st32, v00, v01, v10, v11);
          }
          idxb++;
        }
      }
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) out[sz(j * w + i)] = buf[sz((j + (z & 1)) * newW + i + (x & 1))];
      return;
    }

    case Kind::Land: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++) {
        const int* vz0 = &in[sz(j * pW)];
        const int* vz1 = &in[sz((j + 1) * pW)];
        const int* vz2 = &in[sz((j + 2) * pW)];
        int v00 = vz0[0], vt0 = vz0[1];
        int v02 = vz2[0], vt2 = vz2[1];
        for (int i = 0; i < w; i++) {
          const int v11 = vz1[i + 1], v20 = vz0[i + 2], v22 = vz2[i + 2];
          int v = v11;
          if (v11 == ocean) {
            if (v00 || v20 || v02 || v22) {
              u64 cs = chunkSeed(ss, i + x, j + z);
              int inc = 0;
              v = 1;
              if (v00 != ocean) {
                ++inc;
                v = v00;
                cs = stepSeed(cs, st);
              }
              if (v20 != ocean) {
                if (++inc == 1 || firstIsZero(cs, 2)) v = v20;
                cs = stepSeed(cs, st);
              }
              if (v02 != ocean) {
                switch (++inc) {
                  case 1: v = v02; break;
                  case 2: if (firstIsZero(cs, 2)) v = v02; break;
                  default: if (firstIsZero(cs, 3)) v = v02;
                }
                cs = stepSeed(cs, st);
              }
              if (v22 != ocean) {
                switch (++inc) {
                  case 1: v = v22; break;
                  case 2: if (firstIsZero(cs, 2)) v = v22; break;
                  case 3: if (firstIsZero(cs, 3)) v = v22; break;
                  default: if (firstIsZero(cs, 4)) v = v22;
                }
                cs = stepSeed(cs, st);
              }
              if (v != forest) {
                if (!firstIsZero(cs, 3)) v = ocean;
              }
            }
          } else if (v11 == forest) {
            // sin cambios
          } else if (v00 == 0 || v20 == 0 || v02 == 0 || v22 == 0) {
            if (firstIsZero(chunkSeed(ss, i + x, j + z), 5)) v = 0;
          }
          out[sz(j * w + i)] = v;
          v00 = vt0;
          vt0 = v20;
          v02 = vt2;
          vt2 = v22;
        }
      }
      return;
    }

    case Kind::Island: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          const int v11 = in[sz(i + 1 + (j + 1) * pW)];
          out[sz(i + j * w)] = v11;
          if (v11 == Oceanic) {
            if (in[sz(i + 1 + j * pW)] != Oceanic) continue;
            if (in[sz(i + 2 + (j + 1) * pW)] != Oceanic) continue;
            if (in[sz(i + (j + 1) * pW)] != Oceanic) continue;
            if (in[sz(i + 1 + (j + 2) * pW)] != Oceanic) continue;
            if (firstIsZero(chunkSeed(ss, i + x, j + z), 2)) out[sz(i + j * w)] = 1;
          }
        }
      return;
    }

    case Kind::Snow: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          int v11 = in[sz(i + 1 + (j + 1) * pW)];
          if (!isShallowOcean(v11)) {
            const int r = firstInt(chunkSeed(ss, i + x, j + z), 6);
            v11 = r == 0 ? Freezing : (r <= 1 ? Cold : Warm);
          }
          out[sz(i + j * w)] = v11;
        }
      return;
    }

    case Kind::Cool:
    case Kind::Heat: {
      const bool cool = l.kind == Kind::Cool;
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          int v11 = in[sz(i + 1 + (j + 1) * pW)];
          if (v11 == (cool ? Warm : Freezing)) {
            const int v10 = in[sz(i + 1 + j * pW)], v21 = in[sz(i + 2 + (j + 1) * pW)];
            const int v01 = in[sz(i + (j + 1) * pW)], v12 = in[sz(i + 1 + (j + 2) * pW)];
            if (cool) {
              if (isAny4(Cold, v10, v21, v01, v12) || isAny4(Freezing, v10, v21, v01, v12)) v11 = Lush;
            } else {
              if (isAny4(Warm, v10, v21, v01, v12) || isAny4(Lush, v10, v21, v01, v12)) v11 = Cold;
            }
          }
          out[sz(i + j * w)] = v11;
        }
      return;
    }

    case Kind::Special: {
      map(l.p, x, z, w, h, out);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          int v = out[sz(i + j * w)];
          if (v == Oceanic) continue;
          u64 cs = chunkSeed(ss, i + x, j + z);
          if (firstIsZero(cs, 13)) {
            cs = stepSeed(cs, st);
            v |= static_cast<int>(static_cast<u32>(1 + firstInt(cs, 15)) << 8 & 0xf00);
            out[sz(i + j * w)] = v;
          }
        }
      return;
    }

    case Kind::Mushroom: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          int v11 = in[sz(i + 1 + (j + 1) * pW)];
          if (v11 == 0 && !in[sz(i + j * pW)] && !in[sz(i + 2 + j * pW)] && !in[sz(i + (j + 2) * pW)] && !in[sz(i + 2 + (j + 2) * pW)]) {
            if (firstIsZero(chunkSeed(ss, i + x, j + z), 100)) v11 = mushroom_fields;
          }
          out[sz(i + j * w)] = v11;
        }
      return;
    }

    case Kind::DeepOcean: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          int v11 = in[sz(i + 1 + (j + 1) * pW)];
          if (isShallowOcean(v11)) {
            int oceans = 0;
            if (isShallowOcean(in[sz(i + 1 + j * pW)])) oceans++;
            if (isShallowOcean(in[sz(i + 2 + (j + 1) * pW)])) oceans++;
            if (isShallowOcean(in[sz(i + (j + 1) * pW)])) oceans++;
            if (isShallowOcean(in[sz(i + 1 + (j + 2) * pW)])) oceans++;
            if (oceans >= 4) v11 = deep_ocean;
          }
          out[sz(i + j * w)] = v11;
        }
      return;
    }

    case Kind::Biome: {
      static constexpr int warmBiomes[] = {desert, desert, desert, savanna, savanna, plains};
      static constexpr int lushBiomes[] = {forest, dark_forest, mountains, plains, birch_forest, swamp};
      static constexpr int coldBiomes[] = {forest, mountains, taiga, plains};
      static constexpr int snowBiomes[] = {snowy_tundra, snowy_tundra, snowy_tundra, snowy_taiga};
      map(l.p, x, z, w, h, out);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          const std::size_t k = sz(i + j * w);
          int id = out[k];
          const int hasHighBit = id & 0xf00;
          id &= ~0xf00;
          if (isOceanic(id) || id == mushroom_fields) {
            out[k] = id;
            continue;
          }
          const u64 cs = chunkSeed(ss, i + x, j + z);
          int v;
          switch (id) {
            case Warm: v = hasHighBit ? (firstIsZero(cs, 3) ? badlands_plateau : wooded_badlands_plateau) : warmBiomes[firstInt(cs, 6)]; break;
            case Lush: v = hasHighBit ? jungle : lushBiomes[firstInt(cs, 6)]; break;
            case Cold: v = hasHighBit ? giant_tree_taiga : coldBiomes[firstInt(cs, 4)]; break;
            case Freezing: v = snowBiomes[firstInt(cs, 4)]; break;
            default: v = mushroom_fields;
          }
          out[k] = v;
        }
      return;
    }

    case Kind::BiomeEdge: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      auto replaceEdge = [&](int k, int v10, int v21, int v01, int v12, int id, int baseID, int edgeID) {
        if (id != baseID) return false;
        out[sz(k)] = (areSimilar(v10, baseID) && areSimilar(v21, baseID) && areSimilar(v01, baseID) && areSimilar(v12, baseID)) ? id : edgeID;
        return true;
      };
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          const int v11 = in[sz(i + 1 + (j + 1) * pW)], v10 = in[sz(i + 1 + j * pW)], v21 = in[sz(i + 2 + (j + 1) * pW)];
          const int v01 = in[sz(i + (j + 1) * pW)], v12 = in[sz(i + 1 + (j + 2) * pW)];
          const int k = i + j * w;
          if (replaceEdge(k, v10, v21, v01, v12, v11, wooded_badlands_plateau, badlands) ||
              replaceEdge(k, v10, v21, v01, v12, v11, badlands_plateau, badlands) ||
              replaceEdge(k, v10, v21, v01, v12, v11, giant_tree_taiga, taiga))
            continue;
          if (v11 == desert) {
            out[sz(k)] = !isAny4(snowy_tundra, v10, v21, v01, v12) ? v11 : wooded_mountains;
          } else if (v11 == swamp) {
            if (!isAny4(desert, v10, v21, v01, v12) && !isAny4(snowy_taiga, v10, v21, v01, v12) && !isAny4(snowy_tundra, v10, v21, v01, v12))
              out[sz(k)] = !isAny4(jungle, v10, v21, v01, v12) ? v11 : jungle_edge;
            else
              out[sz(k)] = plains;
          } else {
            out[sz(k)] = v11;
          }
        }
      return;
    }

    case Kind::Noise: {
      map(l.p, x, z, w, h, out);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          int& v = out[sz(i + j * w)];
          v = v > 0 ? firstInt(chunkSeed(ss, i + x, j + z), 299999) + 2 : 0;
        }
      return;
    }

    case Kind::Hills: {
      const int pW = w + 2, pH = h + 2;
      std::vector<int> riv;
      map(l.p, x - 1, z - 1, pW, pH, in);
      map(l.p2, x - 1, z - 1, pW, pH, riv);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          const int a11 = in[sz(i + 1 + (j + 1) * pW)];
          const int b11 = riv[sz(i + 1 + (j + 1) * pW)];
          const std::size_t k = sz(i + j * w);
          const int bn = (b11 - 2) % 29;
          if (bn == 1 && b11 >= 2 && !isShallowOcean(a11)) {
            const int m = getMutated(a11);
            out[k] = m > 0 ? m : a11;
            continue;
          }
          u64 cs = chunkSeed(ss, i + x, j + z);
          if (!(bn == 0 || firstIsZero(cs, 3))) {
            out[k] = a11;
            continue;
          }
          int hillID = a11;
          switch (a11) {
            case desert: hillID = desert_hills; break;
            case forest: hillID = wooded_hills; break;
            case birch_forest: hillID = birch_forest_hills; break;
            case dark_forest: hillID = plains; break;
            case taiga: hillID = taiga_hills; break;
            case giant_tree_taiga: hillID = giant_tree_taiga_hills; break;
            case snowy_taiga: hillID = snowy_taiga_hills; break;
            case plains:
              cs = stepSeed(cs, st);
              hillID = firstIsZero(cs, 3) ? wooded_hills : forest;
              break;
            case snowy_tundra: hillID = snowy_mountains; break;
            case jungle: hillID = jungle_hills; break;
            case ocean: hillID = deep_ocean; break;
            case mountains: hillID = wooded_mountains; break;
            case savanna: hillID = savanna_plateau; break;
            default:
              if (areSimilar(a11, wooded_badlands_plateau)) {
                hillID = badlands;
              } else if (isDeepOcean(a11)) {
                cs = stepSeed(cs, st);
                if (firstIsZero(cs, 3)) {
                  cs = stepSeed(cs, st);
                  hillID = firstIsZero(cs, 2) ? plains : forest;
                }
              }
              break;
          }
          if (bn == 0 && hillID != a11) {
            hillID = getMutated(hillID);
            if (hillID < 0) hillID = a11;
          }
          if (hillID != a11) {
            const int a10 = in[sz(i + 1 + j * pW)], a21 = in[sz(i + 2 + (j + 1) * pW)];
            const int a01 = in[sz(i + (j + 1) * pW)], a12 = in[sz(i + 1 + (j + 2) * pW)];
            int equals = 0;
            if (areSimilar(a10, a11)) equals++;
            if (areSimilar(a21, a11)) equals++;
            if (areSimilar(a01, a11)) equals++;
            if (areSimilar(a12, a11)) equals++;
            out[k] = equals >= 3 ? hillID : a11;
          } else {
            out[k] = a11;
          }
        }
      return;
    }

    case Kind::River: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      auto reduceID = [](int id) { return id >= 2 ? 2 + (id & 1) : id; };
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          const int v01 = reduceID(in[sz(i + (j + 1) * pW)]), v11 = reduceID(in[sz(i + 1 + (j + 1) * pW)]);
          const int v21 = reduceID(in[sz(i + 2 + (j + 1) * pW)]), v10 = reduceID(in[sz(i + 1 + j * pW)]);
          const int v12 = reduceID(in[sz(i + 1 + (j + 2) * pW)]);
          out[sz(i + j * w)] = (v11 == v01 && v11 == v10 && v11 == v12 && v11 == v21) ? -1 : river;
        }
      return;
    }

    case Kind::Smooth: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          int v11 = in[sz(i + 1 + (j + 1) * pW)];
          const int v01 = in[sz(i + (j + 1) * pW)], v10 = in[sz(i + 1 + j * pW)];
          if (v11 != v01 || v11 != v10) {
            const int v21 = in[sz(i + 2 + (j + 1) * pW)], v12 = in[sz(i + 1 + (j + 2) * pW)];
            if (v01 == v21 && v10 == v12) {
              v11 = (chunkSeed(ss, i + x, j + z) & (1ULL << 24)) ? v10 : v01;
            } else {
              if (v01 == v21) v11 = v01;
              if (v10 == v12) v11 = v10;
            }
          }
          out[sz(i + j * w)] = v11;
        }
      return;
    }

    case Kind::Sunflower: {
      map(l.p, x, z, w, h, out);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
          if (out[sz(i + j * w)] == plains && firstIsZero(chunkSeed(ss, i + x, j + z), 57)) out[sz(i + j * w)] = sunflower_plains;
      return;
    }

    case Kind::Shore: {
      const int pW = w + 2, pH = h + 2;
      map(l.p, x - 1, z - 1, pW, pH, in);
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
          const int v11 = in[sz(i + 1 + (j + 1) * pW)], v10 = in[sz(i + 1 + j * pW)], v21 = in[sz(i + 2 + (j + 1) * pW)];
          const int v01 = in[sz(i + (j + 1) * pW)], v12 = in[sz(i + 1 + (j + 2) * pW)];
          int& o = out[sz(i + j * w)];
          auto replaceOcean = [&](int replaceID) { o = isAny4Oceanic(v10, v21, v01, v12) ? replaceID : v11; };
          if (v11 == mushroom_fields) {
            o = isAny4(ocean, v10, v21, v01, v12) ? mushroom_field_shore : v11;
          } else if (getCategory(v11) == jungle) {
            if (isAll4JFTO(v10, v21, v01, v12)) o = isAny4Oceanic(v10, v21, v01, v12) ? beach : v11;
            else o = jungle_edge;
          } else if (v11 == mountains || v11 == wooded_mountains) {
            replaceOcean(stone_shore);
          } else if (isSnowy(v11)) {
            replaceOcean(snowy_beach);
          } else if (v11 == badlands || v11 == wooded_badlands_plateau) {
            if (!isAny4Oceanic(v10, v21, v01, v12)) o = (isMesa(v10) && isMesa(v21) && isMesa(v01) && isMesa(v12)) ? v11 : desert;
            else o = v11;
          } else if (v11 != ocean && v11 != deep_ocean && v11 != river && v11 != swamp) {
            o = isAny4Oceanic(v10, v21, v01, v12) ? beach : v11;
          } else {
            o = v11;
          }
        }
      return;
    }

    case Kind::RiverMix: {
      std::vector<int> riv;
      map(l.p, x, z, w, h, out);
      map(l.p2, x, z, w, h, riv);
      for (std::size_t k = 0; k < out.size(); k++) {
        int v = out[k];
        if (riv[k] == river && v != ocean && !isOceanic(v)) {
          if (v == snowy_tundra) v = frozen_river;
          else if (v == mushroom_fields || v == mushroom_field_shore) v = mushroom_field_shore;
          else v = river;
        }
        out[k] = v;
      }
      return;
    }

    case Kind::Voronoi: {
      x -= 2;
      z -= 2;
      const int pX = x >> 2, pZ = z >> 2;
      const int pW = ((x + w) >> 2) - pX + 2, pH = ((z + h) >> 2) - pZ + 2;
      map(l.p, pX, pZ, pW, pH, in);
      for (int pj = 0; pj < pH - 1; pj++) {
        int v00 = in[sz(pj * pW)];
        int v01 = in[sz((pj + 1) * pW)];
        const int pjz = pZ + pj;
        const int j4 = pjz * 4 - z;
        for (int pi = 0; pi < pW - 1; pi++, v00 = in[sz(pi + pj * pW)], v01 = in[sz(pi + (pj + 1) * pW)]) {
          const int pix = pX + pi;
          const int i4 = pix * 4 - x;
          const int v10 = in[sz(pi + 1 + pj * pW)], v11 = in[sz(pi + 1 + (pj + 1) * pW)];
          if (v00 == v01 && v00 == v10 && v00 == v11) {
            for (int jj = 0; jj < 4; jj++) {
              const int j = j4 + jj;
              if (j < 0 || j >= h) continue;
              for (int ii = 0; ii < 4; ii++) {
                const int i = i4 + ii;
                if (i < 0 || i >= w) continue;
                out[sz(j * w + i)] = v00;
              }
            }
            continue;
          }
          u64 cs = chunkSeed(ss, (pi + pX) * 4, (pj + pZ) * 4);
          const i64 da1 = (firstInt(cs, 1024) - 512) * 36;
          cs = stepSeed(cs, st);
          const i64 da2 = (firstInt(cs, 1024) - 512) * 36;
          cs = chunkSeed(ss, (pi + pX + 1) * 4, (pj + pZ) * 4);
          const i64 db1 = (firstInt(cs, 1024) - 512) * 36 + 40 * 1024;
          cs = stepSeed(cs, st);
          const i64 db2 = (firstInt(cs, 1024) - 512) * 36;
          cs = chunkSeed(ss, (pi + pX) * 4, (pj + pZ + 1) * 4);
          const i64 dc1 = (firstInt(cs, 1024) - 512) * 36;
          cs = stepSeed(cs, st);
          const i64 dc2 = (firstInt(cs, 1024) - 512) * 36 + 40 * 1024;
          cs = chunkSeed(ss, (pi + pX + 1) * 4, (pj + pZ + 1) * 4);
          const i64 dd1 = (firstInt(cs, 1024) - 512) * 36 + 40 * 1024;
          cs = stepSeed(cs, st);
          const i64 dd2 = (firstInt(cs, 1024) - 512) * 36 + 40 * 1024;
          for (int jj = 0; jj < 4; jj++) {
            const int j = j4 + jj;
            if (j < 0 || j >= h) continue;
            const i64 mj = 10240 * jj;
            const i64 sja = (mj - da2) * (mj - da2), sjb = (mj - db2) * (mj - db2), sjc = (mj - dc2) * (mj - dc2), sjd = (mj - dd2) * (mj - dd2);
            for (int ii = 0; ii < 4; ii++) {
              const int i = i4 + ii;
              if (i < 0 || i >= w) continue;
              const i64 mi = 10240 * ii;
              const i64 da = (mi - da1) * (mi - da1) + sja, db = (mi - db1) * (mi - db1) + sjb;
              const i64 dc = (mi - dc1) * (mi - dc1) + sjc, dd = (mi - dd1) * (mi - dd1) + sjd;
              int v;
              if (da < db && da < dc && da < dd) v = v00;
              else if (db < da && db < dc && db < dd) v = v10;
              else if (dc < da && dc < db && dc < dd) v = v01;
              else v = v11;
              out[sz(j * w + i)] = v;
            }
          }
        }
      }
      return;
    }
  }
}

// ======================================================================================================
// Relieve
// ======================================================================================================

bool biomeDepthScale(int id, double& d, double& s) {
  switch (id) {
    case ocean: s = 0.100; d = -1.000; return true;
    case plains: s = 0.050; d = 0.125; return true;
    case desert: s = 0.050; d = 0.125; return true;
    case mountains: s = 0.500; d = 1.000; return true;
    case forest: s = 0.200; d = 0.100; return true;
    case taiga: s = 0.200; d = 0.200; return true;
    case swamp: s = 0.100; d = -0.200; return true;
    case river: s = 0.000; d = -0.500; return true;
    case frozen_ocean: s = 0.100; d = -1.000; return true;
    case frozen_river: s = 0.000; d = -0.500; return true;
    case snowy_tundra: s = 0.050; d = 0.125; return true;
    case snowy_mountains: s = 0.300; d = 0.450; return true;
    case mushroom_fields: s = 0.300; d = 0.200; return true;
    case mushroom_field_shore: s = 0.025; d = 0.000; return true;
    case beach: s = 0.025; d = 0.000; return true;
    case desert_hills: s = 0.300; d = 0.450; return true;
    case wooded_hills: s = 0.300; d = 0.450; return true;
    case taiga_hills: s = 0.300; d = 0.450; return true;
    case mountain_edge: s = 0.300; d = 0.800; return true;
    case jungle: s = 0.200; d = 0.100; return true;
    case jungle_hills: s = 0.300; d = 0.450; return true;
    case jungle_edge: s = 0.200; d = 0.100; return true;
    case deep_ocean: s = 0.100; d = -1.800; return true;
    case stone_shore: s = 0.800; d = 0.100; return true;
    case snowy_beach: s = 0.025; d = 0.000; return true;
    case birch_forest: s = 0.200; d = 0.100; return true;
    case birch_forest_hills: s = 0.300; d = 0.450; return true;
    case dark_forest: s = 0.200; d = 0.100; return true;
    case snowy_taiga: s = 0.200; d = 0.200; return true;
    case snowy_taiga_hills: s = 0.300; d = 0.450; return true;
    case giant_tree_taiga: s = 0.200; d = 0.200; return true;
    case giant_tree_taiga_hills: s = 0.300; d = 0.450; return true;
    case wooded_mountains: s = 0.500; d = 1.000; return true;
    case savanna: s = 0.050; d = 0.125; return true;
    case savanna_plateau: s = 0.025; d = 1.500; return true;
    case badlands: s = 0.200; d = 0.100; return true;
    case wooded_badlands_plateau: s = 0.025; d = 1.500; return true;
    case badlands_plateau: s = 0.025; d = 1.500; return true;
    case sunflower_plains: s = 0.050; d = 0.125; return true;
    case desert_lakes: s = 0.250; d = 0.225; return true;
    case gravelly_mountains: s = 0.500; d = 1.000; return true;
    case flower_forest: s = 0.400; d = 0.100; return true;
    case taiga_mountains: s = 0.400; d = 0.300; return true;
    case swamp_hills: s = 0.300; d = -0.100; return true;
    case ice_spikes: s = 0.450; d = 0.425; return true;
    case modified_jungle: s = 0.400; d = 0.200; return true;
    case modified_jungle_edge: s = 0.400; d = 0.200; return true;
    case tall_birch_forest: s = 0.400; d = 0.200; return true;
    case tall_birch_hills: s = 0.500; d = 0.550; return true;
    case dark_forest_hills: s = 0.400; d = 0.200; return true;
    case snowy_taiga_mountains: s = 0.400; d = 0.300; return true;
    case giant_spruce_taiga: s = 0.200; d = 0.200; return true;
    case giant_spruce_taiga_hills: s = 0.200; d = 0.200; return true;
    case modified_gravelly_mountains: s = 0.500; d = 1.000; return true;
    case shattered_savanna: s = 1.225; d = 0.3625; return true;
    case shattered_savanna_plateau: s = 1.212; d = 1.050; return true;
    case eroded_badlands: s = 0.200; d = 0.100; return true;
    case modified_wooded_badlands_plateau: s = 0.300; d = 0.450; return true;
    case modified_badlands_plateau: s = 0.300; d = 0.450; return true;
    default: return false;
  }
}

/// Las octavas del ruido de superficie, sembradas como en 1.8.
struct Terrain::Noise {
  Octaves minOct, maxOct, mainOct, depth;  // (la de la superficie del suelo no se usa para la altura)
  static constexpr double kXzScale = 0.9999999814507745, kYScale = 0.9999999814507745, kXzFactor = 80, kYFactor = 160;

  explicit Noise(u64 seed) {
    JavaRandom rng;
    rng.setSeed(seed);
    minOct.init(rng, 16);
    maxOct.init(rng, 16);
    mainOct.init(rng, 8);
    Octaves surf;
    surf.init(rng, 4);
    rng.skip(262 * 10);
    depth.init(rng, 16);
  }

  /// La densidad base del terreno en el punto (x, y, z) de la rejilla (x, z a escala 1:4; y en pasos de 8 bloques).
  double sample(int x, int y, int z) const {
    const double xzScale = 684.412 * kXzScale, yScale = 684.412 * kYScale;
    const double xzStep = xzScale / kXzFactor, yStep = yScale / kYFactor;
    double minNoise = 0, maxNoise = 0, mainNoise = 0, persist = 1.0, contrib = 1.0;
    for (int i = 0; i < 16; i++) {
      double dx = x * xzScale * persist, dy = y * yScale * persist, dz = z * xzScale * persist;
      double sy = yScale * persist, ty = y * sy;
      minNoise += minOct.o[sz(i)].sample(dx, dy, dz, sy, ty) * contrib;
      maxNoise += maxOct.o[sz(i)].sample(dx, dy, dz, sy, ty) * contrib;
      if (i < 8) {
        dx = x * xzStep * persist;
        dy = y * yStep * persist;
        dz = z * xzStep * persist;
        sy = yStep * persist;
        ty = y * sy;
        mainNoise += mainOct.o[sz(i)].sample(dx, dy, dz, sy, ty) * contrib;
      }
      persist *= 0.5;
      contrib *= 2.0;
    }
    return clampedLerp(0.5 + 0.05 * mainNoise, minNoise / 512.0, maxNoise / 512.0);
  }
};

Terrain::Terrain(u64 worldSeed, bool largeBiomes) : layers_(worldSeed, largeBiomes), noise_(std::make_unique<Noise>(worldSeed)) {}
Terrain::~Terrain() = default;

int Terrain::biomeAt(int x, int z) const {
  std::vector<int> b;
  layers_.area1(x, z, 1, 1, b);
  return b[0];
}

std::shared_ptr<const ChunkTerrain> Terrain::chunk(int cx, int cz) const {
  const u64 key = (static_cast<u64>(static_cast<u32>(cx)) << 32) | static_cast<u32>(cz);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
  }
  auto made = compute(cx, cz);
  std::lock_guard<std::mutex> lock(mutex_);
  if (cache_.size() >= 4096) cache_.clear();
  cache_[key] = made;
  return made;
}

std::shared_ptr<const ChunkTerrain> Terrain::compute(int cx, int cz) const {
  // Los biomas a escala 1:4 alrededor del chunk (la altura de cada punto mezcla los 5x5 vecinos)
  std::vector<int> b4;
  layers_.area4(cx * 4 - 2, cz * 4 - 2, 9, 9, b4);

  // Perfil de densidad de las 5x5 columnas de la rejilla, 33 niveles (de 8 bloques) cada una
  static constexpr float kKernel[25] = {
      3.302044127f, 4.104975761f, 4.545454545f, 4.104975761f, 3.302044127f, 4.104975761f, 6.194967155f, 8.333333333f, 6.194967155f,
      4.104975761f, 4.545454545f, 8.333333333f, 50.00000000f, 8.333333333f, 4.545454545f, 4.104975761f, 6.194967155f, 8.333333333f,
      6.194967155f, 4.104975761f, 3.302044127f, 4.104975761f, 4.545454545f, 4.104975761f, 3.302044127f};
  std::array<double, 5 * 5 * 33> dens{};
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < 5; i++) {
      double d0 = 0, s0 = 0;
      biomeDepthScale(b4[sz((j + 2) * 9 + (i + 2))], d0, s0);
      double wt = 0, ws = 0, wd = 0;
      for (int jj = 0; jj < 5; jj++)
        for (int ii = 0; ii < 5; ii++) {
          double d = 0, s = 0;
          biomeDepthScale(b4[sz((j + jj) * 9 + (i + ii))], d, s);
          float weight = kKernel[jj * 5 + ii] / static_cast<float>(d + 2);
          if (d > d0) weight *= 0.5f;
          ws += s * weight;
          wd += d * weight;
          wt += weight;
        }
      ws /= wt;
      wd /= wt;
      ws = ws * 0.9 + 0.1;
      wd = (wd * 4.0 - 1) / 8;
      const double scale = 96 / ws;
      const double depth = wd * 17. / 64;

      const int gx = cx * 4 + i, gz = cz * 4 + j;
      double off = noise_->depth.sampleAmp(gx * 200.0, 10, gz * 200.0, 1, 0, true);
      off *= 65535. / 8000;
      if (off < 0) off = -0.3 * off;
      off = off * 3 - 2;
      if (off > 1) off = 1;
      off *= 17. / 64;
      off *= off < 0 ? 1. / 28 : 1. / 40;

      for (int gy = 0; gy < 33; gy++) {
        double n0 = noise_->sample(gx, gy, gz);
        double fall = 1 - 2 * gy / 32.0 + off - 0.46875;
        fall = scale * (fall + depth);
        n0 += fall > 0 ? 4 * fall : fall;
        dens[sz((j * 5 + i) * 33 + gy)] = n0;
      }
    }

  auto out = std::make_shared<ChunkTerrain>();
  std::vector<int> b1;
  layers_.area1(cx * 16, cz * 16, 16, 16, b1);
  for (int z = 0; z < 16; z++)
    for (int x = 0; x < 16; x++) {
      const int k = x + z * 16;
      out->biome[sz(k)] = static_cast<u8>(b1[sz(k)]);
      const int i = x >> 2, j = z >> 2;
      const double fx = (x & 3) / 4.0, fz = (z & 3) / 4.0;
      // Perfil de la columna: interpolación bilineal de las cuatro esquinas de la celda
      double prof[33];
      for (int gy = 0; gy < 33; gy++) {
        const double d00 = dens[sz((j * 5 + i) * 33 + gy)], d10 = dens[sz((j * 5 + i + 1) * 33 + gy)];
        const double d01 = dens[sz(((j + 1) * 5 + i) * 33 + gy)], d11 = dens[sz(((j + 1) * 5 + i + 1) * 33 + gy)];
        prof[gy] = lerp(fz, lerp(fx, d00, d10), lerp(fx, d01, d11));
      }
      int top = 0;
      for (int y = 255; y > 0; y--) {
        const int gy = y >> 3;
        if (lerp((y & 7) / 8.0, prof[gy], prof[gy + 1]) > 0) {
          top = y;
          break;
        }
      }
      out->top[sz(k)] = static_cast<i16>(top);
    }
  return out;
}


// ======================================================================================================
// Estructuras
// ======================================================================================================

namespace {

constexpr double kPi = 3.14159265358979323846;

u64 nextLong(JavaRandom& r) {
  const u64 hi = static_cast<u64>(static_cast<i64>(r.next(32))) << 32;
  const u64 lo = static_cast<u64>(static_cast<i64>(r.next(32)));
  return hi + lo;
}

/// Un bioma que puede llevar un fortín (en 1.8: ni océanos, ni ríos, ni playas, ni pantanos, ni la orilla de la isla de hongos).
bool strongholdBiome(int id) {
  static constexpr int kValid[] = {plains, desert, mountains, forest, taiga, snowy_tundra, snowy_mountains, mushroom_fields, desert_hills,
                                   wooded_hills, taiga_hills, jungle, jungle_hills, jungle_edge, stone_shore, birch_forest, birch_forest_hills,
                                   dark_forest, snowy_taiga, snowy_taiga_hills, giant_tree_taiga, giant_tree_taiga_hills, wooded_mountains,
                                   savanna, savanna_plateau, badlands, wooded_badlands_plateau, badlands_plateau, sunflower_plains, desert_lakes,
                                   gravelly_mountains, flower_forest, taiga_mountains, ice_spikes, modified_jungle, modified_jungle_edge,
                                   tall_birch_forest, tall_birch_hills, dark_forest_hills, snowy_taiga_mountains, giant_spruce_taiga,
                                   giant_spruce_taiga_hills, modified_gravelly_mountains, shattered_savanna, shattered_savanna_plateau,
                                   eroded_badlands, modified_wooded_badlands_plateau, modified_badlands_plateau};
  return std::find(std::begin(kValid), std::end(kValid), id) != std::end(kValid);
}

/// Busca un bioma entre los `valid` alrededor de (x, z) en un radio y elige uno al azar, con el azar y el recuento de 1.8
/// (el contador solo sube cuando se cambia de elección). Si no hay ninguno, devuelve (x, z).
StructPos locateBiome(const BiomeLayers& layers, int x, int z, int radius, const std::function<bool(int)>& valid, JavaRandom& rng, bool* any) {
  StructPos out{x, z};
  const int x1 = (x - radius) >> 2, z1 = (z - radius) >> 2, x2 = (x + radius) >> 2, z2 = (z + radius) >> 2;
  const int width = x2 - x1 + 1, height = z2 - z1 + 1;
  std::vector<int> ids;
  layers.area4(x1, z1, width, height, ids);
  int found = 0;
  for (int i = 0; i < width * height; i++) {
    if (!valid(ids[sz(i)])) continue;
    if (found == 0 || rng.nextInt(found + 1) == 0) {
      out.x = (x1 + i % width) * 4;
      out.z = (z1 + i / width) * 4;
      ++found;
    }
  }
  if (any) *any = found > 0;
  return out;
}

/// La altura a la que el suelo de un bioma cuenta como hierba para el punto de aparición (0: nunca).
int spawnGrassHeight(int id) {
  switch (id) {
    case desert: case mushroom_fields: case mushroom_field_shore: case desert_hills: case badlands: case wooded_badlands_plateau:
    case badlands_plateau: case desert_lakes: case ice_spikes: case eroded_badlands: case modified_wooded_badlands_plateau:
    case modified_badlands_plateau:
      return 0;
    case river: case frozen_river: return 60;
    case beach: case stone_shore: case snowy_beach: return 64;
    default: return 62;
  }
}

}  // namespace

StructPos regionAttempt(u64 seed, int rx, int rz, u32 salt, int region, int range, bool large) {
  JavaRandom r;
  r.setSeed(ext(rx) * 341873128712ULL + ext(rz) * 132897987541ULL + seed + salt);
  int cx, cz;
  if (large) {
    cx = r.nextInt(range);
    cx += r.nextInt(range);
    cz = r.nextInt(range);
    cz += r.nextInt(range);
    cx >>= 1;
    cz >>= 1;
  } else {
    cx = r.nextInt(range);
    cz = r.nextInt(range);
  }
  return {(rx * region + cx) * 16, (rz * region + cz) * 16};
}

bool mineshaftChunk(u64 seed, int cx, int cz) {
  JavaRandom s;
  s.setSeed(seed);
  const u64 a = nextLong(s);
  const u64 b = nextLong(s);
  s.setSeed((ext(cx) * a ^ seed) ^ (ext(cz) * b));
  s.next(32);  // (nextInt() sin usar)
  if (!(s.nextDouble() < 0.004)) return false;
  const int d = std::max(std::max(cx, -cx), std::max(cz, -cz));
  return d >= 80 || s.nextInt(80) < d;
}

bool netherFortressAttempt(u64 seed, int rx, int rz, StructPos& out) {
  u64 st = seed;
  st ^= static_cast<u64>(rx) ^ (static_cast<u64>(rz) << 4);
  JavaRandom r;
  r.setSeed(st);
  r.next(31);
  const bool valid = r.nextInt(3) == 0;
  out.x = (rx * 16 + r.nextInt(8) + 4) * 16;
  out.z = (rz * 16 + r.nextInt(8) + 4) * 16;
  return valid;
}

std::array<StructPos, 3> strongholds(u64 seed, const BiomeLayers& layers) {
  JavaRandom rnds;
  rnds.setSeed(seed);
  double angle = 2.0 * kPi * rnds.nextDouble();
  double dist = (1.25 + rnds.nextDouble()) * 32.0;
  StructPos approx{static_cast<int>(std::round(std::cos(angle) * dist)) * 16 + 8, static_cast<int>(std::round(std::sin(angle) * dist)) * 16 + 8};
  std::array<StructPos, 3> out{};
  for (int i = 0; i < 3; i++) {
    StructPos p = locateBiome(layers, approx.x, approx.z, 112, strongholdBiome, rnds, nullptr);
    out[sz(i)] = {(p.x & ~15) + 4, (p.z & ~15) + 4};
    angle += 2 * kPi / 3;  // (los tres fortines del primer anillo, a un tercio de vuelta)
    dist = (1.25 + rnds.nextDouble()) * 32.0;
    approx = {static_cast<int>(std::round(std::cos(angle) * dist)) * 16 + 8, static_cast<int>(std::round(std::sin(angle) * dist)) * 16 + 8};
  }
  return out;
}

StructPos worldSpawn(u64 seed, const BiomeLayers& layers, const std::function<int(int, int)>& surfaceTop) {
  JavaRandom rng;
  rng.setSeed(seed);
  bool found = false;
  auto spawnBiome = [](int id) { return id == forest || id == plains || id == taiga || id == taiga_hills || id == wooded_hills || id == jungle || id == jungle_hills; };
  StructPos spawn = locateBiome(layers, 0, 0, 256, spawnBiome, rng, &found);
  if (!found) spawn = {8, 8};
  for (int i = 0; i < 1000; i++) {
    std::vector<int> b;
    layers.area4(spawn.x >> 2, spawn.z >> 2, 1, 1, b);
    const int grass = spawnGrassHeight(b[0]);
    if (grass > 0 && surfaceTop((spawn.x >> 2) * 4, (spawn.z >> 2) * 4) >= grass) break;
    const int dx = rng.nextInt(64);
    spawn.x += dx - rng.nextInt(64);
    const int dz = rng.nextInt(64);
    spawn.z += dz - rng.nextInt(64);
  }
  return spawn;
}

}  // namespace mcw::legacy
