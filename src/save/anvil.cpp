#include "save/anvil.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "data/items.h"

namespace mcw::save {
namespace {

using nbt::Tag;
using nbt::Value;

constexpr float kPi = std::numbers::pi_v<float>;

Value doubleList(std::initializer_list<double> v) {
  Value l = Value::list(Tag::Double);
  for (double d : v) l.push(Value::doubleV(d));
  return l;
}
Value floatList(std::initializer_list<float> v) {
  Value l = Value::list(Tag::Float);
  for (float f : v) l.push(Value::floatV(f));
  return l;
}
glm::dvec3 readVec(const Value& c, std::string_view key) {
  const Value* l = c.getList(key);
  if (!l || l->items().size() < 3) return glm::dvec3(0);
  return {l->items()[0].asDouble(), l->items()[1].asDouble(), l->items()[2].asDouble()};
}

u8 nibble(const std::vector<u8>& a, int i) {
  const std::size_t k = static_cast<std::size_t>(i >> 1);
  if (k >= a.size()) return 0;
  return (i & 1) ? (a[k] >> 4) : (a[k] & 15);
}

}  // namespace

float yawToSave(float radians) {
  float d = 180.0f - radians * 180.0f / kPi;
  d = std::fmod(d, 360.0f);
  return d < 0 ? d + 360.0f : d;
}
float yawFromSave(float degrees) { return kPi - degrees * kPi / 180.0f; }

// --- Chunks ---------------------------------------------------------------------

nbt::Value chunkToNbt(const Chunk& chunk, i64 gameTime) {
  Value level = Value::compound();
  level.set("xPos", Value::intV(chunk.pos().x));
  level.set("zPos", Value::intV(chunk.pos().z));
  level.set("LastUpdate", Value::longV(gameTime));
  level.set("LightPopulated", Value::byte(1));
  level.set("TerrainPopulated", Value::byte(1));
  level.set("V", Value::byte(1));
  level.set("InhabitedTime", Value::longV(0));
  std::vector<u8> biomes(chunk.biomes().begin(), chunk.biomes().end());
  level.set("Biomes", Value::byteArray(std::move(biomes)));
  std::vector<i32> height(256);
  for (int i = 0; i < 256; i++) height[i] = chunk.heightMap()[i];
  level.set("HeightMap", Value::intArray(std::move(height)));

  Value sections = Value::list(Tag::Compound);
  for (int sy = 0; sy < kSectionCount; sy++) {
    const Section* s = chunk.section(sy);
    if (!s) continue;
    // Las secciones vacías con luz de cielo completa no hace falta guardarlas
    bool fullSky = true;
    if (s->nonAir == 0) {
      for (u8 l : s->light) fullSky &= l == 0xF0;
      if (fullSky) continue;
    }
    std::vector<u8> blocks(4096), data(2048), blockLight(2048), skyLight(2048), add(2048);
    bool needAdd = false;
    for (int i = 0; i < 4096; i++) {
      const BlockState st = s->blocks[i];
      const int id = stateId(st);
      blocks[i] = static_cast<u8>(id & 0xFF);
      const int hi = id >> 8;
      const int sh = (i & 1) ? 4 : 0;
      if (hi) {
        needAdd = true;
        add[i >> 1] |= static_cast<u8>((hi & 15) << sh);
      }
      data[i >> 1] |= static_cast<u8>(stateMeta(st) << sh);
      blockLight[i >> 1] |= static_cast<u8>((s->light[i] & 15) << sh);
      skyLight[i >> 1] |= static_cast<u8>((s->light[i] >> 4) << sh);
    }
    Value sec = Value::compound();
    sec.set("Y", Value::byte(static_cast<i8>(sy)));
    sec.set("Blocks", Value::byteArray(std::move(blocks)));
    if (needAdd) sec.set("Add", Value::byteArray(std::move(add)));
    sec.set("Data", Value::byteArray(std::move(data)));
    sec.set("BlockLight", Value::byteArray(std::move(blockLight)));
    sec.set("SkyLight", Value::byteArray(std::move(skyLight)));
    sections.push(std::move(sec));
  }
  level.set("Sections", std::move(sections));
  level.set("Entities", Value::list(Tag::Compound));
  level.set("TileEntities", Value::list(Tag::Compound));
  Value root = Value::compound();
  root.set("Level", std::move(level));
  return root;
}

std::unique_ptr<Chunk> chunkFromNbt(const nbt::Value& root) {
  const Value* level = root.getCompound("Level");
  if (!level || !level->has("xPos") || !level->has("zPos")) return nullptr;
  auto chunk = std::make_unique<Chunk>(level->getInt("xPos"), level->getInt("zPos"));
  if (const Value* b = level->get("Biomes"); b && b->type() == Tag::ByteArray && b->bytes().size() == 256)
    for (int i = 0; i < 256; i++) chunk->setBiome(i & 15, i >> 4, b->bytes()[i] == 255 ? 1 : b->bytes()[i]);
  if (const Value* secs = level->getList("Sections")) {
    for (const Value& sec : secs->items()) {
      const int sy = sec.getInt("Y", -1);
      if (sy < 0 || sy >= kSectionCount) continue;
      const Value* blocks = sec.get("Blocks");
      if (!blocks || blocks->bytes().size() != 4096) continue;
      const Value* dataV = sec.get("Data");
      const Value* addV = sec.get("Add");
      const Value* blV = sec.get("BlockLight");
      const Value* slV = sec.get("SkyLight");
      static const std::vector<u8> none;
      const std::vector<u8>& data = dataV ? dataV->bytes() : none;
      const std::vector<u8>& add = addV ? addV->bytes() : none;
      const std::vector<u8>& bl = blV ? blV->bytes() : none;
      const std::vector<u8>& sl = slV ? slV->bytes() : none;
      Section& s = chunk->ensureSection(sy);
      int nonAir = 0;
      for (int i = 0; i < 4096; i++) {
        int id = blocks->bytes()[i] | (nibble(add, i) << 8);
        if (id > 255) id = 0;  // en 1.8 no hay ids de bloque por encima de 255
        const BlockState st = makeState(id, nibble(data, i));
        s.blocks[i] = st;
        if (st != 0) nonAir++;
        s.light[i] = static_cast<u8>((sl.empty() ? 15 : nibble(sl, i)) << 4 | nibble(bl, i));
      }
      s.nonAir = nonAir;
    }
  }
  chunk->recomputeHeightMap();
  return chunk;
}

// --- Objetos ----------------------------------------------------------------------

std::string itemName(int id) {
  const ItemInfo& info = itemInfo(id);
  return info.exists ? "minecraft:" + std::string(info.name) : std::string("minecraft:air");
}

int itemIdFromNbt(const nbt::Value& idTag) {
  if (idTag.isNumber()) return idTag.asInt();
  if (idTag.type() != Tag::String) return 0;
  std::string_view n = idTag.asString();
  if (n.starts_with("minecraft:")) n.remove_prefix(10);
  const int id = itemIdByName(n);
  return id < 0 ? 0 : id;
}

namespace {

Value enchantList(const std::vector<std::pair<i16, i16>>& list) {
  Value l = Value::list(Tag::Compound);
  for (const auto& [id, lvl] : list) {
    Value e = Value::compound();
    e.set("id", Value::shortV(id));
    e.set("lvl", Value::shortV(lvl));
    l.push(std::move(e));
  }
  return l;
}

/// Dibujos de estandarte: lista de {Pattern: "bs", Color: 1}.
Value patternList(const std::vector<BannerPattern>& list) {
  Value l = Value::list(Tag::Compound);
  for (const BannerPattern& p : list) {
    Value e = Value::compound();
    e.set("Pattern", Value::string(p.code));
    e.set("Color", Value::intV(p.color));
    l.push(std::move(e));
  }
  return l;
}

std::vector<BannerPattern> readPatternList(const Value* l) {
  std::vector<BannerPattern> out;
  if (l)
    for (const Value& e : l->items()) {
      const std::string code = e.getString("Pattern");
      if (!bannerPatternTexture(code).empty()) out.push_back({code, static_cast<u8>(e.getInt("Color") & 15)});
    }
  return out;
}

std::vector<std::pair<i16, i16>> readEnchantList(const Value* l) {
  std::vector<std::pair<i16, i16>> out;
  if (l)
    for (const Value& e : l->items()) out.emplace_back(static_cast<i16>(e.getInt("id")), static_cast<i16>(e.getInt("lvl")));
  return out;
}

}  // namespace

nbt::Value itemTagToNbt(const ItemExtra& e) {
  Value t = Value::compound();
  if (!e.ench.empty()) t.set("ench", enchantList(e.ench));
  if (!e.stored.empty()) t.set("StoredEnchantments", enchantList(e.stored));
  if (e.repairCost != 0) t.set("RepairCost", Value::intV(e.repairCost));
  if (!e.name.empty() || !e.lore.empty() || e.color >= 0) {
    Value d = Value::compound();
    if (!e.name.empty()) d.set("Name", Value::string(e.name));
    if (!e.lore.empty()) {
      Value l = Value::list(Tag::String);
      for (const std::string& line : e.lore) l.push(Value::string(line));
      d.set("Lore", std::move(l));
    }
    if (e.color >= 0) d.set("color", Value::intV(e.color));
    t.set("display", std::move(d));
  }
  if (!e.patterns.empty()) {
    Value tag = Value::compound();
    tag.set("Patterns", patternList(e.patterns));
    t.set("BlockEntityTag", std::move(tag));
  }
  if (!e.skullOwner.empty()) t.set("SkullOwner", Value::string(e.skullOwner));
  return t;
}

std::shared_ptr<const ItemExtra> itemTagFromNbt(const nbt::Value& tag) {
  ItemExtra e;
  e.ench = readEnchantList(tag.getList("ench"));
  e.stored = readEnchantList(tag.getList("StoredEnchantments"));
  e.repairCost = tag.getInt("RepairCost");
  if (const Value* d = tag.getCompound("display")) {
    e.name = d->getString("Name");
    if (const Value* l = d->getList("Lore"))
      for (const Value& line : l->items()) e.lore.push_back(line.asString());
    e.color = d->has("color") ? d->getInt("color") : -1;
  }
  if (const Value* b = tag.getCompound("BlockEntityTag")) e.patterns = readPatternList(b->getList("Patterns"));
  if (const Value* o = tag.get("SkullOwner"); o && o->type() == Tag::String) e.skullOwner = std::string(o->asString());
  return e.empty() ? nullptr : std::make_shared<const ItemExtra>(std::move(e));
}

nbt::Value stackToNbt(const ItemStack& s, int slot) {
  Value c = Value::compound();
  if (slot >= 0) c.set("Slot", Value::byte(static_cast<i8>(slot)));
  c.set("id", Value::string(itemName(s.id)));
  c.set("Count", Value::byte(static_cast<i8>(s.count)));
  c.set("Damage", Value::shortV(s.meta));
  if (s.extra) c.set("tag", itemTagToNbt(*s.extra));
  return c;
}

ItemStack stackFromNbt(const nbt::Value& c) {
  const Value* id = c.get("id");
  if (!id) return {};
  const int n = itemIdFromNbt(*id);
  const int count = c.getInt("Count", 1);
  if (n <= 0 || count <= 0) return {};
  ItemStack s(n, count, c.getInt("Damage", 0));
  if (const Value* tag = c.getCompound("tag")) s.extra = itemTagFromNbt(*tag);
  return s;
}

// --- Criaturas ----------------------------------------------------------------------

const char* mobSaveId(MobType t) {
  switch (t) {
    case MobType::Pig: return "Pig";
    case MobType::Cow: return "Cow";
    case MobType::Sheep: return "Sheep";
    case MobType::Chicken: return "Chicken";
    case MobType::Zombie: return "Zombie";
    case MobType::Skeleton: return "Skeleton";
    case MobType::Creeper: return "Creeper";
    case MobType::Spider: return "Spider";
    case MobType::PigZombie: return "PigZombie";
    case MobType::Ghast: return "Ghast";
    case MobType::Blaze: return "Blaze";
    case MobType::MagmaCube: return "LavaSlime";
    case MobType::Slime: return "Slime";
    case MobType::Enderman: return "Enderman";
    case MobType::Silverfish: return "Silverfish";
    case MobType::CaveSpider: return "CaveSpider";
    case MobType::WitherSkeleton: return "Skeleton";  // (con SkeletonType 1)
    case MobType::EnderDragon: return "EnderDragon";
    default: return "Pig";
  }
}

std::optional<MobType> mobTypeFromSaveId(std::string_view id) {
  for (int i = 0; i < static_cast<int>(MobType::Count); i++)
    if (id == mobSaveId(static_cast<MobType>(i))) return static_cast<MobType>(i);
  return std::nullopt;
}

nbt::Value mobToNbt(const Mob& m) {
  Value c = Value::compound();
  c.set("id", Value::string(mobSaveId(m.type)));
  c.set("Pos", doubleList({m.pos.x, m.pos.y, m.pos.z}));
  c.set("Motion", doubleList({m.motion.x, m.motion.y, m.motion.z}));
  c.set("Rotation", floatList({yawToSave(m.yaw), -m.pitch * 180.0f / kPi}));
  c.set("FallDistance", Value::floatV(static_cast<float>(m.fallDistance)));
  c.set("Fire", Value::shortV(static_cast<i16>(m.fireTicks)));
  c.set("Air", Value::shortV(300));
  c.set("OnGround", Value::boolean(m.onGround));
  c.set("HealF", Value::floatV(m.health));
  c.set("Health", Value::shortV(static_cast<i16>(std::ceil(m.health))));
  c.set("HurtTime", Value::shortV(static_cast<i16>(m.hurtTime)));
  c.set("DeathTime", Value::shortV(0));
  c.set("PersistenceRequired", Value::boolean(m.persistent));
  if (m.noAI) c.set("NoAI", Value::byte(1));
  if (isBreedable(m.type)) {  // edad (negativa = cría) y modo amor, como en 1.8
    c.set("Age", Value::intV(m.growth));
    c.set("InLove", Value::intV(m.inLove));
  }
  if (m.type == MobType::Sheep) {
    c.set("Color", Value::byte(static_cast<i8>(m.woolColor)));
    c.set("Sheared", Value::boolean(m.sheared));
  }
  if (m.type == MobType::Pig) c.set("Saddle", Value::boolean(m.saddled));
  if (m.type == MobType::WitherSkeleton) c.set("SkeletonType", Value::byte(1));
  if (m.isSlimeLike()) c.set("Size", Value::intV(m.size - 1));
  if (m.type == MobType::PigZombie) c.set("Anger", Value::shortV(static_cast<i16>(m.anger)));
  if (m.type == MobType::Enderman && m.carried != 0) {
    c.set("carried", Value::shortV(static_cast<i16>(stateId(static_cast<BlockState>(m.carried)))));
    c.set("carriedData", Value::shortV(static_cast<i16>(stateMeta(static_cast<BlockState>(m.carried)))));
  }
  if (m.type == MobType::Chicken) c.set("EggLayTime", Value::intV(m.eggTimer));
  if (m.type == MobType::Creeper) {
    c.set("Fuse", Value::shortV(30));
    c.set("ExplosionRadius", Value::byte(3));
    c.set("ignited", Value::byte(0));
  }
  return c;
}

std::optional<Mob> mobFromNbt(const nbt::Value& c) {
  const auto type = mobTypeFromSaveId(c.getString("id"));
  if (!type) return std::nullopt;
  Mob m;
  m.type = *type;
  m.pos = m.prevPos = m.lastProgressPos = readVec(c, "Pos");
  m.motion = readVec(c, "Motion");
  if (const Value* r = c.getList("Rotation"); r && r->items().size() >= 2) {
    m.yaw = m.prevYaw = m.headYaw = m.prevHeadYaw = yawFromSave(static_cast<float>(r->items()[0].asDouble()));
    m.pitch = m.prevPitch = -static_cast<float>(r->items()[1].asDouble()) * kPi / 180.0f;
  }
  m.health = static_cast<float>(c.has("HealF") ? c.getDouble("HealF") : c.getDouble("Health", m.info().maxHealth));
  if (m.health <= 0) return std::nullopt;
  m.fireTicks = std::max(0, c.getInt("Fire"));
  m.onGround = c.getBool("OnGround");
  m.fallDistance = c.getDouble("FallDistance");
  m.noAI = c.getBool("NoAI");
  if (m.type == MobType::Skeleton && c.getInt("SkeletonType") == 1) m.type = MobType::WitherSkeleton;
  if (m.isSlimeLike()) m.size = static_cast<u8>(std::clamp(c.getInt("Size") + 1, 1, 4));
  if (m.type == MobType::PigZombie) m.anger = std::max(0, c.getInt("Anger"));
  if (m.type == MobType::Enderman && c.getInt("carried") > 0) m.carried = static_cast<int>(makeState(c.getInt("carried"), c.getInt("carriedData")));
  m.persistent = c.getBool("PersistenceRequired");
  if (isBreedable(m.type)) {
    m.growth = c.getInt("Age");
    m.inLove = std::max(0, c.getInt("InLove"));
  }
  m.woolColor = static_cast<u8>(c.getInt("Color") & 15);
  m.sheared = c.getBool("Sheared");
  m.eggTimer = c.getInt("EggLayTime", 6000);
  m.saddled = m.type == MobType::Pig && c.getBool("Saddle");
  return m;
}

nbt::Value xpOrbToNbt(const XpOrb& o) {
  Value c = Value::compound();
  c.set("id", Value::string("XPOrb"));
  c.set("Pos", doubleList({o.pos.x, o.pos.y, o.pos.z}));
  c.set("Motion", doubleList({o.motion.x, o.motion.y, o.motion.z}));
  c.set("Rotation", floatList({0.0f, 0.0f}));
  c.set("OnGround", Value::boolean(o.onGround));
  c.set("Value", Value::shortV(static_cast<i16>(std::min(o.value, 32767))));
  c.set("Age", Value::shortV(static_cast<i16>(std::min(o.age, 32767))));
  c.set("Health", Value::shortV(5));
  return c;
}

std::optional<XpOrb> xpOrbFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "XPOrb") return std::nullopt;
  XpOrb o;
  o.value = c.getInt("Value");
  if (o.value <= 0) return std::nullopt;
  o.pos = o.prevPos = readVec(c, "Pos");
  o.motion = readVec(c, "Motion");
  o.onGround = c.getBool("OnGround");
  o.age = c.getInt("Age");
  o.pickupDelay = 0;
  return o;
}

