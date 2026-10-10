// Redstone de 1.8: fuentes (antorchas, palancas, botones, placas, bloque de redstone, raíl detector,
// gancho de cable trampa), polvo que pierde 1 de potencia por bloque, bloques sólidos que conducen
// (carga fuerte o débil), repetidores, comparadores, antorchas que se apagan, y mecanismos:
// lámparas, puertas, trampillas, puertas de valla, pistones, TNT y raíles propulsores.
// Comportamiento descrito en minecraft.wiki ("Redstone mechanics"); implementación propia.
#include <algorithm>
#include <cmath>
#include <deque>

#include "core/face.h"
#include "data/blockstates.h"
#include "game/rails.h"
#include "game/rules.h"
#include "game/session.h"
#include "world/world.h"

namespace mcw {
namespace {

constexpr int kHDX[4] = {0, -1, 0, 1};  // índice horizontal de 1.8: 0 sur, 1 oeste, 2 norte, 3 este
constexpr int kHDZ[4] = {1, 0, -1, 0};

glm::ivec3 dirOf(int face) { return {kFaceNormals[face][0], kFaceNormals[face][1], kFaceNormals[face][2]}; }

glm::ivec3 facing6(int m) {
  static const glm::ivec3 d[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
  return d[std::clamp(m & 7, 0, 5)];
}

bool isDoor(int id) { return id == 64 || id == 71 || (id >= 193 && id <= 197); }
bool isGate(int id) { return id == 107 || (id >= 183 && id <= 187); }
bool isTrap(int id) { return id == 96 || id == 167; }
bool isPlate(int id) { return id == 70 || id == 72 || id == 147 || id == 148; }

/// Bloque que conduce la carga: sólido y opaco, pero no una fuente por sí mismo.
bool isConductor(int id) { return blockInfo(id).opaqueCube && id != 152 && id != 29 && id != 33; }

/// Bloques de redstone que hay que revisar cuando cambia algo cerca.
bool isRedstoneThing(int id) {
  switch (id) {
    case 55: case 75: case 76: case 93: case 94: case 149: case 150: case 123: case 124: case 46: case 29: case 33:
    case 27: case 157: return true;
    default: return isDoor(id) || isGate(id) || isTrap(id);
  }
}

struct WorldNeighbors {
  const World* w;
  glm::ivec3 p;
  static BlockState get(const void* ctx, int dx, int dy, int dz) {
    const auto* n = static_cast<const WorldNeighbors*>(ctx);
    return n->w->block(n->p.x + dx, n->p.y + dy, n->p.z + dz);
  }
};

}  // namespace

void GameSession::schedule(const glm::ivec3& p, int ticks, TickKind kind) {
  for (const Scheduled& s : scheduled_)
    if (s.pos == p && s.kind == kind) return;  // ya pendiente
  scheduled_.push_back({p, ticks, kind});
}

int GameSession::emittedPower(const glm::ivec3& from, const glm::ivec3& dir, bool strongOnly) {
  World& w = access_.world();
  const BlockState s = w.block(from.x, from.y, from.z);
  const int id = stateId(s), meta = stateMeta(s);
  switch (id) {
    case 152: return strongOnly ? 0 : 15;  // bloque de redstone: débil en todas direcciones
    case 76: {  // antorcha encendida: fuerte hacia arriba, débil a los lados; nada hacia su apoyo
      if (dir == supportOffset(s)) return 0;
      if (strongOnly) return dir == glm::ivec3(0, 1, 0) ? 15 : 0;
      return 15;
    }
    case 69: case 77: case 143: case 131:  // palanca, botones y gancho: fuerte a su apoyo
      if (!(meta & 8)) return 0;
      if (strongOnly) return dir == supportOffset(s) ? 15 : 0;
      return 15;
    case 70: case 72: case 28:  // placas y raíl detector: fuerte hacia abajo
      if (!(meta & (id == 28 ? 8 : 1))) return 0;
      if (strongOnly) return dir == glm::ivec3(0, -1, 0) ? 15 : 0;
      return 15;
    case 147: case 148:
      if (strongOnly) return dir == glm::ivec3(0, -1, 0) ? meta : 0;
      return meta;
    case 151: case 178: return strongOnly ? 0 : meta;
    case 94: case 150: {  // repetidor y comparador activados: solo por delante
      const int f = meta & 3;
      const glm::ivec3 out(-kHDX[f], 0, -kHDZ[f]);
      return dir == out ? 15 : 0;
    }
    case 55: {  // polvo: débil hacia abajo y hacia donde apunta
      if (strongOnly || meta == 0) return 0;
      if (dir == glm::ivec3(0, -1, 0)) return meta;
      if (dir.y != 0) return 0;
      const WorldNeighbors n{&w, from};
      const int ext = neighborBits(s, &n, &WorldNeighbors::get);
      const int c[4] = {ext % 3, (ext / 3) % 3, (ext / 9) % 3, (ext / 27) % 3};  // N, E, S, O
      const glm::ivec3 dirs[4] = {{0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}};
      const int count = (c[0] > 0) + (c[1] > 0) + (c[2] > 0) + (c[3] > 0);
      for (int d = 0; d < 4; d++) {
        if (dirs[d] != dir) continue;
        if (count == 0) return meta;
        if (c[d] > 0) return meta;
        // Una sola conexión (o una línea): también sale por el lado contrario
        const bool nsOnly = !c[1] && !c[3], ewOnly = !c[0] && !c[2];
        if ((d == 0 || d == 2) && nsOnly && (c[0] || c[2])) return meta;
        if ((d == 1 || d == 3) && ewOnly && (c[1] || c[3])) return meta;
        return 0;
      }
      return 0;
    }
    default: return 0;
  }
}

int GameSession::conductorPower(const glm::ivec3& c, bool strongOnly) {
  World& w = access_.world();
  if (!isConductor(stateId(w.block(c.x, c.y, c.z)))) return 0;
  int best = 0;
  for (int f = 0; f < 6; f++) {
    const glm::ivec3 d = dirOf(f);
    // El bloque vecino da potencia "hacia" c, es decir en dirección -d desde él
    best = std::max(best, emittedPower(c + d, -d, true));
    if (!strongOnly) best = std::max(best, emittedPower(c + d, -d, false) * (stateId(w.block(c.x + d.x, c.y + d.y, c.z + d.z)) == 55 ? 1 : 0));
  }
  return best;
}

int GameSession::powerInto(const glm::ivec3& p, int skip) {
  World& w = access_.world();
  int best = 0;
  for (int f = 0; f < 6; f++) {
    if (f == skip) continue;
    const glm::ivec3 d = dirOf(f), n = p + d;
    best = std::max(best, emittedPower(n, -d, false));
    if (isConductor(stateId(w.block(n.x, n.y, n.z)))) best = std::max(best, conductorPower(n, false));
    if (best >= 15) break;
  }
  return best;
}

void GameSession::updateWireNetwork(const glm::ivec3& start, std::vector<glm::ivec3>& changed) {
  World& w = access_.world();
  // Todos los trozos de polvo conectados (también en diagonal por escalones)
  std::vector<glm::ivec3> nodes;
  std::set<std::tuple<int, int, int>> seen;
  std::deque<glm::ivec3> q{start};
  seen.insert({start.x, start.y, start.z});
  auto wireAt = [&](const glm::ivec3& p) { return stateId(w.block(p.x, p.y, p.z)) == 55; };
  auto links = [&](const glm::ivec3& p) {
    std::vector<glm::ivec3> out;
    for (int f = 2; f < 6; f++) {
      const glm::ivec3 d = dirOf(f), side = p + d;
      if (wireAt(side)) out.push_back(side);
      const bool sideSolid = blockInfo(stateId(w.block(side.x, side.y, side.z))).opaqueCube;
      const bool aboveSolid = blockInfo(stateId(w.block(p.x, p.y + 1, p.z))).opaqueCube;
      if (!aboveSolid && sideSolid && wireAt(side + glm::ivec3(0, 1, 0))) out.push_back(side + glm::ivec3(0, 1, 0));
      const glm::ivec3 down = side - glm::ivec3(0, 1, 0);
      if (!sideSolid && wireAt(down)) out.push_back(down);
    }
    return out;
  };
  while (!q.empty() && nodes.size() < 4096) {
    const glm::ivec3 p = q.front();
    q.pop_front();
    if (!wireAt(p)) continue;
    nodes.push_back(p);
    for (const glm::ivec3& n : links(p))
      if (seen.insert({n.x, n.y, n.z}).second) q.push_back(n);
  }
  if (nodes.empty()) return;
  // Entrada de cada trozo desde fuera de la red (fuentes, repetidores, bloques con carga fuerte)
  std::map<std::tuple<int, int, int>, int> power;
  std::vector<std::pair<int, glm::ivec3>> order;
  for (const glm::ivec3& p : nodes) {
    int in = 0;
    for (int f = 0; f < 6; f++) {
      const glm::ivec3 d = dirOf(f), n = p + d;
      if (wireAt(n)) continue;
      in = std::max(in, emittedPower(n, -d, false));
      if (isConductor(stateId(w.block(n.x, n.y, n.z)))) in = std::max(in, conductorPower(n, true));
    }
    power[{p.x, p.y, p.z}] = in;
    if (in > 0) order.push_back({in, p});
  }
  // Propagar: cada paso pierde 1 (de mayor a menor, como un Dijkstra)
  std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::deque<glm::ivec3> work;
  for (const auto& [pw, p] : order) work.push_back(p);
  for (int guard = 0; guard < 200000 && !work.empty(); guard++) {
    const glm::ivec3 p = work.front();
    work.pop_front();
    const int pw = power[{p.x, p.y, p.z}];
    if (pw <= 1) continue;
    for (const glm::ivec3& n : links(p)) {
      auto it = power.find({n.x, n.y, n.z});
      if (it == power.end() || it->second >= pw - 1) continue;
      it->second = pw - 1;
      work.push_back(n);
    }
  }
  for (const glm::ivec3& p : nodes) {
    const int pw = power[{p.x, p.y, p.z}];
    if (stateMeta(w.block(p.x, p.y, p.z)) != pw) {
      setWorldBlock(p.x, p.y, p.z, makeState(55, pw));
      changed.push_back(p);
    }
  }
}

void GameSession::redstoneUpdate(const glm::ivec3& p, std::vector<glm::ivec3>& changed) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s), meta = stateMeta(s);
  auto set = [&](BlockState ns) {
    if (ns == s) return;
    setWorldBlock(p.x, p.y, p.z, ns);
    changed.push_back(p);
  };
  switch (id) {
    case 55: updateWireNetwork(p, changed); return;
    case 123: if (powerInto(p) > 0) set(makeState(124)); return;
    case 124: if (powerInto(p) == 0) schedule(p, 4, TickKind::Lamp); return;
    case 75: case 76: {  // la antorcha se apaga si su bloque tiene carga
      const glm::ivec3 o = supportOffset(s);
      const bool powered = conductorPower(p + o, false) > 0 || emittedPower(p + o, -o, false) > 0;
      if (powered == (id == 76)) schedule(p, 2, TickKind::Torch);
      return;
    }
    case 93: case 94: {
      const int f = meta & 3;
      const glm::ivec3 back(kHDX[f], 0, kHDZ[f]);
      const glm::ivec3 b = p + back;
      const bool in = emittedPower(b, -back, false) > 0 || conductorPower(b, false) > 0;
      if (in != (id == 94)) schedule(p, 2 * ((meta >> 2) + 1), TickKind::Repeater);
      return;
    }
    case 149: case 150: {
      const int f = meta & 3;
      const glm::ivec3 back(kHDX[f], 0, kHDZ[f]);
      const glm::ivec3 b = p + back;
      int rear = std::max(emittedPower(b, -back, false), conductorPower(b, false));
      const glm::ivec3 l(back.z, 0, -back.x), r = -l;
      const int side = std::max(emittedPower(p + l, -l, false), emittedPower(p + r, -r, false));
      const int out = (meta & 4) ? std::max(0, rear - side) : (rear >= side ? rear : 0);
      if ((out > 0) != (id == 150)) schedule(p, 2, TickKind::Comparator);
      return;
    }
    case 46:  // TNT: se enciende con redstone
      if (powerInto(p) > 0) {
        setWorldBlock(p.x, p.y, p.z, 0);
        changed.push_back(p);
        schedule(p, 80, TickKind::Tnt);
      }
      return;
    case 29: case 33: {  // pistón: potencia por cualquier lado menos el de delante
      const glm::ivec3 front = facing6(meta);
      int frontFace = -1;
      for (int f = 0; f < 6; f++)
        if (dirOf(f) == front) frontFace = f;
      const bool powered = powerInto(p, frontFace) > 0;
      if (powered != ((meta & 8) != 0) && pistonMove(p, powered)) changed.push_back(p);
      return;
    }
    case 27: case 157: {  // raíles propulsor y activador: se encienden con potencia propia o la de la fila (hasta 8 más allá)
      const bool powered =
          powerInto(p) > 0 || rails::poweredByChain(w, p, [&](const glm::ivec3& q) { return powerInto(q) > 0; });
      if (powered != ((meta & 8) != 0)) set(makeState(id, (meta & 7) | (powered ? 8 : 0)));
      return;
    }
    default: break;
  }
  if (isDoor(id)) {
    const bool upper = meta & 8;
    const glm::ivec3 lo = upper ? p - glm::ivec3(0, 1, 0) : p, hi = lo + glm::ivec3(0, 1, 0);
    const BlockState ls = w.block(lo.x, lo.y, lo.z), hs = w.block(hi.x, hi.y, hi.z);
    if (stateId(ls) != id || stateId(hs) != id) return;
    const bool powered = powerInto(lo) > 0 || powerInto(hi) > 0;
    const bool was = stateMeta(hs) & 2;
    if (powered == was) return;
    setWorldBlock(hi.x, hi.y, hi.z, makeState(id, (stateMeta(hs) & ~2) | (powered ? 2 : 0)));
    const int lm = (stateMeta(ls) & ~4) | (powered ? 4 : 0);
    if (lm != stateMeta(ls)) {
      setWorldBlock(lo.x, lo.y, lo.z, makeState(id, lm));
      events_.push_back({powered ? SessionEvent::Type::DoorOpened : SessionEvent::Type::DoorClosed, lo, ls});
    }
    return;
  }
  if (isGate(id)) {
    const bool powered = powerInto(p) > 0;
    if (powered == ((meta & 8) != 0)) return;
    set(makeState(id, (meta & 3) | (powered ? 12 : 0)));
    events_.push_back({powered ? SessionEvent::Type::DoorOpened : SessionEvent::Type::DoorClosed, p, s});
    return;
  }
  if (isTrap(id)) {
    const bool powered = powerInto(p) > 0;
    const auto key = std::make_tuple(p.x, p.y, p.z);
    const bool was = poweredTrapdoors_.count(key) > 0;
    if (powered == was) return;
    if (powered) poweredTrapdoors_.insert(key);
    else poweredTrapdoors_.erase(key);
    set(makeState(id, (meta & ~4) | (powered ? 4 : 0)));
    events_.push_back({powered ? SessionEvent::Type::DoorOpened : SessionEvent::Type::DoorClosed, p, s});
  }
}

