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
/// Etiqueta de un objeto (el compuesto "tag"): ench, StoredEnchantments, display {Name, Lore, color} y RepairCost.
nbt::Value itemTagToNbt(const ItemExtra& e);
/// Lo contrario; nulo si la etiqueta no trae nada que conozcamos.
std::shared_ptr<const ItemExtra> itemTagFromNbt(const nbt::Value& tag);
/// "minecraft:stone" <-> id numérico (acepta también ids numéricos de mundos viejos).
std::string itemName(int id);
int itemIdFromNbt(const nbt::Value& idTag);

const char* mobSaveId(MobType t);  // "Pig", "Zombie"... (ids de entidad de 1.8)
std::optional<MobType> mobTypeFromSaveId(std::string_view id);
nbt::Value mobToNbt(const Mob& m);
std::optional<Mob> mobFromNbt(const nbt::Value& c);
nbt::Value itemEntityToNbt(const ItemEntity& e);
std::optional<ItemEntity> itemEntityFromNbt(const nbt::Value& c);
nbt::Value xpOrbToNbt(const XpOrb& o);
std::optional<XpOrb> xpOrbFromNbt(const nbt::Value& c);
/// Vagonetas con los ids de entidad de 1.8 (MinecartRideable, MinecartChest, MinecartFurnace, MinecartTNT); la de cofre lleva
/// "Items", la de horno "PushX", "PushZ" y "Fuel", la de dinamita "TNTFuse".
nbt::Value cartToNbt(const Minecart& c, const ChestState* contents);
std::optional<std::pair<Minecart, ChestState>> cartFromNbt(const nbt::Value& c);
nbt::Value furnaceToNbt(int x, int y, int z, const FurnaceState& f);
std::optional<std::pair<glm::ivec3, FurnaceState>> furnaceFromNbt(const nbt::Value& c);
/// Cofre (TileEntity "Chest"): 27 casillas con "Slot".
nbt::Value chestToNbt(int x, int y, int z, const ChestState& c);
std::optional<std::pair<glm::ivec3, ChestState>> chestFromNbt(const nbt::Value& c);
/// Cartel (TileEntity "Sign"): Text1..Text4 como texto de chat en JSON.
nbt::Value signToNbt(int x, int y, int z, const SignText& t);
std::optional<std::pair<glm::ivec3, SignText>> signFromNbt(const nbt::Value& c);
/// Estandarte ("Banner": Base y Patterns) y cabeza ("Skull": SkullType, Rot, ExtraType).
nbt::Value bannerToNbt(int x, int y, int z, const BannerData& b);
std::optional<std::pair<glm::ivec3, BannerData>> bannerFromNbt(const nbt::Value& c);
nbt::Value skullToNbt(int x, int y, int z, const SkullData& s);
std::optional<std::pair<glm::ivec3, SkullData>> skullFromNbt(const nbt::Value& c);
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
