// MD5 según RFC 1321 (implementación propia).
#include "core/hash.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace mcw {
namespace {

constexpr u32 kShift[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                            14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                            4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

const std::array<u32, 64>& constants() {
  static const std::array<u32, 64> k = [] {
    std::array<u32, 64> t{};
    for (int i = 0; i < 64; i++) t[i] = static_cast<u32>(std::floor(std::fabs(std::sin(i + 1.0)) * 4294967296.0));
    return t;
  }();
  return k;
}

u32 rotl(u32 x, u32 c) { return (x << c) | (x >> (32 - c)); }

}  // namespace

std::array<u8, 16> md5(const void* data, std::size_t size) {
  const auto& K = constants();
  u32 a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  std::vector<u8> msg(static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
  const u64 bits = static_cast<u64>(size) * 8;
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) msg.push_back(0);
  for (int i = 0; i < 8; i++) msg.push_back(static_cast<u8>(bits >> (8 * i)));
  for (std::size_t off = 0; off < msg.size(); off += 64) {
    u32 M[16];
    for (int i = 0; i < 16; i++)
      M[i] = msg[off + i * 4] | (msg[off + i * 4 + 1] << 8) | (msg[off + i * 4 + 2] << 16) | (static_cast<u32>(msg[off + i * 4 + 3]) << 24);
    u32 A = a0, B = b0, C = c0, D = d0;
    for (u32 i = 0; i < 64; i++) {
      u32 F, g;
      if (i < 16) { F = (B & C) | (~B & D); g = i; }
      else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
      else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
      else { F = C ^ (B | ~D); g = (7 * i) % 16; }
      F = F + A + K[i] + M[g];
      A = D;
      D = C;
      C = B;
      B = B + rotl(F, kShift[i]);
    }
    a0 += A;
    b0 += B;
    c0 += C;
    d0 += D;
  }
  std::array<u8, 16> out{};
  const u32 words[4] = {a0, b0, c0, d0};
  for (int w = 0; w < 4; w++)
    for (int i = 0; i < 4; i++) out[w * 4 + i] = static_cast<u8>(words[w] >> (8 * i));
  return out;
}

std::string uuidToString(const std::array<u8, 16>& b) {
  static const char* hex = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) s += '-';
    s += hex[b[i] >> 4];
    s += hex[b[i] & 15];
  }
  return s;
}

std::string offlineUuid(std::string_view name) {
  auto b = md5("OfflinePlayer:" + std::string(name));
  b[6] = static_cast<u8>((b[6] & 0x0f) | 0x30);  // versión 3
  b[8] = static_cast<u8>((b[8] & 0x3f) | 0x80);  // variante IETF
  return uuidToString(b);
}

}  // namespace mcw