nbt::Value itemEntityToNbt(const ItemEntity& e) {
  Value c = Value::compound();
  c.set("id", Value::string("Item"));
  c.set("Pos", doubleList({e.pos.x, e.pos.y, e.pos.z}));
  c.set("Motion", doubleList({e.motion.x, e.motion.y, e.motion.z}));
  c.set("Rotation", floatList({0.0f, 0.0f}));
  c.set("OnGround", Value::boolean(e.onGround));
  c.set("Age", Value::shortV(static_cast<i16>(std::min(e.age, 32767))));
  c.set("PickupDelay", Value::shortV(static_cast<i16>(e.pickupDelay)));
  c.set("Health", Value::shortV(5));
  c.set("Item", stackToNbt(e.stack));
  return c;
}

std::optional<ItemEntity> itemEntityFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "Item") return std::nullopt;
  const Value* item = c.getCompound("Item");
  if (!item) return std::nullopt;
  ItemEntity e;
  e.stack = stackFromNbt(*item);
  if (e.stack.empty()) return std::nullopt;
  e.pos = e.prevPos = readVec(c, "Pos");
  e.motion = readVec(c, "Motion");
  e.onGround = c.getBool("OnGround");
  e.age = c.getInt("Age");
  e.pickupDelay = std::max(0, c.getInt("PickupDelay"));
  return e;
}

