#include "world/light.h"

#include <algorithm>

#include "core/face.h"
#include "world/world.h"

namespace mcw {
namespace {

inline int opacityOf(BlockState s) { return blockInfo(stateId(s)).opacity; }

}  // namespace

namespace light {

void computeInitial(Chunk& c) {
  c.recomputeHeightMap();
  const int topY = (c.topSection() + 1) * 16;
  struct N { u8 x, z; u16 y; };
  std::vector<N> queue;
  queue.reserve(8192);

  // Cielo: 15 por encima del heightmap, 0 por debajo (la propagación rellena lo demás).
  for (int z = 0; z < 16; z++)
    for (int x = 0; x < 16; x++) {
      const int h = c.height(x, z);
      for (int y = 0; y < h; y++) c.setSkyLight(x, y, z, 0);
      // Semillas: celdas iluminadas que tocan zonas más bajas que el heightmap de alguna vecina.
      int maxN = h;
      for (int f = Face::North; f <= Face::East; f++) {
        const int nx = x + kFaceNormals[f][0], nz = z + kFaceNormals[f][2];
        if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16) maxN = std::max(maxN, c.height(nx, nz));
      }
      for (int y = h; y <= std::min(maxN, topY); y++)
        if (y < kChunkHeight) queue.push_back({static_cast<u8>(x), static_cast<u8>(z), static_cast<u16>(y)});
    }

  auto run = [&](bool sky) {
    for (std::size_t head = 0; head < queue.size(); head++) {
      const N n = queue[head];
      const int level = sky ? c.skyLight(n.x, n.y, n.z) : c.blockLight(n.x, n.y, n.z);
      if (level <= 1) continue;
      for (int f = 0; f < 6; f++) {
        const int nx = n.x + kFaceNormals[f][0], ny = n.y + kFaceNormals[f][1], nz = n.z + kFaceNormals[f][2];
        if (nx < 0 || nx >= 16 || nz < 0 || nz >= 16 || ny < 0 || ny >= kChunkHeight) continue;
        const int op = opacityOf(c.block(nx, ny, nz));
        if (op >= 15) continue;
        int nl = level - std::max(1, op);
        if (sky && f == Face::Down && level == 15 && op == 0) nl = 15;
        const int cur = sky ? c.skyLight(nx, ny, nz) : c.blockLight(nx, ny, nz);
        if (nl <= cur) continue;
        if (sky) c.setSkyLight(nx, ny, nz, nl);
        else c.setBlockLight(nx, ny, nz, nl);
        queue.push_back({static_cast<u8>(nx), static_cast<u8>(nz), static_cast<u16>(ny)});
      }
    }
  };
  run(true);

  // Bloques que emiten luz (lava, antorchas, glowstone...)
  queue.clear();
  for (int i = 0; i <= c.topSection(); i++) {
    const Section* s = c.section(i);
    if (!s || s->nonAir == 0) continue;
    for (int idx = 0; idx < 4096; idx++) {
      const int emit = blockInfo(stateId(s->blocks[idx])).emitLight;
      if (emit == 0) continue;
      const int x = idx & 15, z = (idx >> 4) & 15, y = i * 16 + (idx >> 8);
      c.setBlockLight(x, y, z, emit);
      queue.push_back({static_cast<u8>(x), static_cast<u8>(z), static_cast<u16>(y)});
    }
  }
  run(false);
}

}  // namespace light

void LightEngine::propagateIncrease(bool sky, std::vector<Node>& queue, ChunkSet& modified) {
  for (std::size_t head = 0; head < queue.size(); head++) {
    const Node n = queue[head];
    const int level = sky ? world_.skyLight(n.x, n.y, n.z) : world_.blockLight(n.x, n.y, n.z);
    if (level <= 1) continue;
    for (int f = 0; f < 6; f++) {
      const int nx = n.x + kFaceNormals[f][0], ny = n.y + kFaceNormals[f][1], nz = n.z + kFaceNormals[f][2];
      if (ny < 0 || ny >= kChunkHeight) continue;
      Chunk* ch = world_.chunkAt(nx, nz);
      if (!ch) continue;
      const int lx = nx & 15, lz = nz & 15;
      const int op = opacityOf(ch->block(lx, ny, lz));
      if (op >= 15) continue;
      int nl = level - std::max(1, op);
      if (sky && f == Face::Down && level == 15 && op == 0) nl = 15;
      const int cur = sky ? ch->skyLight(lx, ny, lz) : ch->blockLight(lx, ny, lz);
      if (nl <= cur) continue;
      if (sky) ch->setSkyLight(lx, ny, lz, nl);
      else ch->setBlockLight(lx, ny, lz, nl);
      modified.insert(ch->pos());
      queue.push_back({nx, ny, nz, nl});
    }
  }
}