void GameSession::redstoneNotify(const glm::ivec3& origin) {
  // Revisa los mecanismos a distancia 2 de cada cambio (alcanza a lo que hay al otro lado de un
  // bloque sólido y al polvo en escalón); lo que cambia avisa a su vez
  if (inRedstone_) {
    pendingRedstone_.push_back(origin);
    return;
  }
  inRedstone_ = true;
  World& w = access_.world();
  std::deque<glm::ivec3> todo{origin};
  for (const glm::ivec3& p : pendingRedstone_) todo.push_back(p);
  pendingRedstone_.clear();
  for (int guard = 0; guard < 2000 && !todo.empty(); guard++) {
    const glm::ivec3 o = todo.front();
    todo.pop_front();
    std::vector<glm::ivec3> changed;
    std::set<std::tuple<int, int, int>> wiresDone;
    for (int dy = -2; dy <= 2; dy++)
      for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++) {
          if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 2) continue;
          const glm::ivec3 p = o + glm::ivec3(dx, dy, dz);
          if (p.y < 0 || p.y >= kChunkHeight) continue;
          const int id = stateId(w.block(p.x, p.y, p.z));
          if (!isRedstoneThing(id)) continue;
          if (id == 55 && wiresDone.count({p.x, p.y, p.z})) continue;
          const std::size_t before = changed.size();
          redstoneUpdate(p, changed);
          if (id == 55) wiresDone.insert({p.x, p.y, p.z});
          (void)before;
        }
    for (const glm::ivec3& c : changed) todo.push_back(c);
    for (const glm::ivec3& p : pendingRedstone_) todo.push_back(p);
    pendingRedstone_.clear();
  }
  inRedstone_ = false;
}