namespace {
const char* cartSaveId(CartType t) {
  switch (t) {
    case CartType::Chest: return "MinecartChest";
    case CartType::Furnace: return "MinecartFurnace";
    case CartType::Tnt: return "MinecartTNT";
    default: return "MinecartRideable";
  }
}
}  // namespace

nbt::Value cartToNbt(const Minecart& m, const ChestState* contents) {
  Value c = Value::compound();
  c.set("id", Value::string(cartSaveId(m.type)));
  c.set("Pos", doubleList({m.pos.x, m.pos.y, m.pos.z}));
  c.set("Motion", doubleList({m.motion.x, m.motion.y, m.motion.z}));
  c.set("Rotation", floatList({yawToSave(m.yaw), -m.pitch * 180.0f / kPi}));
  c.set("FallDistance", Value::floatV(static_cast<float>(m.fallDistance)));
  c.set("Fire", Value::shortV(0));
  c.set("Air", Value::shortV(300));
  c.set("OnGround", Value::boolean(m.onGround));
  c.set("Invulnerable", Value::boolean(false));
  if (m.type == CartType::Chest) c.set("Items", contents ? itemsToNbt(contents->items) : Value::list(Tag::Compound));
  if (m.type == CartType::Furnace) {
    c.set("PushX", Value::doubleV(m.push.x));
    c.set("PushZ", Value::doubleV(m.push.y));
    c.set("Fuel", Value::shortV(static_cast<i16>(std::min(m.fuel, 32767))));
  }
  if (m.type == CartType::Tnt) c.set("TNTFuse", Value::intV(m.fuse));
  return c;
}

