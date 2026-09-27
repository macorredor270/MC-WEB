#include "core/buffer.h"

#include <bit>
#include <cstring>
#include <format>

namespace mcw {

BufferWriter& BufferWriter::u16(mcw::u16 v) {
  data_.push_back(static_cast<mcw::u8>(v >> 8));
  data_.push_back(static_cast<mcw::u8>(v));
  return *this;
}

BufferWriter& BufferWriter::i32(mcw::i32 v) {
  const auto x = static_cast<u32>(v);
  for (int s = 24; s >= 0; s -= 8) data_.push_back(static_cast<mcw::u8>(x >> s));
  return *this;
}

BufferWriter& BufferWriter::i64(mcw::i64 v) {
  const auto x = static_cast<u64>(v);
  for (int s = 56; s >= 0; s -= 8) data_.push_back(static_cast<mcw::u8>(x >> s));
  return *this;
}

BufferWriter& BufferWriter::f32(float v) { return i32(std::bit_cast<mcw::i32>(v)); }
BufferWriter& BufferWriter::f64(double v) { return i64(std::bit_cast<mcw::i64>(v)); }

BufferWriter& BufferWriter::varInt(mcw::i32 v) {
  auto x = static_cast<u32>(v);
  while (x >= 0x80) {
    data_.push_back(static_cast<mcw::u8>((x & 0x7f) | 0x80));
    x >>= 7;
  }
  data_.push_back(static_cast<mcw::u8>(x));
  return *this;
}

BufferWriter& BufferWriter::varLong(mcw::i64 v) {
  auto x = static_cast<u64>(v);
  while (x >= 0x80) {
    data_.push_back(static_cast<mcw::u8>((x & 0x7f) | 0x80));
    x >>= 7;
  }
  data_.push_back(static_cast<mcw::u8>(x));
  return *this;
}

BufferWriter& BufferWriter::string(std::string_view s) {
  varInt(static_cast<mcw::i32>(s.size()));
  data_.insert(data_.end(), s.begin(), s.end());
  return *this;
}

void BufferReader::need(std::size_t n) const {
  if (pos_ + n > data_.size())
    throw DecodeError(std::format("lectura fuera de rango (+{} en {}/{})", n, pos_, data_.size()));
}

mcw::u8 BufferReader::u8() {
  need(1);
  return data_[pos_++];
}

mcw::u16 BufferReader::u16() {
  need(2);
  const mcw::u16 v = static_cast<mcw::u16>((data_[pos_] << 8) | data_[pos_ + 1]);
  pos_ += 2;
  return v;
}

mcw::i32 BufferReader::i32() {
  need(4);
  u32 v = 0;
  for (int i = 0; i < 4; i++) v = (v << 8) | data_[pos_ + i];
  pos_ += 4;
  return static_cast<mcw::i32>(v);
}

mcw::i64 BufferReader::i64() {
  need(8);
  u64 v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | data_[pos_ + i];
  pos_ += 8;
  return static_cast<mcw::i64>(v);
}

float BufferReader::f32() { return std::bit_cast<float>(i32()); }
double BufferReader::f64() { return std::bit_cast<double>(i64()); }

mcw::i32 BufferReader::varInt() {
  u32 result = 0;
  for (int shift = 0;; shift += 7) {
    if (shift >= 35) throw DecodeError("VarInt demasiado largo");
    const mcw::u8 b = u8();
    result |= static_cast<u32>(b & 0x7f) << shift;
    if (!(b & 0x80)) break;
  }
  return static_cast<mcw::i32>(result);
}

mcw::i64 BufferReader::varLong() {
  u64 result = 0;
  for (int shift = 0;; shift += 7) {
    if (shift >= 70) throw DecodeError("VarLong demasiado largo");
    const mcw::u8 b = u8();
    result |= static_cast<u64>(b & 0x7f) << shift;
    if (!(b & 0x80)) break;
  }
  return static_cast<mcw::i64>(result);
}

std::span<const mcw::u8> BufferReader::bytes(std::size_t n) {
  need(n);
  auto s = data_.subspan(pos_, n);
  pos_ += n;
  return s;
}

std::string BufferReader::string(std::size_t maxChars) {
  const mcw::i32 n = varInt();
  if (n < 0 || static_cast<std::size_t>(n) > maxChars * 4) throw DecodeError(std::format("string demasiado larga ({})", n));
  auto b = bytes(static_cast<std::size_t>(n));
  return {reinterpret_cast<const char*>(b.data()), b.size()};
}

int varIntSize(mcw::i32 v) {
  auto x = static_cast<u32>(v);
  int n = 1;
  while (x >= 0x80) { x >>= 7; n++; }
  return n;
}

}  // namespace mcw
