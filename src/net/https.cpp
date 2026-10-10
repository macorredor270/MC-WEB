#include "net/https.h"

#ifdef __EMSCRIPTEN__
namespace mcw::net {
bool httpsAvailable() { return false; }
bool httpsGet(const std::string&, std::vector<u8>&, std::string* error, int, const std::function<bool(std::size_t, std::size_t)>&) {
  if (error) *error = "sin HTTPS en el navegador";
  return false;
}
}  // namespace mcw::net
#else

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace mcw::net {
namespace {

std::string mbedError(int code) {
  char buf[160];
  mbedtls_strerror(code, buf, sizeof(buf));
  return buf;
}

/// Raíces de confianza del sistema: un archivo de certificados en Linux y macOS, el almacén de Windows allí.
bool loadRoots(mbedtls_x509_crt& roots) {
  int loaded = 0;
#ifdef _WIN32
  if (HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT")) {
    for (PCCERT_CONTEXT c = CertEnumCertificatesInStore(store, nullptr); c; c = CertEnumCertificatesInStore(store, c))
      if (mbedtls_x509_crt_parse_der(&roots, c->pbCertEncoded, c->cbCertEncoded) == 0) loaded++;
    CertCloseStore(store, 0);
  }
#else
  if (const char* env = std::getenv("SSL_CERT_FILE"); env && mbedtls_x509_crt_parse_file(&roots, env) >= 0) loaded++;
  if (!loaded)
  for (const char* path : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem",
                           "/etc/ssl/ca-bundle.pem", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"}) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) && mbedtls_x509_crt_parse_file(&roots, path) >= 0) loaded++;
    if (loaded) break;
  }
#endif
  return loaded > 0;
}

struct Url {
  std::string host, path;
  int port = 443;
};

bool parseUrl(const std::string& url, Url& out) {
  if (url.rfind("https://", 0) != 0) return false;
  const std::size_t slash = url.find('/', 8);
  std::string hostPort = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
  out.path = slash == std::string::npos ? "/" : url.substr(slash);
  if (const std::size_t colon = hostPort.rfind(':'); colon != std::string::npos) {
    out.port = std::atoi(hostPort.c_str() + colon + 1);
    hostPort.resize(colon);
  }
  out.host = hostPort;
  return !out.host.empty();
}

/// Una petición: devuelve el código HTTP (0 si falla) y, si es una redirección, el destino en `location`.
int getOnce(const Url& u, mbedtls_x509_crt& roots, std::vector<u8>& body, std::string& location, std::string& error, int timeoutSec,
            const std::function<bool(std::size_t, std::size_t)>& progress) {
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_net_init(&net);
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  int status = 0;
  auto done = [&]() {
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_net_free(&net);
    return status;
  };
  int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
  if (rc) { error = "aleatorios: " + mbedError(rc); return done(); }
  if ((rc = mbedtls_net_connect(&net, u.host.c_str(), std::to_string(u.port).c_str(), MBEDTLS_NET_PROTO_TCP))) {
    error = "no se pudo conectar con " + u.host;
    return done();
  }
  if ((rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT))) {
    error = "TLS: " + mbedError(rc);
    return done();
  }
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&conf, &roots, nullptr);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
  mbedtls_ssl_conf_read_timeout(&conf, static_cast<uint32_t>(timeoutSec * 1000));
  if ((rc = mbedtls_ssl_setup(&ssl, &conf)) || (rc = mbedtls_ssl_set_hostname(&ssl, u.host.c_str()))) {
    error = "TLS: " + mbedError(rc);
    return done();
  }
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, nullptr, mbedtls_net_recv_timeout);
  while ((rc = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
      error = "el servidor no es de fiar o no responde (" + mbedError(rc) + ")";
      return done();
    }
  }
  const std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
                          "\r\nUser-Agent: MC-WEB\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
  for (std::size_t sent = 0; sent < req.size();) {
    rc = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(req.data()) + sent, req.size() - sent);
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    if (rc < 0) { error = "no se pudo enviar la petición"; return done(); }
    sent += static_cast<std::size_t>(rc);
  }
  std::vector<u8> raw;
  std::size_t headerEnd = std::string::npos, total = 0;
  bool chunked = false;
  unsigned char buf[16384];
  for (;;) {
    rc = mbedtls_ssl_read(&ssl, buf, sizeof(buf));
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
    if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == 0) break;
    if (rc < 0) {
      if (headerEnd != std::string::npos && !chunked && total && raw.size() - headerEnd >= total) break;
      error = rc == MBEDTLS_ERR_SSL_TIMEOUT ? "sin respuesta" : "conexión cortada (" + mbedError(rc) + ")";
      status = 0;
      return done();
    }
    raw.insert(raw.end(), buf, buf + rc);
    if (headerEnd == std::string::npos) {
      const std::string head(raw.begin(), raw.end());
      if (const std::size_t e = head.find("\r\n\r\n"); e != std::string::npos) {
        headerEnd = e + 4;
        status = std::atoi(head.c_str() + head.find(' ') + 1);
        auto header = [&](const char* name) {
          std::string lower = head.substr(0, e);
          std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
          const std::string key = std::string("\r\n") + name + ":";
          const std::size_t at = lower.find(key);
          if (at == std::string::npos) return std::string();
          std::size_t from = at + key.size();
          while (from < e && head[from] == ' ') from++;
          return head.substr(from, head.find("\r\n", from) - from);
        };
        total = static_cast<std::size_t>(std::atoll(header("content-length").c_str()));
        chunked = header("transfer-encoding").find("chunked") != std::string::npos;
        location = header("location");
      }
    }
    if (progress && headerEnd != std::string::npos && !progress(raw.size() - headerEnd, total)) {
      error = "cancelado";
      status = 0;
      return done();
    }
    if (headerEnd != std::string::npos && !chunked && total && raw.size() - headerEnd >= total) break;
  }
  if (headerEnd == std::string::npos) { error = "respuesta vacía"; status = 0; return done(); }
  body.assign(raw.begin() + static_cast<std::ptrdiff_t>(headerEnd), raw.end());
  if (chunked) {  // cuerpo troceado: tamaño en hexadecimal, datos, salto
    std::vector<u8> out;
    std::size_t p = 0;
    while (p < body.size()) {
      std::size_t eol = p;
      while (eol + 1 < body.size() && !(body[eol] == '\r' && body[eol + 1] == '\n')) eol++;
      const std::size_t len = std::strtoull(std::string(body.begin() + static_cast<std::ptrdiff_t>(p), body.begin() + static_cast<std::ptrdiff_t>(eol)).c_str(), nullptr, 16);
      p = eol + 2;
      if (len == 0 || p + len > body.size()) break;
      out.insert(out.end(), body.begin() + static_cast<std::ptrdiff_t>(p), body.begin() + static_cast<std::ptrdiff_t>(p + len));
      p += len + 2;
    }
    body = std::move(out);
  } else if (total && body.size() > total) {
    body.resize(total);
  }
  return done();
}

}  // namespace