std::optional<std::pair<Minecart, ChestState>> cartFromNbt(const nbt::Value& c) {
  const std::string id = c.getString("id");
  Minecart m;
  if (id == "MinecartRideable") m.type = CartType::Normal;
  else if (id == "MinecartChest") m.type = CartType::Chest;
  else if (id == "MinecartFurnace") m.type = CartType::Furnace;
  else if (id == "MinecartTNT") m.type = CartType::Tnt;
  else return std::nullopt;
  m.pos = m.prevPos = readVec(c, "Pos");
  m.motion = readVec(c, "Motion");
  m.onGround = c.getBool("OnGround");
  m.fallDistance = c.getDouble("FallDistance");
  m.aligned = false;  // al moverse por primera vez se alinea con el raíl en el que esté
  ChestState contents;
  if (m.type == CartType::Chest) itemsFromNbt(c.getList("Items"), contents.items);
  if (m.type == CartType::Furnace) {
    m.push = {c.getDouble("PushX"), c.getDouble("PushZ")};
    m.fuel = std::max(0, c.getInt("Fuel"));
  }
  if (m.type == CartType::Tnt) m.fuse = c.has("TNTFuse") ? c.getInt("TNTFuse") : -1;
  return std::make_pair(m, contents);
}

namespace {
Value tileBase(const char* id, int x, int y, int z) {
  Value c = Value::compound();
  c.set("id", Value::string(id));
  c.set("x", Value::intV(x));
  c.set("y", Value::intV(y));
  c.set("z", Value::intV(z));
  return c;
}
}  // namespace

