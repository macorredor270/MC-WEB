#include "net/websocket.h"

#include <algorithm>
#include <cctype>

#include "core/buffer.h"
#include "core/hash.h"
#include "net/client.h"

namespace mcw::net {

namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

}  // namespace

std::string websocketAccept(std::string_view key) {
  const auto digest = sha1(std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  return base64Encode(std::string_view(reinterpret_cast<const char*>(digest.data()), digest.size()));
}

std::optional<UpgradeRequest> parseUpgradeRequest(std::string_view request) {
  const auto lineEnd = request.find("\r\n");
  if (lineEnd == std::string_view::npos) return std::nullopt;
  // Primera línea: GET <ruta> HTTP/1.1
  const std::string_view first = request.substr(0, lineEnd);
  if (first.substr(0, 4) != "GET ") return std::nullopt;
  const auto sp = first.find(' ', 4);
  if (sp == std::string_view::npos) return std::nullopt;
  UpgradeRequest out;
  out.path = std::string(first.substr(4, sp - 4));
  bool upgrade = false;
  std::size_t pos = lineEnd + 2;
  while (pos < request.size()) {
    const auto end = request.find("\r\n", pos);
    const std::string_view line = request.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
    if (line.empty()) break;
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      const std::string name = lower(trim(line.substr(0, colon)));
      const std::string_view value = trim(line.substr(colon + 1));
      if (name == "upgrade") upgrade = lower(value) == "websocket";
      else if (name == "sec-websocket-key") out.key = std::string(value);
      else if (name == "origin") out.origin = std::string(value);
    }
    if (end == std::string_view::npos) break;
    pos = end + 2;
  }
  if (!upgrade || out.key.empty()) return std::nullopt;
  return out;
}

std::string upgradeResponse(std::string_view key) {
  return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
         websocketAccept(key) + "\r\n\r\n";
}

std::optional<WsFrame> takeWsFrame(std::vector<u8>& buf, std::size_t maxPayload) {
  if (buf.size() < 2) return std::nullopt;
  WsFrame f;
  f.fin = (buf[0] & 0x80) != 0;
  f.opcode = static_cast<WsOpcode>(buf[0] & 0x0F);
  const bool masked = (buf[1] & 0x80) != 0;
  u64 len = buf[1] & 0x7F;
  std::size_t pos = 2;
  if (len == 126) {
    if (buf.size() < 4) return std::nullopt;
    len = static_cast<u64>(buf[2]) << 8 | buf[3];
    pos = 4;
  } else if (len == 127) {
    if (buf.size() < 10) return std::nullopt;
    len = 0;
    for (int i = 0; i < 8; i++) len = len << 8 | buf[2 + i];
    pos = 10;
  }
  if (len > maxPayload) throw DecodeError("trama de WebSocket demasiado grande");
  u8 mask[4] = {0, 0, 0, 0};
  if (masked) {
    if (buf.size() < pos + 4) return std::nullopt;
    std::copy_n(buf.begin() + static_cast<std::ptrdiff_t>(pos), 4, mask);
    pos += 4;
  }
  if (buf.size() < pos + len) return std::nullopt;
  f.payload.assign(buf.begin() + static_cast<std::ptrdiff_t>(pos), buf.begin() + static_cast<std::ptrdiff_t>(pos + len));
  if (masked)
    for (std::size_t i = 0; i < f.payload.size(); i++) f.payload[i] ^= mask[i & 3];
  buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(pos + len));
  return f;
}

std::vector<u8> encodeWsFrame(WsOpcode op, std::span<const u8> payload, bool mask, u32 maskKey) {
  std::vector<u8> out;
  out.reserve(payload.size() + 14);
  out.push_back(static_cast<u8>(0x80 | static_cast<u8>(op)));
  const u8 m = mask ? 0x80 : 0;
  const u64 len = payload.size();
  if (len < 126) {
    out.push_back(static_cast<u8>(m | len));
  } else if (len <= 0xFFFF) {
    out.push_back(m | 126);
    out.push_back(static_cast<u8>(len >> 8));
    out.push_back(static_cast<u8>(len));
  } else {
    out.push_back(m | 127);
    for (int i = 7; i >= 0; i--) out.push_back(static_cast<u8>(len >> (i * 8)));
  }
  const u8 key[4] = {static_cast<u8>(maskKey >> 24), static_cast<u8>(maskKey >> 16), static_cast<u8>(maskKey >> 8), static_cast<u8>(maskKey)};
  if (mask) out.insert(out.end(), key, key + 4);
  for (std::size_t i = 0; i < payload.size(); i++) out.push_back(mask ? static_cast<u8>(payload[i] ^ key[i & 3]) : payload[i]);
  return out;
}

}  // namespace mcw::net
