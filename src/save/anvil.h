#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/menu.h"
#include "game/mob.h"
#include "game/player.h"
#include "game/session.h"
#include "save/nbt.h"
#include "world/chunk.h"

namespace mcw::save {

// --- Chunks (formato Anvil de 1.8) ---------------------------------------------

/// Compuesto raíz de un chunk: {Level: {xPos, zPos, Sections, Biomes, HeightMap, Entities, TileEntities...}}.
nbt::Value chunkToNbt(const Chunk& chunk, i64 gameTime);
/// Chunk desde su NBT (con la luz guardada). nullptr si está mal formado.
std::unique_ptr<Chunk> chunkFromNbt(const nbt::Value& root);

// --- Objetos, criaturas y bloques con datos -----------------------------------

nbt::Value stackToNbt(const ItemStack& s, int slot = -1);
ItemStack stackFromNbt(const nbt::Value& c);
/// "minecraft:stone" <-> id numérico (acepta también ids numéricos de mundos viejos).
std::string itemName(int id);
int itemIdFromNbt(const nbt::Value& idTag);

const char* mobSaveId(MobType t);  // "Pig", "Zombie"... (ids de entidad de 1.8)
std::optional<MobType> mobTypeFromSaveId(std::string_view id);
nbt::Value mobToNbt(const Mob& m);
std::optional<Mob> mobFromNbt(const nbt::Value& c);
nbt::Value itemEntityToNbt(const ItemEntity& e);
std::optional<ItemEntity> itemEntityFromNbt(const nbt::Value& c);
nbt::Value furnaceToNbt(int x, int y, int z, const FurnaceState& f);
std::optional<std::pair<glm::ivec3, FurnaceState>> furnaceFromNbt(const nbt::Value& c);
/// Cofre (TileEntity "Chest"): 27 casillas con "Slot".
nbt::Value chestToNbt(int x, int y, int z, const ChestState& c);
std::optional<std::pair<glm::ivec3, ChestState>> chestFromNbt(const nbt::Value& c);
/// Lista "Items" con Slot (cofre de ender del jugador).
nbt::Value itemsToNbt(std::span<const ItemStack> items);
void itemsFromNbt(const nbt::Value* list, std::span<ItemStack> items);

// --- Jugador (compuesto "Player" de level.dat o playerdata/<uuid>.dat) ---------

nbt::Value playerToNbt(const Player& p, const glm::dvec3& spawn, bool hasSpawn);
void playerFromNbt(const nbt::Value& c, Player& p);

/// Posiciones y rotación como las guarda el juego (grados, yaw 0 = sur).
float yawToSave(float radians);
float yawFromSave(float degrees);

}  // namespace mcw::save
