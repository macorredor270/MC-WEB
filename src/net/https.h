#pragma once
// Descargas por HTTPS (mbedTLS) con comprobación del certificado del servidor. Solo escritorio: en el navegador
// la página usa `fetch`. Bloqueante: se llama antes de abrir el mundo, con un tiempo máximo.
#include <functional>
#include <string>
#include <vector>

#include "core/types.h"

namespace mcw::net {

/// Si esta compilación sabe descargar (escritorio sí, navegador no).
bool httpsAvailable();

/// GET de `url` (https://...). Sigue hasta 5 redirecciones. `progress(recibidos, total)` (total 0 si no se sabe) y devuelve
/// false para cancelar. `timeoutSec` es el tiempo sin recibir nada. En caso de error devuelve false y rellena `error`.
bool httpsGet(const std::string& url, std::vector<u8>& body, std::string* error, int timeoutSec = 15,
              const std::function<bool(std::size_t, std::size_t)>& progress = {});

}  // namespace mcw::net