bool GameSession::pistonMove(const glm::ivec3& p, bool extend) {
  World& w = access_.world();
  const BlockState s = w.block(p.x, p.y, p.z);
  const int id = stateId(s), meta = stateMeta(s);
  const bool sticky = id == 29;
  const glm::ivec3 d = facing6(meta);
  auto immovable = [&](int bid, int bmeta) {
    switch (bid) {
      case B::obsidian: case B::bedrock: case 34: case 36: case 52: case 54: case 61: case 62: case 23: case 158: case 116:
      case 117: case 118: case 119: case 120: case 130: case 138: case 146: case 154: case 145: case 90: case 63: case 68:
      case 176: case 177: case 144: case 166: case 137: case 25: case 84:
        return true;
      case 29: case 33: return (bmeta & 8) != 0;
      default: return false;
    }
  };
  auto breaksWhenPushed = [&](int bid) {
    return bid != B::air && (collisionBoxes(bid, 0).empty() || isReplaceable(makeState(bid, 0))) && !isFluid(bid);
  };
  if (extend) {
    std::vector<glm::ivec3> line;
    glm::ivec3 c = p + d;
    for (int i = 0; i <= 12; i++, c += d) {
      if (c.y < 0 || c.y >= kChunkHeight) return false;
      const BlockState b = w.block(c.x, c.y, c.z);
      const int bid = stateId(b);
      if (bid == B::air || isFluid(bid)) break;
      if (breaksWhenPushed(bid)) {
        breakBlock(c, false);
        break;
      }
      if (immovable(bid, stateMeta(b)) || i == 12) return false;
      line.push_back(c);
    }
    for (auto it = line.rbegin(); it != line.rend(); ++it) {
      const BlockState b = w.block(it->x, it->y, it->z);
      setWorldBlock(it->x + d.x, it->y + d.y, it->z + d.z, b);
    }
    setWorldBlock(p.x, p.y, p.z, makeState(id, meta | 8));
    setWorldBlock(p.x + d.x, p.y + d.y, p.z + d.z, makeState(34, (meta & 7) | (sticky ? 8 : 0)));
    events_.push_back({SessionEvent::Type::DoorOpened, p, s});
    return true;
  }
  // Recoger: quitar la cabeza y, si es pegajoso, traer el bloque de delante
  const glm::ivec3 head = p + d;
  if (stateId(w.block(head.x, head.y, head.z)) == 34) setWorldBlock(head.x, head.y, head.z, 0);
  setWorldBlock(p.x, p.y, p.z, makeState(id, meta & 7));
  if (sticky) {
    const glm::ivec3 far = head + d;
    const BlockState b = w.block(far.x, far.y, far.z);
    const int bid = stateId(b);
    if (bid != B::air && !isFluid(bid) && !immovable(bid, stateMeta(b)) && !breaksWhenPushed(bid)) {
      setWorldBlock(head.x, head.y, head.z, b);
      setWorldBlock(far.x, far.y, far.z, 0);
    }
  }
  events_.push_back({SessionEvent::Type::DoorClosed, p, s});
  return true;
}