nbt::Value signToNbt(int x, int y, int z, const SignText& t) {
  Value c = tileBase("Sign", x, y, z);
  for (int i = 0; i < 4; i++) c.set("Text" + std::to_string(i + 1), Value::string(signLineToJson(t.lines[static_cast<std::size_t>(i)])));
  return c;
}

std::optional<std::pair<glm::ivec3, SignText>> signFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "Sign") return std::nullopt;
  SignText t;
  for (int i = 0; i < 4; i++) t.lines[static_cast<std::size_t>(i)] = signLineFromJson(c.getString("Text" + std::to_string(i + 1)));
  return std::make_pair(glm::ivec3(c.getInt("x"), c.getInt("y"), c.getInt("z")), t);
}

nbt::Value bannerToNbt(int x, int y, int z, const BannerData& b) {
  Value c = tileBase("Banner", x, y, z);
  c.set("Base", Value::intV(b.base));
  c.set("Patterns", patternList(b.patterns));
  return c;
}

std::optional<std::pair<glm::ivec3, BannerData>> bannerFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "Banner") return std::nullopt;
  BannerData b;
  b.base = static_cast<u8>(c.getInt("Base") & 15);
  b.patterns = readPatternList(c.getList("Patterns"));
  return std::make_pair(glm::ivec3(c.getInt("x"), c.getInt("y"), c.getInt("z")), b);
}