void LightEngine::propagateDecrease(bool sky, std::vector<Node>& queue, std::vector<Node>& relight, ChunkSet& modified) {
  for (std::size_t head = 0; head < queue.size(); head++) {
    const Node n = queue[head];
    for (int f = 0; f < 6; f++) {
      const int nx = n.x + kFaceNormals[f][0], ny = n.y + kFaceNormals[f][1], nz = n.z + kFaceNormals[f][2];
      if (ny < 0 || ny >= kChunkHeight) continue;
      Chunk* ch = world_.chunkAt(nx, nz);
      if (!ch) continue;
      const int lx = nx & 15, lz = nz & 15;
      const int cur = sky ? ch->skyLight(lx, ny, lz) : ch->blockLight(lx, ny, lz);
      if (cur == 0) continue;
      const bool fedByUs = cur < n.level || (sky && f == Face::Down && n.level == 15 && cur == 15);
      if (fedByUs) {
        if (sky) ch->setSkyLight(lx, ny, lz, 0);
        else ch->setBlockLight(lx, ny, lz, 0);
        modified.insert(ch->pos());
        queue.push_back({nx, ny, nz, cur});
        // Si el propio bloque emite luz, vuelve a encenderse después
        if (!sky) {
          const int emit = blockInfo(stateId(ch->block(lx, ny, lz))).emitLight;
          if (emit > 0) {
            ch->setBlockLight(lx, ny, lz, emit);
            relight.push_back({nx, ny, nz, emit});
          }
        }
      } else {
        relight.push_back({nx, ny, nz, cur});
      }
    }
  }
}

void LightEngine::stitch(ChunkPos pos, ChunkSet& modified) {
  Chunk* c = world_.chunk(pos.x, pos.z);
  if (!c) return;
  for (bool sky : {true, false}) {
    std::vector<Node> queue;
    for (int f = Face::North; f <= Face::East; f++) {
      Chunk* n = world_.chunk(pos.x + kFaceNormals[f][0], pos.z + kFaceNormals[f][2]);
      if (!n) continue;
      const int top = std::max(c->topSection(), n->topSection()) * 16 + 16;
      for (int i = 0; i < 16; i++) {
        // (ax,az) en este chunk y (bx,bz) en el vecino, pegados al borde
        int ax, az, bx, bz;
        switch (f) {
          case Face::North: ax = i; az = 0; bx = i; bz = 15; break;
          case Face::South: ax = i; az = 15; bx = i; bz = 0; break;
          case Face::West: ax = 0; az = i; bx = 15; bz = i; break;
          default: ax = 15; az = i; bx = 0; bz = i; break;
        }
        for (int y = 0; y < std::min(top, kChunkHeight); y++) {
          const int la = sky ? c->skyLight(ax, y, az) : c->blockLight(ax, y, az);
          const int lb = sky ? n->skyLight(bx, y, bz) : n->blockLight(bx, y, bz);
          if (la == lb) continue;
          // Encolamos la celda más brillante; la BFS se encarga de propagar al otro lado.
          if (la > lb) queue.push_back({pos.x * 16 + ax, y, pos.z * 16 + az, la});
          else queue.push_back({n->pos().x * 16 + bx, y, n->pos().z * 16 + bz, lb});
        }
      }
    }
    propagateIncrease(sky, queue, modified);
  }
}

void LightEngine::blockChanged(int x, int y, int z, ChunkSet& modified) {
  Chunk* c = world_.chunkAt(x, z);
  if (!c || y < 0 || y >= kChunkHeight) return;
  const int lx = x & 15, lz = z & 15;
  const BlockState s = c->block(lx, y, lz);
  const int op = opacityOf(s);
  modified.insert(c->pos());

  for (bool sky : {true, false}) {
    std::vector<Node> dec, relight;
    const int old = sky ? c->skyLight(lx, y, lz) : c->blockLight(lx, y, lz);
    // Apagar la luz que dependía de esta celda
    if (old > 0) {
      if (sky) c->setSkyLight(lx, y, lz, 0);
      else c->setBlockLight(lx, y, lz, 0);
      dec.push_back({x, y, z, old});
      propagateDecrease(sky, dec, relight, modified);
    }
    // Volver a encender desde los vecinos iluminados y desde la propia celda
    int own = 0;
    if (sky) {
      // Luz directa del cielo si no hay nada opaco encima
      bool open = op == 0;
      for (int yy = y + 1; open && yy < kChunkHeight; yy++)
        if (opacityOf(c->block(lx, yy, lz)) > 0) open = false;
      if (open) own = 15;
    } else {
      own = blockInfo(stateId(s)).emitLight;
    }
    if (op < 15) {
      for (int f = 0; f < 6; f++) {
        const int nx = x + kFaceNormals[f][0], ny = y + kFaceNormals[f][1], nz = z + kFaceNormals[f][2];
        if (ny < 0 || ny >= kChunkHeight) continue;
        const int nl = sky ? world_.skyLight(nx, ny, nz) : world_.blockLight(nx, ny, nz);
        int candidate = nl - std::max(1, op);
        if (sky && f == Face::Up && nl == 15 && op == 0) candidate = 15;
        own = std::max(own, candidate);
      }
    }
    if (own > 0) {
      if (sky) c->setSkyLight(lx, y, lz, own);
      else c->setBlockLight(lx, y, lz, own);
      relight.push_back({x, y, z, own});
    }
    propagateIncrease(sky, relight, modified);
  }
}

}  // namespace mcw
