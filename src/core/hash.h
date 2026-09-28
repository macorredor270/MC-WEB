#pragma once
#include <array>
#include <string>
#include <string_view>

#include "core/types.h"

namespace mcw {

/// MD5 (RFC 1321).
std::array<u8, 16> md5(const void* data, std::size_t size);
inline std::array<u8, 16> md5(std::string_view s) { return md5(s.data(), s.size()); }

/// UUID de un jugador sin cuenta (modo offline de 1.8): MD5 de "OfflinePlayer:<nombre>" con la
/// versión 3. Devuelve la forma con guiones ("xxxxxxxx-xxxx-3xxx-...").
std::string offlineUuid(std::string_view name);

/// 16 bytes -> texto con guiones.
std::string uuidToString(const std::array<u8, 16>& b);

}  // namespace mcw