nbt::Value skullToNbt(int x, int y, int z, const SkullData& s) {
  Value c = tileBase("Skull", x, y, z);
  c.set("SkullType", Value::byte(static_cast<i8>(s.type)));
  c.set("Rot", Value::byte(static_cast<i8>(s.rot)));
  c.set("ExtraType", Value::string(s.owner));
  return c;
}

std::optional<std::pair<glm::ivec3, SkullData>> skullFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "Skull") return std::nullopt;
  SkullData s;
  s.type = static_cast<u8>(std::clamp(c.getInt("SkullType"), 0, 4));
  s.rot = static_cast<u8>(c.getInt("Rot") & 15);
  s.owner = c.getString("ExtraType");
  return std::make_pair(glm::ivec3(c.getInt("x"), c.getInt("y"), c.getInt("z")), s);
}

nbt::Value furnaceToNbt(int x, int y, int z, const FurnaceState& f) {
  Value c = Value::compound();
  c.set("id", Value::string("Furnace"));
  c.set("x", Value::intV(x));
  c.set("y", Value::intV(y));
  c.set("z", Value::intV(z));
  c.set("BurnTime", Value::shortV(static_cast<i16>(f.burnTime)));
  c.set("CookTime", Value::shortV(static_cast<i16>(f.cookTime)));
  c.set("CookTimeTotal", Value::shortV(static_cast<i16>(FurnaceState::kCookTicks)));
  Value items = Value::list(Tag::Compound);
  if (!f.input.empty()) items.push(stackToNbt(f.input, 0));
  if (!f.fuel.empty()) items.push(stackToNbt(f.fuel, 1));
  if (!f.output.empty()) items.push(stackToNbt(f.output, 2));
  c.set("Items", std::move(items));
  return c;
}

std::optional<std::pair<glm::ivec3, FurnaceState>> furnaceFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "Furnace") return std::nullopt;
  FurnaceState f;
  f.burnTime = c.getInt("BurnTime");
  f.cookTime = c.getInt("CookTime");
  f.burnTotal = f.burnTime;
  if (const Value* items = c.getList("Items"))
    for (const Value& it : items->items()) {
      const ItemStack s = stackFromNbt(it);
      switch (it.getInt("Slot", -1)) {
        case 0: f.input = s; break;
        case 1: f.fuel = s; break;
        case 2: f.output = s; break;
        default: break;
      }
    }
  return std::make_pair(glm::ivec3(c.getInt("x"), c.getInt("y"), c.getInt("z")), f);
}

nbt::Value itemsToNbt(std::span<const ItemStack> items) {
  Value list = Value::list(Tag::Compound);
  for (std::size_t i = 0; i < items.size(); i++)
    if (!items[i].empty()) list.push(stackToNbt(items[i], static_cast<int>(i)));
  return list;
}

void itemsFromNbt(const nbt::Value* list, std::span<ItemStack> items) {
  if (!list) return;
  for (const Value& it : list->items()) {
    const int slot = it.getInt("Slot", -1);
    if (slot >= 0 && slot < static_cast<int>(items.size())) items[static_cast<std::size_t>(slot)] = stackFromNbt(it);
  }
}

