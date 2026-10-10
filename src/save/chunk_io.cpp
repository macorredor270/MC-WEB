#include "save/chunk_io.h"

#include "save/anvil.h"
#include "save/nbt.h"

namespace mcw::save {

std::unique_ptr<Chunk> loadChunk(RegionStore& regions, ChunkPos p, GameSession& session) {
  auto data = regions.readChunk(p.x, p.z);
  if (!data) return nullptr;
  auto root = nbt::read(*data);
  if (!root) return nullptr;
  auto chunk = chunkFromNbt(*root);
  if (!chunk || chunk->pos() != p) return nullptr;
  if (const nbt::Value* lv = root->getCompound("Level")) {
    if (const nbt::Value* ents = lv->getList("Entities"))
      for (const nbt::Value& e : ents->items()) {
        if (auto m = mobFromNbt(e)) session.addMob(*m);
        else if (auto it = itemEntityFromNbt(e)) session.addItem(*it);
        else if (auto orb = xpOrbFromNbt(e)) session.addOrb(*orb);
        else if (auto cart = cartFromNbt(e)) session.addCart(cart->first, &cart->second);
      }
    if (const nbt::Value* tiles = lv->getList("TileEntities"))
      for (const nbt::Value& t : tiles->items()) {
        if (auto f = furnaceFromNbt(t)) session.setFurnace(f->first, f->second);
        else if (auto ch = chestFromNbt(t)) session.setChest(ch->first, ch->second);
        else if (auto sg = signFromNbt(t)) session.tiles().signs[{sg->first.x, sg->first.y, sg->first.z}] = sg->second;
        else if (auto bn = bannerFromNbt(t)) session.tiles().banners[{bn->first.x, bn->first.y, bn->first.z}] = bn->second;
        else if (auto sk = skullFromNbt(t)) session.tiles().skulls[{sk->first.x, sk->first.y, sk->first.z}] = sk->second;
      }
  }
  return chunk;
}

void storeChunk(RegionStore& regions, const Chunk& c, GameSession& session, bool unloading, i64 worldTime) {
  nbt::Value root = chunkToNbt(c, worldTime);
  nbt::Value& lv = *root.get("Level");
  nbt::Value& ents = *lv.get("Entities");
  for (const Mob& m : session.mobsInChunk(c.pos().x, c.pos().z, unloading)) ents.push(mobToNbt(m));
  for (const ItemEntity& e : session.itemsInChunk(c.pos().x, c.pos().z, unloading)) ents.push(itemEntityToNbt(e));
  for (const XpOrb& o : session.orbsInChunk(c.pos().x, c.pos().z, unloading)) ents.push(xpOrbToNbt(o));
  for (const auto& [cart, contents] : session.cartsInChunk(c.pos().x, c.pos().z, unloading)) ents.push(cartToNbt(cart, &contents));
  nbt::Value& tiles = *lv.get("TileEntities");
  for (const auto& [pos, f] : session.furnacesInChunk(c.pos().x, c.pos().z, unloading)) tiles.push(furnaceToNbt(pos.x, pos.y, pos.z, f));
  for (const auto& [pos, ch] : session.chestsInChunk(c.pos().x, c.pos().z, unloading))
    if (!ch.empty()) tiles.push(chestToNbt(pos.x, pos.y, pos.z, ch));
  // Carteles, estandartes y cabezas de este chunk (si se descarga, se sueltan de la memoria)
  {
    TileEntities& te = session.tiles();
    auto inChunk = [&](const TilePos& k) { return (std::get<0>(k) >> 4) == c.pos().x && (std::get<2>(k) >> 4) == c.pos().z; };
    for (auto it = te.signs.begin(); it != te.signs.end();) {
      const auto [x, y, z] = it->first;
      if (inChunk(it->first)) {
        tiles.push(signToNbt(x, y, z, it->second));
        if (unloading) { it = te.signs.erase(it); continue; }
      }
      ++it;
    }
    for (auto it = te.banners.begin(); it != te.banners.end();) {
      const auto [x, y, z] = it->first;
      if (inChunk(it->first)) {
        tiles.push(bannerToNbt(x, y, z, it->second));
        if (unloading) { it = te.banners.erase(it); continue; }
      }
      ++it;
    }
    for (auto it = te.skulls.begin(); it != te.skulls.end();) {
      const auto [x, y, z] = it->first;
      if (inChunk(it->first)) {
        tiles.push(skullToNbt(x, y, z, it->second));
        if (unloading) { it = te.skulls.erase(it); continue; }
      }
      ++it;
    }
  }
  regions.writeChunk(c.pos().x, c.pos().z, nbt::write(root));
}

}  // namespace mcw::save
