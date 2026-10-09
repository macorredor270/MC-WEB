#include "client/mesher.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "assets/models.h"
#include "assets/textures.h"
#include "client/visibility.h"
#include "core/face.h"
#include "core/random.h"
#include "data/blockstates.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr int P = MeshInput::P;
constexpr float kFaceShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
// Ejes tangentes de cada cara (para buscar los vecinos de AO de cada vértice)
constexpr int kTangents[6][2] = {{0, 2}, {0, 2}, {0, 1}, {0, 1}, {2, 1}, {2, 1}};

inline const BlockInfo& info(BlockState s) { return blockInfo(stateId(s)); }

/// Redondeo al entero más cercano sin pasar por libm (se llama 5 veces por vértice).
inline int roundi(float v) { return static_cast<int>(v >= 0.0f ? v + 0.5f : v - 0.5f); }

inline bool isLeaves(int id) { return id == B::leaves || id == B::leaves2; }

/// ¿El vecino tapa la cara del bloque `self`?
inline bool occludes(BlockState neighbor, BlockState self) {
  const BlockInfo& n = info(neighbor);
  if (n.opaqueCube) return true;
  return info(self).selfCull && stateId(neighbor) == stateId(self);
}

/// Con hojas rápidas, las hojas son como bloques opacos: se tapan entre sí y tapan lo de al lado.
inline bool occludesFast(BlockState neighbor, BlockState self) {
  return occludes(neighbor, self) || isLeaves(stateId(neighbor));
}

/// Estados virtuales que dependen de los vecinos (como el estado "real" de 1.8).
BlockState resolveState(const MeshInput& in, int px, int py, int pz, BlockState s) {
  const int id = stateId(s);
  if (id == B::double_plant && (stateMeta(s) & 8)) {
    const BlockState below = in.blocks[MeshInput::idx(px, py - 1, pz)];
    const int type = stateId(below) == B::double_plant ? (stateMeta(below) & 7) : 2;
    return makeState(B::double_plant, 8 | type);
  }
  if (id == B::grass || id == B::mycelium || (id == B::dirt && stateMeta(s) == 2)) {
    const int above = stateId(in.blocks[MeshInput::idx(px, py + 1, pz)]);
    if (above == B::snow_layer || above == B::snow) return static_cast<BlockState>(s | 8);
  }
  return s;
}

/// Vecinos de un bloque dentro de la sección con borde (para las conexiones de vallas, etc.).
struct Around {
  const MeshInput* in;
  int px, py, pz;
  static BlockState get(const void* ctx, int dx, int dy, int dz) {
    const Around* a = static_cast<const Around*>(ctx);
    return a->in->blocks[MeshInput::idx(a->px + dx, a->py + dy, a->pz + dz)];
  }
};

struct Tints {
  std::array<u32, 256> grass{}, foliage{};
};

Tints computeTints(const MeshInput& in, const Colormaps& colors) {
  // Media 3x3 de colores de bioma para suavizar las transiciones
  Tints t;
  for (int z = 0; z < 16; z++)
    for (int x = 0; x < 16; x++) {
      int gr = 0, gg = 0, gb = 0, fr = 0, fg = 0, fb = 0;
      for (int dz = 0; dz < 3; dz++)
        for (int dx = 0; dx < 3; dx++) {
          const int b = in.biomes[(z + dz) * P + (x + dx)];
          const u32 g = colors.grass(b), f = colors.foliage(b);
          gr += (g >> 16) & 255; gg += (g >> 8) & 255; gb += g & 255;
          fr += (f >> 16) & 255; fg += (f >> 8) & 255; fb += f & 255;
        }
      t.grass[z * 16 + x] = (u32(gr / 9) << 16) | (u32(gg / 9) << 8) | u32(gb / 9);
      t.foliage[z * 16 + x] = (u32(fr / 9) << 16) | (u32(fg / 9) << 8) | u32(fb / 9);
    }
  return t;
}