nbt::Value chestToNbt(int x, int y, int z, const ChestState& ch, const char* tileId) {
  Value c = Value::compound();
  const std::string id = tileId;
  c.set("id", Value::string(id));
  c.set("x", Value::intV(x));
  c.set("y", Value::intV(y));
  c.set("z", Value::intV(z));
  if (id == "RecordPlayer") {  // el tocadiscos guarda el disco aparte
    if (!ch.items[0].empty()) {
      c.set("Record", Value::intV(ch.items[0].id));
      c.set("RecordItem", stackToNbt(ch.items[0]));
    }
  } else {
    c.set("Items", itemsToNbt(ch.items));
    if (id == "Cauldron") c.set("BrewTime", Value::shortV(static_cast<i16>(ch.brewTime)));  // (el atril de pociones se llama así en 1.8)
  }
  return c;
}

std::optional<std::pair<glm::ivec3, ChestState>> chestFromNbt(const nbt::Value& c) {
  const std::string id = c.getString("id");
  if (id != "Chest" && id != "Hopper" && id != "Trap" && id != "Dropper" && id != "RecordPlayer" && id != "Cauldron") return std::nullopt;
  ChestState ch;
  if (id == "RecordPlayer") {
    if (const Value* r = c.getCompound("RecordItem")) ch.items[0] = stackFromNbt(*r);
    else if (c.getInt("Record") > 0) ch.items[0] = ItemStack(c.getInt("Record"));
  } else {
    itemsFromNbt(c.getList("Items"), ch.items);
    ch.brewTime = std::max(0, c.getInt("BrewTime"));
  }
  return std::make_pair(glm::ivec3(c.getInt("x"), c.getInt("y"), c.getInt("z")), ch);
}

nbt::Value crystalToNbt(const glm::dvec3& pos) {
  Value c = Value::compound();
  c.set("id", Value::string("EnderCrystal"));
  c.set("Pos", doubleList({pos.x, pos.y, pos.z}));
  c.set("Motion", doubleList({0, 0, 0}));
  c.set("Rotation", floatList({0.0f, 0.0f}));
  c.set("Health", Value::shortV(5));
  return c;
}

std::optional<glm::dvec3> crystalFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "EnderCrystal") return std::nullopt;
  return readVec(c, "Pos");
}

nbt::Value spawnerToNbt(int x, int y, int z, int entityId, int delay) {
  Value c = Value::compound();
  c.set("id", Value::string("MobSpawner"));
  c.set("x", Value::intV(x));
  c.set("y", Value::intV(y));
  c.set("z", Value::intV(z));
  const auto type = mobFromEntityId(entityId);
  c.set("EntityId", Value::string(type ? mobSaveId(*type) : "Pig"));
  c.set("Delay", Value::shortV(static_cast<i16>(delay)));
  return c;
}

std::optional<std::tuple<glm::ivec3, int, int>> spawnerFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "MobSpawner") return std::nullopt;
  const auto type = mobTypeFromSaveId(c.getString("EntityId"));
  return std::make_tuple(glm::ivec3(c.getInt("x"), c.getInt("y"), c.getInt("z")), type ? mobEntityId(*type) : 90, c.getInt("Delay", 20));
}

nbt::Value noteToNbt(int x, int y, int z, int note, bool powered) {
  Value c = Value::compound();
  c.set("id", Value::string("Music"));
  c.set("x", Value::intV(x));
  c.set("y", Value::intV(y));
  c.set("z", Value::intV(z));
  c.set("note", Value::byte(static_cast<i8>(note)));
  c.set("powered", Value::boolean(powered));
  return c;
}

std::optional<std::tuple<glm::ivec3, int, bool>> noteFromNbt(const nbt::Value& c) {
  if (c.getString("id") != "Music") return std::nullopt;
  return std::make_tuple(glm::ivec3(c.getInt("x"), c.getInt("y"), c.getInt("z")), std::clamp(c.getInt("note"), 0, 24), c.getBool("powered"));
}

// --- Jugador ---------------------------------------------------------------------------

