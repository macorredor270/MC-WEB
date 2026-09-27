#pragma once
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

namespace mcw {

/// Error de lectura de datos binarios mal formados (paquetes, NBT...).
struct DecodeError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

/// Escritura big-endian con los tipos del protocolo de Minecraft (VarInt, strings...).
class BufferWriter {
 public:
  BufferWriter& u8(mcw::u8 v) { data_.push_back(v); return *this; }
  BufferWriter& i8(mcw::i8 v) { return u8(static_cast<mcw::u8>(v)); }
  BufferWriter& boolean(bool v) { return u8(v ? 1 : 0); }
  BufferWriter& u16(mcw::u16 v);
  BufferWriter& i16(mcw::i16 v) { return u16(static_cast<mcw::u16>(v)); }
  BufferWriter& i32(mcw::i32 v);
  BufferWriter& i64(mcw::i64 v);
  BufferWriter& f32(float v);
  BufferWriter& f64(double v);
  BufferWriter& varInt(mcw::i32 v);
  BufferWriter& varLong(mcw::i64 v);
  BufferWriter& bytes(std::span<const mcw::u8> b) { data_.insert(data_.end(), b.begin(), b.end()); return *this; }
  BufferWriter& string(std::string_view s);

  const std::vector<mcw::u8>& data() const { return data_; }
  std::vector<mcw::u8> take() { return std::move(data_); }
  std::size_t size() const { return data_.size(); }

 private:
  std::vector<mcw::u8> data_;
};

/// Lectura big-endian; lanza DecodeError si los datos se acaban o son inválidos.
class BufferReader {
 public:
  explicit BufferReader(std::span<const mcw::u8> data) : data_(data) {}

  mcw::u8 u8();
  mcw::i8 i8() { return static_cast<mcw::i8>(u8()); }
  bool boolean() { return u8() != 0; }
  mcw::u16 u16();
  mcw::i16 i16() { return static_cast<mcw::i16>(u16()); }
  mcw::i32 i32();
  mcw::i64 i64();
  float f32();
  double f64();
  mcw::i32 varInt();
  mcw::i64 varLong();
  std::span<const mcw::u8> bytes(std::size_t n);
  std::string string(std::size_t maxChars = 32767);

  std::size_t remaining() const { return data_.size() - pos_; }
  std::size_t position() const { return pos_; }

 private:
  void need(std::size_t n) const;
  std::span<const mcw::u8> data_;
  std::size_t pos_ = 0;
};

int varIntSize(i32 v);

}  // namespace mcw