u32 tintFor(BlockState s, int x, int z, const Tints& tints) {
  if (stateId(s) == 55) return redstoneWireColor(stateMeta(s));
  if (stateId(s) == 104 || stateId(s) == 105) return stemColor(stateMeta(s) & 7);
  switch (tintTypeOf(s)) {
    case TintType::Grass: return tints.grass[z * 16 + x];
    case TintType::Foliage: return tints.foliage[z * 16 + x];
    case TintType::Birch: return 0x80A755;
    case TintType::Spruce: return 0x619961;
    case TintType::Constant: return info(s).tintColor;
    default: return 0xFFFFFF;
  }
}

class Builder {
 public:
  Builder(const MeshInput& in, const MesherContext& ctx, MeshOutput& out) : in_(in), ctx_(ctx), out_(out) {
    tints_ = computeTints(in, *ctx.colors);
  }

  void run() {
    for (int y = 0; y < 16; y++)
      for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
          const BlockState raw = in_.blocks[MeshInput::idx(x + 1, y + 1, z + 1)];
          if (raw == 0) continue;
          const BlockInfo& bi = info(raw);
          if (bi.fluid) {
            fluid(x, y, z, raw);
            continue;
          }
          const BlockState s = resolveState(in_, x + 1, y + 1, z + 1, raw);
          const VariantList* vl;
          if (dependsOnNeighbors(stateId(s))) {
            const Around around{&in_, x + 1, y + 1, z + 1};
            const int ext = neighborBits(s, &around, &Around::get);
            vl = ctx_.models->forExtended(extReplacesMeta(stateId(s)) ? makeState(stateId(s), 0) : s, ext);
          } else {
            vl = ctx_.models->forState(s);
          }
          if (!vl) vl = &ctx_.models->missing();
          const u32 h = hash3(in_.sx * 16 + x, in_.sy * 16 + y, in_.sz * 16 + z);
          block(x, y, z, raw, vl->pick(h), bi.layer == RenderLayer::Translucent ? out_.translucent : out_.opaque);
        }
  }

 private:
  BlockState at(int px, int py, int pz) const { return in_.blocks[MeshInput::idx(px, py, pz)]; }
  u8 lightAt(int px, int py, int pz) const { return in_.light[MeshInput::idx(px, py, pz)]; }

  void block(int x, int y, int z, BlockState self, const BakedModel& model, std::vector<ChunkVertex>& dst) {
    const int px = x + 1, py = y + 1, pz = z + 1;
    const u32 tint = tintFor(self, x, z, tints_);
    const bool fast = !(in_.flags & kMeshFancyLeaves);
    const bool solidLeaves = fast && isLeaves(stateId(self));
    for (const BakedQuad& q : model.quads) {
      // Hojas rápidas: sus caras se tapan como las de un cubo aunque el modelo no diga "cullface"
      const int cull = q.cullface >= 0 ? q.cullface : (solidLeaves && q.onFace ? q.face : -1);
      if (cull >= 0) {
        const auto& n = kFaceNormals[cull];
        const BlockState nb = at(px + n[0], py + n[1], pz + n[2]);
        if (fast ? occludesFast(nb, self) : occludes(nb, self)) continue;
      }
      float ao[4] = {1, 1, 1, 1}, sky[4], blk[4];
      if (q.onFace) {
        const auto& n = kFaceNormals[q.face];
        const int cx = px + n[0], cy = py + n[1], cz = pz + n[2];
        const u8 center = lightAt(cx, cy, cz);
        if (model.ambientOcclusion && (in_.flags & kMeshSmoothLight)) {
          smoothLight(q, cx, cy, cz, center, ao, sky, blk);
        } else {
          for (int i = 0; i < 4; i++) { sky[i] = center >> 4; blk[i] = center & 15; }
        }
      } else {
        u8 own = lightAt(px, py, pz);
        // Bloques que frenan la luz sin ser cubos (escaleras, tierra de cultivo): como en 1.8, usan
        // la luz más alta de sus vecinos para no salir negros por dentro
        if (info(self).opacity > 0) {
          int s = own >> 4, bl = own & 15;
          for (const auto& n : kFaceNormals) {
            const u8 l = lightAt(px + n[0], py + n[1], pz + n[2]);
            s = std::max(s, l >> 4);
            bl = std::max(bl, l & 15);
          }
          own = static_cast<u8>((s << 4) | bl);
        }
        for (int i = 0; i < 4; i++) { sky[i] = own >> 4; blk[i] = own & 15; }
      }
      const float shade = q.shade ? kFaceShade[q.face] : 1.0f;
      const u32 color = q.tintIndex >= 0 ? tint : 0xFFFFFF;
      emit(dst, q, x, y, z, color, shade, ao, sky, blk, solidLeaves ? 0 : 255);
    }
  }

  /// Luz suave + oclusión ambiental por vértice (3 vecinos + el centro delante de la cara).
  void smoothLight(const BakedQuad& q, int cx, int cy, int cz, u8 center, float* ao, float* sky, float* blk) const {
    const int ta = kTangents[q.face][0], tb = kTangents[q.face][1];
    for (int i = 0; i < 4; i++) {
      int da[3] = {0, 0, 0}, db[3] = {0, 0, 0};
      da[ta] = q.pos[i][ta] < 0.5f ? -1 : 1;
      db[tb] = q.pos[i][tb] < 0.5f ? -1 : 1;
      const int s1x = cx + da[0], s1y = cy + da[1], s1z = cz + da[2];
      const int s2x = cx + db[0], s2y = cy + db[1], s2z = cz + db[2];
      const int crx = cx + da[0] + db[0], cry = cy + da[1] + db[1], crz = cz + da[2] + db[2];
      const bool o1 = info(at(s1x, s1y, s1z)).opaqueCube;
      const bool o2 = info(at(s2x, s2y, s2z)).opaqueCube;
      const bool oc = (o1 && o2) || info(at(crx, cry, crz)).opaqueCube;
      const u8 l1 = o1 ? center : lightAt(s1x, s1y, s1z);
      const u8 l2 = o2 ? center : lightAt(s2x, s2y, s2z);
      const u8 lc = oc ? center : lightAt(crx, cry, crz);
      // Cada vecino opaco aporta 0.2 en vez de 1 (el centro siempre es aire): 1 lado -> 0.8, rincón -> 0.4
      ao[i] = (1.0f + (o1 ? 0.2f : 1.0f) + (o2 ? 0.2f : 1.0f) + (oc ? 0.2f : 1.0f)) / 4.0f;
      sky[i] = ((center >> 4) + (l1 >> 4) + (l2 >> 4) + (lc >> 4)) / 4.0f;
      blk[i] = ((center & 15) + (l1 & 15) + (l2 & 15) + (lc & 15)) / 4.0f;
    }
  }

  /// `alpha` 0 = pintar opaco aunque la textura tenga huecos (hojas rápidas); 255 = normal.
  void emit(std::vector<ChunkVertex>& dst, const BakedQuad& q, int x, int y, int z, u32 color, float shade, const float* ao,
            const float* sky, const float* blk, u8 alpha = 255) {
    // Girar el orden de vértices para que la diagonal siga el gradiente de AO (evita artefactos)
    int start = 0;
    if (ao[0] + ao[2] < ao[1] + ao[3]) start = 1;
    const float cr = ((color >> 16) & 255) / 255.0f, cg = ((color >> 8) & 255) / 255.0f, cb = (color & 255) / 255.0f;
    for (int k = 0; k < 4; k++) {
      const int i = (start + k) & 3;
      ChunkVertex v{};
      v.x = static_cast<i16>(roundi((x + q.pos[i][0]) * 256.0f));
      v.y = static_cast<i16>(roundi((y + q.pos[i][1]) * 256.0f));
      v.z = static_cast<i16>(roundi((z + q.pos[i][2]) * 256.0f));
      v.layer = q.layer;
      v.u = static_cast<u16>(std::clamp(q.uv[i][0], 0.0f, 1.0f) * 65535.0f);
      v.v = static_cast<u16>(std::clamp(q.uv[i][1], 0.0f, 1.0f) * 65535.0f);
      const float k2 = shade * ao[i];
      v.r = static_cast<u8>(std::clamp(cr * k2 * 255.0f, 0.0f, 255.0f));
      v.g = static_cast<u8>(std::clamp(cg * k2 * 255.0f, 0.0f, 255.0f));
      v.b = static_cast<u8>(std::clamp(cb * k2 * 255.0f, 0.0f, 255.0f));
      v.a = alpha;
      v.skyLight = static_cast<u8>(roundi(sky[i] * 16.0f));
      v.blockLight = static_cast<u8>(roundi(blk[i] * 16.0f));
      dst.push_back(v);
    }
  }

  // --- Fluidos (render propio: alturas por esquina según el nivel de los vecinos) ---
  static float fluidHeight(int meta) { return meta >= 8 ? 1.0f : (8 - meta) / 9.0f; }

  bool sameFluid(BlockState a, BlockState b) const {
    const int ia = stateId(a), ib = stateId(b);
    return (isWater(ia) && isWater(ib)) || (isLava(ia) && isLava(ib));
  }

  float cornerHeight(int px, int py, int pz, int cx, int cz, BlockState self) const {
    // Media ponderada de los 4 bloques que comparten la esquina: las fuentes pesan 10, el
    // agua que fluye 1 y el aire 1 (tira de la esquina hacia abajo). Los sólidos no cuentan.
    float sum = 0, weight = 0;
    for (int dz = -1; dz <= 0; dz++)
      for (int dx = -1; dx <= 0; dx++) {
        const int x = px + cx + dx, z = pz + cz + dz;
        const BlockState s = at(x, py, z);
        if (sameFluid(s, self)) {
          if (sameFluid(at(x, py + 1, z), self)) return 1.0f;
          const float w = stateMeta(s) == 0 ? 10.0f : 1.0f;
          sum += fluidHeight(stateMeta(s)) * w;
          weight += w;
        } else if (!info(s).opaqueCube) {
          weight += 1.0f;
        }
      }
    return weight > 0 ? sum / weight : fluidHeight(stateMeta(self));
  }

  void fluid(int x, int y, int z, BlockState self) {
    const int px = x + 1, py = y + 1, pz = z + 1;
    const bool water = isWater(stateId(self));
    auto& dst = water ? out_.translucent : out_.opaque;
    const u16 still = water ? ctx_.models->waterStill : ctx_.models->lavaStill;
    const u16 flow = water ? ctx_.models->waterFlow : ctx_.models->lavaFlow;
    const bool covered = sameFluid(at(px, py + 1, pz), self);
    float h[4];  // esquinas (0,0) (0,1) (1,1) (1,0) en x,z
    if (covered) {
      h[0] = h[1] = h[2] = h[3] = 1.0f;
    } else {
      h[0] = cornerHeight(px, py, pz, 0, 0, self);
      h[1] = cornerHeight(px, py, pz, 0, 1, self);
      h[2] = cornerHeight(px, py, pz, 1, 1, self);
      h[3] = cornerHeight(px, py, pz, 1, 0, self);
    }
    const u8 own = std::max(lightAt(px, py, pz), lightAt(px, py + 1, pz));
    const float lsky[4] = {float(own >> 4), float(own >> 4), float(own >> 4), float(own >> 4)};
    const float lblk[4] = {float(own & 15), float(own & 15), float(own & 15), float(own & 15)};
    const float ao[4] = {1, 1, 1, 1};

    auto quad = [&](std::array<std::array<float, 3>, 4> pos, std::array<std::array<float, 2>, 4> uv, u16 layer, int face, bool twoSided) {
      BakedQuad q;
      q.pos = pos;
      q.uv = uv;
      q.layer = layer;
      q.face = static_cast<i8>(face);
      emit(dst, q, x, y, z, 0xFFFFFF, kFaceShade[face], ao, lsky, lblk);
      if (twoSided) {
        std::swap(q.pos[1], q.pos[3]);
        std::swap(q.uv[1], q.uv[3]);
        emit(dst, q, x, y, z, 0xFFFFFF, kFaceShade[face], ao, lsky, lblk);
      }
    };

    if (!covered && !occludes(at(px, py + 1, pz), self)) {
      quad({{{0, h[0], 0}, {0, h[1], 1}, {1, h[2], 1}, {1, h[3], 0}}}, {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}}, still, Face::Up, water);
    }
    if (!sameFluid(at(px, py - 1, pz), self) && !occludes(at(px, py - 1, pz), self)) {
      quad({{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}}, {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}}, still, Face::Down, false);
    }
    // Lados: la textura de flujo se muestrea a media escala, como en el juego
    struct Side { int face; int dx, dz; int c0, c1; std::array<float, 3> p0, p1; };
    const Side sides[4] = {
        {Face::North, 0, -1, 3, 0, {1, 0, 0}, {0, 0, 0}},
        {Face::South, 0, 1, 1, 2, {0, 0, 1}, {1, 0, 1}},
        {Face::West, -1, 0, 0, 1, {0, 0, 0}, {0, 0, 1}},
        {Face::East, 1, 0, 2, 3, {1, 0, 1}, {1, 0, 0}},
    };
    for (const Side& sd : sides) {
      const BlockState n = at(px + sd.dx, py, pz + sd.dz);
      if (sameFluid(n, self) || occludes(n, self)) continue;
      const float ha = h[sd.c0], hb = h[sd.c1];
      quad({{{sd.p0[0], ha, sd.p0[2]}, {sd.p0[0], 0, sd.p0[2]}, {sd.p1[0], 0, sd.p1[2]}, {sd.p1[0], hb, sd.p1[2]}}},
           {{{0, (1 - ha) * 0.5f}, {0, 0.5f}, {0.5f, 0.5f}, {0.5f, (1 - hb) * 0.5f}}}, flow, sd.face, water);
    }
  }

  const MeshInput& in_;
  const MesherContext& ctx_;
  MeshOutput& out_;
  Tints tints_;
};

}  // namespace