nbt::Value playerToNbt(const Player& p, const glm::dvec3& spawn, bool hasSpawn, int dimension) {
  Value c = Value::compound();
  c.set("Pos", doubleList({p.pos.x, p.pos.y, p.pos.z}));
  c.set("Motion", doubleList({p.motion.x, p.motion.y, p.motion.z}));
  c.set("Rotation", floatList({yawToSave(p.yaw), -p.pitch * 180.0f / kPi}));
  c.set("FallDistance", Value::floatV(static_cast<float>(p.fallDistance)));
  c.set("OnGround", Value::boolean(p.onGround));
  c.set("Dimension", Value::intV(dimension));
  c.set("Air", Value::shortV(static_cast<i16>(p.air)));
  c.set("HealF", Value::floatV(p.health));
  c.set("Health", Value::shortV(static_cast<i16>(std::ceil(p.health))));
  c.set("foodLevel", Value::intV(p.food));
  c.set("foodSaturationLevel", Value::floatV(p.saturation));
  c.set("foodExhaustionLevel", Value::floatV(p.exhaustion));
  c.set("foodTickTimer", Value::intV(p.foodTimer));
  c.set("playerGameType", Value::intV(p.creative() ? 1 : 0));
  c.set("SelectedItemSlot", Value::intV(p.inventory.selectedIndex()));
  c.set("XpLevel", Value::intV(p.xpLevel));
  c.set("XpP", Value::floatV(p.xpProgress));
  c.set("XpTotal", Value::intV(p.xpTotal));
  c.set("XpSeed", Value::intV(p.xpSeed));
  Value abilities = Value::compound();
  abilities.set("flying", Value::boolean(p.flying));
  abilities.set("mayfly", Value::boolean(p.creative()));
  abilities.set("instabuild", Value::boolean(p.creative()));
  abilities.set("invulnerable", Value::boolean(p.creative()));
  abilities.set("mayBuild", Value::byte(1));
  abilities.set("flySpeed", Value::floatV(0.05f));
  abilities.set("walkSpeed", Value::floatV(0.1f));
  c.set("abilities", std::move(abilities));
  Value inv = Value::list(Tag::Compound);
  for (int i = 0; i < PlayerInventory::kSize; i++)
    if (!p.inventory.slot(i).empty()) inv.push(stackToNbt(p.inventory.slot(i), i));
  for (int i = 0; i < 4; i++)  // armadura: casillas 100 (botas) a 103 (casco), como en 1.8
    if (!p.inventory.armor(i).empty()) inv.push(stackToNbt(p.inventory.armor(i), 100 + i));
  c.set("Inventory", std::move(inv));
  c.set("EnderItems", itemsToNbt(p.enderItems));
  if (hasSpawn) {
    c.set("SpawnX", Value::intV(static_cast<i32>(std::floor(spawn.x))));
    c.set("SpawnY", Value::intV(static_cast<i32>(std::floor(spawn.y))));
    c.set("SpawnZ", Value::intV(static_cast<i32>(std::floor(spawn.z))));
  }
  return c;
}

void playerFromNbt(const nbt::Value& c, Player& p) {
  p.pos = p.prevPos = readVec(c, "Pos");
  p.motion = readVec(c, "Motion");
  if (const Value* r = c.getList("Rotation"); r && r->items().size() >= 2) {
    p.yaw = yawFromSave(static_cast<float>(r->items()[0].asDouble()));
    p.pitch = -static_cast<float>(r->items()[1].asDouble()) * kPi / 180.0f;
  }
  p.fallDistance = c.getDouble("FallDistance");
  p.onGround = c.getBool("OnGround");
  p.air = c.getInt("Air", 300);
  p.health = static_cast<float>(c.has("HealF") ? c.getDouble("HealF") : c.getDouble("Health", Player::kMaxHealth));
  p.dead = p.health <= 0;
  p.food = c.getInt("foodLevel", 20);
  p.saturation = static_cast<float>(c.getDouble("foodSaturationLevel", 5));
  p.exhaustion = static_cast<float>(c.getDouble("foodExhaustionLevel", 0));
  p.foodTimer = c.getInt("foodTickTimer");
  p.mode = c.getInt("playerGameType") == 1 ? GameMode::Creative : GameMode::Survival;
  p.xpLevel = std::max(0, c.getInt("XpLevel"));
  p.xpProgress = std::clamp(static_cast<float>(c.getDouble("XpP")), 0.0f, 1.0f);
  p.xpTotal = std::max(0, c.getInt("XpTotal"));
  if (c.has("XpSeed")) p.xpSeed = c.getInt("XpSeed");
  if (const Value* a = c.getCompound("abilities")) p.flying = a->getBool("flying") && p.creative();
  p.inventory.clear();
  p.enderItems = {};
  itemsFromNbt(c.getList("EnderItems"), p.enderItems);
  if (const Value* inv = c.getList("Inventory"))
    for (const Value& it : inv->items()) {
      const int slot = it.getInt("Slot", -1);
      if (slot >= 0 && slot < PlayerInventory::kSize) p.inventory.slot(slot) = stackFromNbt(it);
      else if (slot >= 100 && slot < 104) p.inventory.armor(slot - 100) = stackFromNbt(it);
    }
  p.inventory.select(c.getInt("SelectedItemSlot"));
}

}  // namespace mcw::save