void GameSession::tickScheduled() {
  World& w = access_.world();
  std::vector<Scheduled> due;
  for (auto& t : scheduled_)
    if (--t.ticks <= 0) due.push_back(t);
  std::erase_if(scheduled_, [](const Scheduled& t) { return t.ticks <= 0; });
  for (const Scheduled& t : due) {
    const glm::ivec3 p = t.pos;
    const BlockState s = w.block(p.x, p.y, p.z);
    const int id = stateId(s), meta = stateMeta(s);
    switch (t.kind) {
      case TickKind::ButtonRelease:
        if ((id == 77 || id == 143) && (meta & 8)) {
          setWorldBlock(p.x, p.y, p.z, makeState(id, meta & 7));
          events_.push_back({SessionEvent::Type::Click, p, s});
          redstoneNotify(p);
          redstoneNotify(p + supportOffset(s));
        }
        break;
      case TickKind::Torch: {
        if (id != 75 && id != 76) break;
        const glm::ivec3 o = supportOffset(s);
        const bool powered = conductorPower(p + o, false) > 0 || emittedPower(p + o, -o, false) > 0;
        const int want = powered ? 75 : 76;
        if (want != id) {
          setWorldBlock(p.x, p.y, p.z, makeState(want, meta));
          redstoneNotify(p);
          redstoneNotify(p + glm::ivec3(0, 1, 0));
        }
        break;
      }
      case TickKind::Repeater: {
        if (id != 93 && id != 94) break;
        const int f = meta & 3;
        const glm::ivec3 back(kHDX[f], 0, kHDZ[f]);
        const bool in = emittedPower(p + back, -back, false) > 0 || conductorPower(p + back, false) > 0;
        const int want = in ? 94 : 93;
        if (want != id) {
          setWorldBlock(p.x, p.y, p.z, makeState(want, meta));
          redstoneNotify(p);
          redstoneNotify(p - back);
        }
        break;
      }
      case TickKind::Comparator: {
        if (id != 149 && id != 150) break;
        std::vector<glm::ivec3> tmp;
        const int f = meta & 3;
        const glm::ivec3 back(kHDX[f], 0, kHDZ[f]);
        const int rear = std::max(emittedPower(p + back, -back, false), conductorPower(p + back, false));
        const glm::ivec3 l(back.z, 0, -back.x), r = -l;
        const int side = std::max(emittedPower(p + l, -l, false), emittedPower(p + r, -r, false));
        const int out = (meta & 4) ? std::max(0, rear - side) : (rear >= side ? rear : 0);
        const int want = out > 0 ? 150 : 149;
        if (want != id) {
          setWorldBlock(p.x, p.y, p.z, makeState(want, (meta & 7) | (out > 0 ? 8 : 0)));
          redstoneNotify(p);
          redstoneNotify(p - back);
        }
        break;
      }
      case TickKind::Lamp:
        if (id == 124 && powerInto(p) == 0) setWorldBlock(p.x, p.y, p.z, makeState(123));
        break;
      case TickKind::Tnt:
        explode(glm::dvec3(p) + 0.5, 4.0f);
        break;
      case TickKind::Detector: {
        if (id != rails::kDetector || !(meta & 8)) break;
        bool cart = false;
        for (const Minecart& c : carts_) {
          glm::ivec3 r;
          if (!c.dead && rails::railAt(w, c.pos.x, c.pos.y, c.pos.z, r) && r == p) cart = true;
        }
        if (cart) {
          schedule(p, 20, TickKind::Detector);
        } else {
          setWorldBlock(p.x, p.y, p.z, makeState(id, meta & 7));
          events_.push_back({SessionEvent::Type::Click, p, s});
          redstoneNotify(p);
          redstoneNotify(p - glm::ivec3(0, 1, 0));
        }
        break;
      }
    }
  }
}

