#pragma once
// Mundo plano para las pruebas de la partida: 3x3 chunks de piedra, tierra y hierba (suelo a y=64).
#include <memory>

#include "game/session.h"
#include "world/light.h"
#include "world/world.h"

namespace mcw::testing {

struct FlatTestWorld : WorldAccess {
  World w;
  FlatTestWorld() {
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

/// Un tick sin hacer nada (a la hora dada).
inline TickInput idleTick(double time = 6000) {
  TickInput in;
  in.worldTime = time;
  return in;
}

}  // namespace mcw::testing