bool fillMeshInput(const World& world, int sx, int sy, int sz, MeshInput& out) {
  const Chunk* center = world.chunk(sx, sz);
  if (!center) return false;
  const Section* sec = center->section(sy);
  if (!sec || sec->nonAir == 0) return false;
  out.sx = sx;
  out.sy = sy;
  out.sz = sz;
  const int bx = sx * 16 - 1, by = sy * 16 - 1, bz = sz * 16 - 1;
  for (int dz = -1; dz <= 1; dz++)
    for (int dx = -1; dx <= 1; dx++) {
      const Chunk* c = world.chunk(sx + dx, sz + dz);
      const int x0 = dx < 0 ? 0 : (dx == 0 ? 1 : 17), x1 = dx < 0 ? 1 : (dx == 0 ? 17 : 18);
      const int z0 = dz < 0 ? 0 : (dz == 0 ? 1 : 17), z1 = dz < 0 ? 1 : (dz == 0 ? 17 : 18);
      for (int pz = z0; pz < z1; pz++)
        for (int px = x0; px < x1; px++) {
          const int lx = (bx + px) & 15, lz = (bz + pz) & 15;
          if (c) out.biomes[pz * MeshInput::P + px] = static_cast<u8>(c->biome(lx, lz));
          else out.biomes[pz * MeshInput::P + px] = static_cast<u8>(center->biome(std::clamp(px - 1, 0, 15), std::clamp(pz - 1, 0, 15)));
          for (int py = 0; py < MeshInput::P; py++) {
            const int wy = by + py;
            const int i = MeshInput::idx(px, py, pz);
            if (!c || wy < 0) { out.blocks[i] = makeState(B::stone); out.light[i] = 0; continue; }
            if (wy >= kChunkHeight) { out.blocks[i] = 0; out.light[i] = 0xF0; continue; }
            out.blocks[i] = c->block(lx, wy, lz);
            out.light[i] = c->packedLight(lx, wy, lz);
          }
        }
    }
  return true;
}