void GameSession::tickPlates() {
  // Placas de presión: pulsadas mientras haya alguien encima (las de madera también con objetos)
  World& w = access_.world();
  std::map<std::tuple<int, int, int>, int> now;
  auto feet = [&](const glm::dvec3& pos, bool item) {
    const int x = static_cast<int>(std::floor(pos.x)), y = static_cast<int>(std::floor(pos.y + 0.01)), z = static_cast<int>(std::floor(pos.z));
    const int id = stateId(w.block(x, y, z));
    if (!isPlate(id) || (item && id == 70)) return;
    now[{x, y, z}]++;
  };
  if (!player_.dead && !player_.flying) feet(player_.pos, false);
  for (const Mob& m : mobs_) feet(m.pos, false);
  for (const ItemEntity& e : items_) feet(e.pos, true);
  auto apply = [&](const std::tuple<int, int, int>& k, int count) {
    const auto [x, y, z] = k;
    const BlockState s = w.block(x, y, z);
    const int id = stateId(s);
    if (!isPlate(id)) return;
    int meta = 0;
    if (id == 70 || id == 72) meta = count > 0 ? 1 : 0;
    else if (id == 147) meta = std::min(15, count);
    else meta = std::min(15, (count + 9) / 10);
    if (meta == stateMeta(s)) return;
    setWorldBlock(x, y, z, makeState(id, meta));
    events_.push_back({SessionEvent::Type::Click, {x, y, z}, s});
    redstoneNotify({x, y, z});
    redstoneNotify({x, y - 1, z});
  };
  for (const auto& k : pressedPlates_)
    if (!now.count(k)) apply(k, 0);
  pressedPlates_.clear();
  for (const auto& [k, n] : now) {
    apply(k, n);
    pressedPlates_.insert(k);
  }
}

}  // namespace mcw
