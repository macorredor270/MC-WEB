#include "save/nbt.h"

#include <bit>
#include <cstring>

#include "core/buffer.h"
#include "core/zip.h"

namespace mcw::nbt {

Value& Value::push(Value v) {
  items_.push_back(std::move(v));
  return items_.back();
}

Value& Value::set(std::string_view key, Value v) {
  for (std::size_t i = 0; i < keys_.size(); i++)
    if (keys_[i] == key) return items_[i] = std::move(v);
  keys_.emplace_back(key);
  items_.push_back(std::move(v));
  return items_.back();
}

const Value* Value::get(std::string_view key) const {
  for (std::size_t i = 0; i < keys_.size(); i++)
    if (keys_[i] == key) return &items_[i];
  return nullptr;
}

Value* Value::get(std::string_view key) {
  for (std::size_t i = 0; i < keys_.size(); i++)
    if (keys_[i] == key) return &items_[i];
  return nullptr;
}

void Value::remove(std::string_view key) {
  for (std::size_t i = 0; i < keys_.size(); i++)
    if (keys_[i] == key) {
      keys_.erase(keys_.begin() + static_cast<std::ptrdiff_t>(i));
      items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
}

i64 Value::getLong(std::string_view key, i64 def) const {
  const Value* v = get(key);
  return v && v->isNumber() ? v->asLong() : def;
}

double Value::getDouble(std::string_view key, double def) const {
  const Value* v = get(key);
  return v && v->isNumber() ? v->asDouble() : def;
}

std::string Value::getString(std::string_view key, std::string_view def) const {
  const Value* v = get(key);
  return v && v->type() == Tag::String ? v->asString() : std::string(def);
}

const Value* Value::getCompound(std::string_view key) const {
  const Value* v = get(key);
  return v && v->type() == Tag::Compound ? v : nullptr;
}

const Value* Value::getList(std::string_view key) const {
  const Value* v = get(key);
  return v && v->type() == Tag::List ? v : nullptr;
}

namespace {

// Las cadenas NBT son "UTF-8 modificado" con longitud u16; para nuestro uso basta UTF-8 normal.
void writeString(BufferWriter& w, std::string_view s) {
  const std::size_t n = std::min<std::size_t>(s.size(), 65535);
  w.u16(static_cast<u16>(n));
  w.bytes({reinterpret_cast<const u8*>(s.data()), n});
}

void writePayload(BufferWriter& w, const Value& v) {
  switch (v.type()) {
    case Tag::End: break;
    case Tag::Byte: w.i8(static_cast<i8>(v.asLong())); break;
    case Tag::Short: w.i16(static_cast<i16>(v.asLong())); break;
    case Tag::Int: w.i32(static_cast<i32>(v.asLong())); break;
    case Tag::Long: w.i64(v.asLong()); break;
    case Tag::Float: w.f32(static_cast<float>(v.asDouble())); break;
    case Tag::Double: w.f64(v.asDouble()); break;
    case Tag::ByteArray:
      w.i32(static_cast<i32>(v.bytes().size()));
      w.bytes(v.bytes());
      break;
    case Tag::String: writeString(w, v.asString()); break;
    case Tag::List: {
      // Una lista vacía se escribe como lista de "End", como hace el juego
      const Tag t = v.items().empty() && v.listType() == Tag::End ? Tag::End : v.listType();
      w.u8(static_cast<u8>(t));
      w.i32(static_cast<i32>(v.items().size()));
      for (const Value& it : v.items()) writePayload(w, it);
      break;
    }
    case Tag::Compound:
      for (std::size_t i = 0; i < v.keys().size(); i++) {
        w.u8(static_cast<u8>(v.values()[i].type()));
        writeString(w, v.keys()[i]);
        writePayload(w, v.values()[i]);
      }
      w.u8(0);
      break;
    case Tag::IntArray:
      w.i32(static_cast<i32>(v.ints().size()));
      for (i32 x : v.ints()) w.i32(x);
      break;
  }
}

std::string readString(BufferReader& r) {
  const u16 n = r.u16();
  auto b = r.bytes(n);
  return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

Value readPayload(BufferReader& r, Tag t, int depth) {
  if (depth > 512) throw DecodeError("NBT demasiado anidado");
  switch (t) {
    case Tag::End: return Value(Tag::End);
    case Tag::Byte: return Value::byte(r.i8());
    case Tag::Short: return Value::shortV(r.i16());
    case Tag::Int: return Value::intV(r.i32());
    case Tag::Long: return Value::longV(r.i64());
    case Tag::Float: return Value::floatV(r.f32());
    case Tag::Double: return Value::doubleV(r.f64());
    case Tag::ByteArray: {
      const i32 n = r.i32();
      if (n < 0) throw DecodeError("NBT: tamaño negativo");
      auto b = r.bytes(static_cast<std::size_t>(n));
      return Value::byteArray(std::vector<u8>(b.begin(), b.end()));
    }
    case Tag::String: return Value::string(readString(r));
    case Tag::List: {
      const Tag et = static_cast<Tag>(r.u8());
      const i32 n = r.i32();
      if (n < 0 || static_cast<u8>(et) > static_cast<u8>(Tag::IntArray)) throw DecodeError("NBT: lista inválida");
      Value l = Value::list(et);
      l.items().reserve(static_cast<std::size_t>(std::min(n, 65536)));
      for (i32 i = 0; i < n; i++) l.push(readPayload(r, et, depth + 1));
      return l;
    }
    case Tag::Compound: {
      Value c = Value::compound();
      for (;;) {
        const Tag ct = static_cast<Tag>(r.u8());
        if (ct == Tag::End) break;
        if (static_cast<u8>(ct) > static_cast<u8>(Tag::IntArray)) throw DecodeError("NBT: tipo desconocido");
        std::string key = readString(r);
        c.set(key, readPayload(r, ct, depth + 1));
      }
      return c;
    }
    case Tag::IntArray: {
      const i32 n = r.i32();
      if (n < 0) throw DecodeError("NBT: tamaño negativo");
      std::vector<i32> v(static_cast<std::size_t>(n));
      for (auto& x : v) x = r.i32();
      return Value::intArray(std::move(v));
    }
  }
  throw DecodeError("NBT: tipo desconocido");
}

}  // namespace

std::vector<u8> write(const Value& root, std::string_view rootName) {
  BufferWriter w;
  w.u8(static_cast<u8>(Tag::Compound));
  writeString(w, rootName);
  writePayload(w, root);
  return w.take();
}

std::optional<Value> read(std::span<const u8> data, std::string* rootName) {
  std::optional<std::vector<u8>> inflated;
  if (isGzip(data.data(), data.size())) inflated = gzipDecompress(data.data(), data.size());
  else if (data.size() >= 2 && data[0] == 0x78) inflated = zlibDecompress(data.data(), data.size());
  if (inflated) data = *inflated;
  try {
    BufferReader r(data);
    if (static_cast<Tag>(r.u8()) != Tag::Compound) return std::nullopt;
    std::string name = readString(r);
    if (rootName) *rootName = std::move(name);
    return readPayload(r, Tag::Compound, 0);
  } catch (const DecodeError&) {
    return std::nullopt;
  }
}

}  // namespace mcw::nbt