u16 computeVisibility(const MeshInput& in) {
  constexpr int N = 16;
  // 0 = tapa la vista, 1 = deja pasar (sin visitar), 2 = visitado
  std::array<u8, N * N * N> state;
  int openCells = 0;
  for (int y = 0; y < N; y++)
    for (int z = 0; z < N; z++)
      for (int x = 0; x < N; x++) {
        const bool open = !info(in.blocks[MeshInput::idx(x + 1, y + 1, z + 1)]).opaqueCube;
        state[static_cast<std::size_t>((y * N + z) * N + x)] = open ? 1 : 0;
        openCells += open ? 1 : 0;
      }
  if (openCells == N * N * N) return kVisAll;
  u16 result = 0;
  std::array<u16, N * N * N> stack;
  for (int start = 0; start < N * N * N; start++) {
    if (state[static_cast<std::size_t>(start)] != 1) continue;
    int sp = 0;
    stack[static_cast<std::size_t>(sp++)] = static_cast<u16>(start);
    state[static_cast<std::size_t>(start)] = 2;
    u8 faces = 0;  // caras del cubo que toca esta región
    while (sp > 0) {
      const int c = stack[static_cast<std::size_t>(--sp)];
      const int x = c & 15, z = (c >> 4) & 15, y = c >> 8;
      if (x == 0) faces |= 1u << Face::West;
      if (x == N - 1) faces |= 1u << Face::East;
      if (y == 0) faces |= 1u << Face::Down;
      if (y == N - 1) faces |= 1u << Face::Up;
      if (z == 0) faces |= 1u << Face::North;
      if (z == N - 1) faces |= 1u << Face::South;
      auto visit = [&](int n) {
        if (state[static_cast<std::size_t>(n)] != 1) return;
        state[static_cast<std::size_t>(n)] = 2;
        stack[static_cast<std::size_t>(sp++)] = static_cast<u16>(n);
      };
      if (x > 0) visit(c - 1);
      if (x < N - 1) visit(c + 1);
      if (z > 0) visit(c - N);
      if (z < N - 1) visit(c + N);
      if (y > 0) visit(c - N * N);
      if (y < N - 1) visit(c + N * N);
    }
    for (int a = 0; a < 6; a++)
      for (int b = a + 1; b < 6; b++)
        if ((faces & (1u << a)) && (faces & (1u << b))) result = static_cast<u16>(result | visPairBit(a, b));
    if (result == kVisAll) break;
  }
  return result;
}

