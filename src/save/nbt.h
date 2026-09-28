#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/buffer.h"
#include "core/types.h"

namespace mcw::nbt {

/// Tipos de etiqueta NBT (formato público de los datos de Minecraft).
enum class Tag : u8 { End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray };

/// Un valor NBT. Los compuestos guardan sus claves en orden de inserción.
class Value {
 public:
  Value() = default;
  explicit Value(Tag t) : type_(t) {}

  static Value byte(i8 v) { Value x(Tag::Byte); x.i_ = v; return x; }
  static Value boolean(bool v) { return byte(v ? 1 : 0); }
  static Value shortV(i16 v) { Value x(Tag::Short); x.i_ = v; return x; }
  static Value intV(i32 v) { Value x(Tag::Int); x.i_ = v; return x; }
  static Value longV(i64 v) { Value x(Tag::Long); x.i_ = v; return x; }
  static Value floatV(float v) { Value x(Tag::Float); x.d_ = v; return x; }
  static Value doubleV(double v) { Value x(Tag::Double); x.d_ = v; return x; }
  static Value string(std::string v) { Value x(Tag::String); x.s_ = std::move(v); return x; }
  static Value byteArray(std::vector<u8> v) { Value x(Tag::ByteArray); x.bytes_ = std::move(v); return x; }
  static Value intArray(std::vector<i32> v) { Value x(Tag::IntArray); x.ints_ = std::move(v); return x; }
  static Value list(Tag elementType) { Value x(Tag::List); x.listType_ = elementType; return x; }
  static Value compound() { return Value(Tag::Compound); }

  Tag type() const { return type_; }
  bool isNumber() const { return type_ >= Tag::Byte && type_ <= Tag::Double; }

  // Números (convierten entre tipos numéricos)
  i64 asLong() const { return (type_ == Tag::Float || type_ == Tag::Double) ? static_cast<i64>(d_) : i_; }
  i32 asInt() const { return static_cast<i32>(asLong()); }
  double asDouble() const { return (type_ == Tag::Float || type_ == Tag::Double) ? d_ : static_cast<double>(i_); }
  const std::string& asString() const { return s_; }
  const std::vector<u8>& bytes() const { return bytes_; }
  std::vector<u8>& bytes() { return bytes_; }
  const std::vector<i32>& ints() const { return ints_; }

  // Listas
  Tag listType() const { return listType_; }
  const std::vector<Value>& items() const { return items_; }
  std::vector<Value>& items() { return items_; }
  Value& push(Value v);

  // Compuestos
  const std::vector<std::string>& keys() const { return keys_; }
  const std::vector<Value>& values() const { return items_; }
  Value& set(std::string_view key, Value v);
  const Value* get(std::string_view key) const;
  Value* get(std::string_view key);
  bool has(std::string_view key) const { return get(key) != nullptr; }
  void remove(std::string_view key);
  /// Atajos con valor por defecto si falta la clave o es de otro tipo.
  i64 getLong(std::string_view key, i64 def = 0) const;
  i32 getInt(std::string_view key, i32 def = 0) const { return static_cast<i32>(getLong(key, def)); }
  double getDouble(std::string_view key, double def = 0) const;
  bool getBool(std::string_view key, bool def = false) const { return getLong(key, def ? 1 : 0) != 0; }
  std::string getString(std::string_view key, std::string_view def = {}) const;
  const Value* getCompound(std::string_view key) const;
  const Value* getList(std::string_view key) const;

 private:
  Tag type_ = Tag::End;
  i64 i_ = 0;
  double d_ = 0;
  std::string s_;
  std::vector<u8> bytes_;
  std::vector<i32> ints_;
  Tag listType_ = Tag::End;
  std::vector<Value> items_;       // elementos de la lista o valores del compuesto
  std::vector<std::string> keys_;  // claves del compuesto (mismo índice que items_)
};

/// Escribe un compuesto raíz con nombre (sin comprimir).
std::vector<u8> write(const Value& root, std::string_view rootName = "");
/// Lee un compuesto raíz. Acepta datos sin comprimir, gzip o zlib.
std::optional<Value> read(std::span<const u8> data, std::string* rootName = nullptr);
/// Lee un compuesto con nombre de un flujo (NBT dentro de un paquete). Si el primer byte es 0
/// (TAG_End) no hay NBT y devuelve nullopt. Lanza DecodeError si está mal formado.
std::optional<Value> readFrom(BufferReader& r);

}  // namespace mcw::nbt
