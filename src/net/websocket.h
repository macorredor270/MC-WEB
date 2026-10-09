#pragma once
// WebSocket (RFC 6455) lo justo para el proxy: saludo HTTP y tramas. Implementación propia.
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

namespace mcw::net {

/// Valor de Sec-WebSocket-Accept para la clave del cliente (SHA-1 + base64 con el GUID de la RFC).
std::string websocketAccept(std::string_view key);

/// Petición de subida a WebSocket ("GET /ruta HTTP/1.1" con Upgrade: websocket).
struct UpgradeRequest {
  std::string path, key, origin;
};
/// Lee la petición (cabeceras completas, hasta la línea vacía). nullopt si no es un WebSocket válido.
std::optional<UpgradeRequest> parseUpgradeRequest(std::string_view request);
/// Respuesta 101 para aceptar la conexión.
std::string upgradeResponse(std::string_view key);

enum class WsOpcode : u8 { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

struct WsFrame {
  WsOpcode opcode = WsOpcode::Binary;
  bool fin = true;
  std::vector<u8> payload;
};

/// Saca la siguiente trama completa del principio de `buf` (y la quita). nullopt si aún falta.
/// Lanza DecodeError si la trama es demasiado grande (más de `maxPayload`).
std::optional<WsFrame> takeWsFrame(std::vector<u8>& buf, std::size_t maxPayload = 1 << 24);
/// Trama lista para mandar. Las del cliente van enmascaradas (`mask`); las del servidor, no.
std::vector<u8> encodeWsFrame(WsOpcode op, std::span<const u8> payload, bool mask = false, u32 maskKey = 0);

}  // namespace mcw::net