bool httpsAvailable() { return true; }

bool httpsGet(const std::string& url0, std::vector<u8>& body, std::string* error, int timeoutSec,
              const std::function<bool(std::size_t, std::size_t)>& progress) {
  static bool psaReady = psa_crypto_init() == PSA_SUCCESS;
  (void)psaReady;
  std::string err;
  mbedtls_x509_crt roots;
  mbedtls_x509_crt_init(&roots);
  if (!loadRoots(roots)) {
    if (error) *error = "no hay certificados raíz del sistema para comprobar el servidor";
    mbedtls_x509_crt_free(&roots);
    return false;
  }
  std::string url = url0;
  bool ok = false;
  for (int hop = 0; hop < 6; hop++) {
    Url u;
    if (!parseUrl(url, u)) { err = "dirección no válida: " + url; break; }
    std::string location;
    body.clear();
    const int code = getOnce(u, roots, body, location, err, timeoutSec, progress);
    if (code >= 200 && code < 300) { ok = true; break; }
    if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && !location.empty()) {
      url = location.rfind("http", 0) == 0 ? location : "https://" + u.host + location;
      continue;
    }
    if (err.empty()) err = "HTTP " + std::to_string(code);
    break;
  }
  mbedtls_x509_crt_free(&roots);
  if (!ok && error) *error = err;
  return ok;
}

}  // namespace mcw::net
#endif