MeshOutput buildMesh(const MeshInput& in, const MesherContext& ctx) {
  MeshOutput out;
  out.sx = in.sx;
  out.sy = in.sy;
  out.sz = in.sz;
  Builder(in, ctx, out).run();
  out.visibility = computeVisibility(in);
  return out;
}

std::vector<u8> encodeMeshOutput(const MeshOutput& out) {
  const i32 head[6] = {out.sx, out.sy, out.sz, static_cast<i32>(out.opaque.size()), static_cast<i32>(out.translucent.size()),
                       static_cast<i32>(out.visibility)};
  std::vector<u8> bytes(sizeof(head) + (out.opaque.size() + out.translucent.size()) * sizeof(ChunkVertex));
  u8* p = bytes.data();
  std::memcpy(p, head, sizeof(head));
  p += sizeof(head);
  if (!out.opaque.empty()) std::memcpy(p, out.opaque.data(), out.opaque.size() * sizeof(ChunkVertex));
  p += out.opaque.size() * sizeof(ChunkVertex);
  if (!out.translucent.empty()) std::memcpy(p, out.translucent.data(), out.translucent.size() * sizeof(ChunkVertex));
  return bytes;
}

bool decodeMeshOutput(const u8* data, std::size_t size, MeshOutput& out) {
  i32 head[6];
  if (size < sizeof(head)) return false;
  std::memcpy(head, data, sizeof(head));
  if (head[3] < 0 || head[4] < 0) return false;
  const std::size_t n0 = static_cast<std::size_t>(head[3]), n1 = static_cast<std::size_t>(head[4]);
  if (size != sizeof(head) + (n0 + n1) * sizeof(ChunkVertex)) return false;
  out.sx = head[0];
  out.sy = head[1];
  out.sz = head[2];
  out.visibility = static_cast<u16>(head[5] & 0x7FFF);
  const u8* p = data + sizeof(head);
  out.opaque.resize(n0);
  if (n0) std::memcpy(out.opaque.data(), p, n0 * sizeof(ChunkVertex));
  p += n0 * sizeof(ChunkVertex);
  out.translucent.resize(n1);
  if (n1) std::memcpy(out.translucent.data(), p, n1 * sizeof(ChunkVertex));
  return true;
}

}  // namespace mcw
