#include <doctest/doctest.h>

#include "core/hash.h"
#include "net/protocol.h"
#include "world/generator.h"

using namespace mcw;
using namespace mcw::net;

TEST_CASE("Protocolo 47: tramas con y sin compresión") {
  for (int threshold : {-1, 64}) {
    PacketCodec out, in;
    out.setCompression(threshold);
    in.setCompression(threshold);
    std::vector<u8> small = {1, 2, 3};
    std::vector<u8> big(5000);
    for (std::size_t i = 0; i < big.size(); i++) big[i] = static_cast<u8>(i * 7);
    std::vector<u8> stream = out.encode(0x21, big);
    const auto s2 = out.encode(0x00, small);
    stream.insert(stream.end(), s2.begin(), s2.end());
    // Llegan de trozo en trozo
    for (std::size_t i = 0; i < stream.size(); i += 333) in.feed(std::span(stream).subspan(i, std::min<std::size_t>(333, stream.size() - i)));
    auto p1 = in.next();
    REQUIRE(p1);
    CHECK(p1->id == 0x21);
    CHECK(p1->data == big);
    auto p2 = in.next();
    REQUIRE(p2);
    CHECK(p2->id == 0);
    CHECK(p2->data == small);
    CHECK_FALSE(in.next());
  }
}

TEST_CASE("Protocolo 47: posición, objetos, metadatos, chat") {
  BufferWriter w;
  writePosition(w, {-123, 64, 4567});
  writePosition(w, {33554431, 255, -33554432});
  writeSlot(w, ItemStack(276, 1, 12));
  writeSlot(w, ItemStack());
  Metadata m;
  m.byte(0, 2);
  m.floatV(6, 20.0f);
  m.string(2, "Paco");
  writeMetadata(w, m);
  BufferReader r(w.data());
  CHECK(readPosition(r) == glm::ivec3(-123, 64, 4567));
  CHECK(readPosition(r) == glm::ivec3(33554431, 255, -33554432));
  CHECK(readSlot(r) == ItemStack(276, 1, 12));
  CHECK(readSlot(r).empty());
  Metadata back = readMetadata(r);
  REQUIRE(back.find(6));
  CHECK(back.find(6)->f == doctest::Approx(20.0f));
  CHECK(back.find(2)->s == "Paco");
  CHECK(r.remaining() == 0);

  CHECK(chatToText(R"({"translate":"chat.type.text","with":["Ana",{"text":"hola"}]})") == "<Ana> hola");
  CHECK(chatToText(R"({"text":"","extra":[{"text":"rojo","color":"red"}]})") == "\xC2\xA7" "crojo");
  CHECK(chatToText(textToChat("¡Hola!")) == "¡Hola!");
  CHECK(toAngle(90.0f) == 64);
}

TEST_CASE("Protocolo 47: una columna de chunk de ida y vuelta") {
  TerrainGenerator gen(99);
  auto c = gen.generate(3, -2);
  u16 mask = 0;
  const std::vector<u8> data = encodeChunkColumn(*c, true, mask);
  CHECK(mask != 0);
  Chunk back(3, -2);
  CHECK(decodeChunkColumn(back, data, mask, true, true) == data.size());
  int diffs = 0;
  for (int y = 0; y < 256; y++)
    for (int z = 0; z < 16; z++)
      for (int x = 0; x < 16; x++) {
        diffs += back.block(x, y, z) != c->block(x, y, z);
        diffs += back.packedLight(x, y, z) != c->packedLight(x, y, z);
      }
  CHECK(diffs == 0);
  CHECK(back.biome(5, 9) == c->biome(5, 9));
}

#include <chrono>
#include <filesystem>
#include <thread>

#include "game/session.h"
#include "net/client.h"
#include "net/server.h"
#include "world/light.h"
#include "world/world.h"

namespace {
struct NetFlatWorld : WorldAccess {
  World w;
  NetFlatWorld() {
    ChunkSet mod;
    for (int cz = -3; cz <= 3; cz++)
      for (int cx = -3; cx <= 3; cx++) {
        auto c = std::make_unique<Chunk>(cx, cz);
        for (int z = 0; z < 16; z++)
          for (int x = 0; x < 16; x++) {
            c->setBlock(x, 0, z, makeState(B::bedrock));
            for (int y = 1; y < 63; y++) c->setBlock(x, y, z, makeState(B::dirt));
            c->setBlock(x, 63, z, makeState(B::grass));
          }
        light::computeInitial(*c);
        w.insert(std::move(c), mod);
      }
  }
  World& world() override { return w; }
  void setBlock(int x, int y, int z, BlockState s) override {
    ChunkSet mod;
    w.setBlock(x, y, z, s, mod);
  }
};
}  // namespace

TEST_CASE("Multijugador: nuestro cliente entra en nuestro servidor por TCP") {
  NetFlatWorld fw;
  GameSession session(fw, 1);
  session.setSpawn({0.5, 64, 0.5});
  Server::Config cfg;
  cfg.guestMode = 1;  // creativo: romper es instantáneo
  cfg.viewDistance = 2;
  Server server(session, cfg);
  session.setBlockListener([&](const glm::ivec3& p, BlockState s) { server.blockChanged(p, s); });
  std::string err;
  REQUIRE(server.start(0, &err));

  // Ping de estado (lista de servidores)
  StatusPinger pinger(connectTcp("127.0.0.1", server.port()), "127.0.0.1", server.port());
  double t = 0;
  for (int i = 0; i < 400 && !pinger.poll(t); i++, t += 0.01) {
    server.tick(t, 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(pinger.ok());
  CHECK(pinger.status().protocol == 47);
  CHECK(pinger.status().motd == "Partida de MC-WEB");

  Client client(connectTcp("127.0.0.1", server.port()), "127.0.0.1", server.port(), "Invitado");
  bool joined = false, gotPos = false;
  int chunks = 0;
  for (int i = 0; i < 600 && !(joined && gotPos && chunks >= 25); i++, t += 0.05) {
    server.tick(t, 1000);
    client.poll();
    for (const auto& e : client.takeEvents()) {
      if (e.type == ClientEvent::Type::Joined) joined = true;
      if (e.type == ClientEvent::Type::PlayerPosition) gotPos = true;
      if (e.type == ClientEvent::Type::Chunk) chunks++;
      if (e.type == ClientEvent::Type::Disconnected) FAIL("desconectado: " << e.text);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(joined);
  CHECK(gotPos);
  CHECK(chunks >= 25);
  CHECK(server.playerCount() == 2);  // anfitrión + invitado
  CHECK(client.uuid() == offlineUuid("Invitado"));

  // Romper un bloque (creativo) y recibir el cambio
  client.sendPosition({0.5, 64, 0.5}, 0, 0, true);
  client.sendDig(0, {2, 63, 2}, 1);
  bool changed = false;
  client.sendChat("hola a todos");
  bool chat = false;
  for (int i = 0; i < 300 && !(changed && chat); i++, t += 0.05) {
    server.tick(t, 1000);
    client.poll();
    for (const auto& e : client.takeEvents()) {
      if (e.type == ClientEvent::Type::BlockChange)
        for (const auto& [p, s] : e.blocks)
          if (p == glm::ivec3(2, 63, 2) && s == 0) changed = true;
      if (e.type == ClientEvent::Type::Chat && e.text.find("hola a todos") != std::string::npos) chat = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(fw.w.block(2, 63, 2) == 0);
  CHECK(changed);
  CHECK(chat);
  const auto hostChat = server.takeChat();
  CHECK(std::any_of(hostChat.begin(), hostChat.end(), [](const std::string& s) { return s == "<Invitado> hola a todos"; }));
  // El anfitrión ve al invitado
  const auto views = server.players();
  REQUIRE(views.size() == 1);
  CHECK(views[0].name == "Invitado");
}

TEST_CASE("Multijugador: dos invitados se ven, se guardan al salir y reciben el motivo al cerrar") {
  namespace stdfs = std::filesystem;
  const stdfs::path dir = stdfs::temp_directory_path() / "mcweb_test_playerdata";
  std::error_code ec;
  stdfs::remove_all(dir, ec);

  NetFlatWorld fw;
  GameSession session(fw, 1);
  session.setSpawn({0.5, 64, 0.5});
  Server::Config cfg;
  cfg.guestMode = 1;
  cfg.viewDistance = 2;
  cfg.playerDataDir = dir;
  Server server(session, cfg);
  std::string err;
  REQUIRE(server.start(0, &err));

  double t = 0;
  // Da vueltas al servidor y a los clientes hasta que se cumpla `done` (o se acabe el tiempo)
  auto pump = [&](std::vector<Client*> clients, auto&& onEvent, auto&& done) {
    for (int i = 0; i < 600 && !done(); i++, t += 0.05) {
      server.tick(t, 1000);
      for (Client* c : clients) {
        c->poll();
        for (const auto& e : c->takeEvents()) onEvent(*c, e);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  };

  auto ana = std::make_unique<Client>(connectTcp("127.0.0.1", server.port()), "127.0.0.1", server.port(), "Ana");
  Client beto(connectTcp("127.0.0.1", server.port()), "127.0.0.1", server.port(), "Beto");
  bool anaSeesBeto = false, betoSeesAna = false;
  pump({ana.get(), &beto},
       [&](Client& c, const ClientEvent& e) {
         if (e.type == ClientEvent::Type::Disconnected) FAIL("desconectado: " << e.text);
         if (e.type == ClientEvent::Type::SpawnPlayer && e.uuid == offlineUuid(&c == ana.get() ? "Beto" : "Ana"))
           (&c == ana.get() ? anaSeesBeto : betoSeesAna) = true;
       },
       [&] { return anaSeesBeto && betoSeesAna; });
  CHECK(anaSeesBeto);
  CHECK(betoSeesAna);
  CHECK(server.playerCount() == 3);

  // Ana (creativo) se pone 5 diamantes en la primera casilla de la barra y se va
  ana->sendCreativeSlot(36, ItemStack(ItemId::diamond, 5));
  int ticks = 0;
  pump({ana.get(), &beto}, [](Client&, const ClientEvent&) {}, [&] { return ++ticks > 20; });
  ana->disconnect();
  ana.reset();
  pump({&beto}, [](Client&, const ClientEvent&) {}, [&] { return server.playerCount() == 2; });
  CHECK(server.playerCount() == 2);
  CHECK(stdfs::exists(dir / (offlineUuid("Ana") + ".dat")));

  // Vuelve y tiene sus diamantes
  Client ana2(connectTcp("127.0.0.1", server.port()), "127.0.0.1", server.port(), "Ana");
  bool gotDiamonds = false;
  pump({&ana2, &beto},
       [&](Client&, const ClientEvent& e) {
         if (e.type == ClientEvent::Type::WindowItems && e.a == 0 && e.items.size() > 36)
           gotDiamonds = e.items[36].id == ItemId::diamond && e.items[36].count == 5;
       },
       [&] { return gotDiamonds; });
  CHECK(gotDiamonds);

  // Cerrar la partida: el motivo llega a los invitados (cierre ordenado, sin RST)
  server.stop();
  std::string reason;
  for (int i = 0; i < 200 && reason.empty(); i++) {
    beto.poll();
    for (const auto& e : beto.takeEvents())
      if (e.type == ClientEvent::Type::Disconnected) reason = e.text;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(reason.find("El anfitrión ha cerrado la partida") != std::string::npos);
  stdfs::remove_all(dir, ec);
}

TEST_CASE("Multijugador: sin jugador local, un zombi persigue y pega al invitado") {
  NetFlatWorld fw;
  GameSession session(fw, 1);
  session.setSpawn({0.5, 64, 0.5});
  session.setLocalPlayerActive(false);  // como en el servidor dedicado
  session.setRules({2, false, false});  // sin monstruos al azar: solo el nuestro
  Server::Config cfg;
  cfg.guestMode = 0;
  cfg.viewDistance = 2;
  cfg.hostName = "";
  Server server(session, cfg);
  std::string err;
  REQUIRE(server.start(0, &err));

  Client carla(connectTcp("127.0.0.1", server.port()), "127.0.0.1", server.port(), "Carla");
  double t = 0;
  bool joined = false;
  for (int i = 0; i < 400 && !joined; i++, t += 0.05) {
    server.tick(t, 18000);
    carla.poll();
    for (const auto& e : carla.takeEvents()) joined |= e.type == ClientEvent::Type::PlayerPosition;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(joined);
  CHECK(server.playerCount() == 1);

  session.spawnMob(MobType::Zombie, {0.5, 64, 6.5});
  bool hurt = false, pushed = false;
  TickInput in;
  in.worldTime = 18000;  // de noche: el zombi no arde
  for (int i = 0; i < 400 && !(hurt && pushed); i++, t += 0.05) {
    session.tick(in);
    server.tick(t, 18000);
    carla.poll();
    for (const auto& e : carla.takeEvents()) {
      if (e.type == ClientEvent::Type::Health && e.f < 20) hurt = true;
      if (e.type == ClientEvent::Type::EntityVelocity && e.eid == carla.entityId()) pushed = true;
      if (e.type == ClientEvent::Type::Disconnected) FAIL("desconectado: " << e.text);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(hurt);
  CHECK(pushed);
  // El zombi ha ido hacia el invitado
  REQUIRE(!session.mobs().empty());
  CHECK(glm::length(session.mobs()[0].pos - glm::dvec3(0.5, 64, 0.5)) < 3.0);
}

#include "net/websocket.h"

namespace {
std::string hex(std::span<const u8> b) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (u8 x : b) {
    s += d[x >> 4];
    s += d[x & 15];
  }
  return s;
}
}  // namespace

TEST_CASE("SHA-1: vectores de FIPS 180") {
  CHECK(hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  CHECK(hex(sha1(std::string(1000000, 'a'))) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("WebSocket: saludo de la RFC 6455 y tramas de ida y vuelta") {
  // Ejemplo de la sección 1.3 de la RFC
  CHECK(websocketAccept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  const auto up = parseUpgradeRequest(
      "GET /mc.example.org:25565 HTTP/1.1\r\nHost: localhost:25500\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://localhost\r\nSec-WebSocket-Version: 13\r\n\r\n");
  REQUIRE(up.has_value());
  CHECK(up->path == "/mc.example.org:25565");
  CHECK(up->key == "dGhlIHNhbXBsZSBub25jZQ==");
  CHECK(up->origin == "http://localhost");
  CHECK_FALSE(parseUpgradeRequest("GET / HTTP/1.1\r\nHost: x\r\n\r\n").has_value());
  CHECK(upgradeResponse("dGhlIHNhbXBsZSBub25jZQ==").find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos);

  // Tamaños con longitud de 7, 16 y 64 bits; las del cliente enmascaradas
  for (std::size_t size : {std::size_t{0}, std::size_t{5}, std::size_t{125}, std::size_t{126}, std::size_t{300}, std::size_t{70000}}) {
    std::vector<u8> payload(size);
    for (std::size_t i = 0; i < size; i++) payload[i] = static_cast<u8>(i * 7 + 3);
    for (bool masked : {false, true}) {
      std::vector<u8> wire = encodeWsFrame(WsOpcode::Binary, payload, masked, 0x12345678);
      wire.push_back(0xAB);  // el principio de la siguiente trama se queda
      // Por trozos: hasta que no llega entera no sale
      std::vector<u8> buf(wire.begin(), wire.begin() + 1);
      CHECK_FALSE(takeWsFrame(buf).has_value());
      buf.assign(wire.begin(), wire.end());
      const auto f = takeWsFrame(buf);
      REQUIRE(f.has_value());
      CHECK(f->opcode == WsOpcode::Binary);
      CHECK(f->fin);
      CHECK(f->payload == payload);
      REQUIRE(buf.size() == 1);
      CHECK(buf[0] == 0xAB);
    }
  }
  // Un ping de control
  std::vector<u8> ping = encodeWsFrame(WsOpcode::Ping, std::vector<u8>{1, 2, 3}, true, 7);
  const auto pf = takeWsFrame(ping);
  REQUIRE(pf.has_value());
  CHECK(pf->opcode == WsOpcode::Ping);
  CHECK(pf->payload == std::vector<u8>{1, 2, 3});
}

#include "assets/image.h"

TEST_CASE("Skins: el mensaje del canal MCWEB|Skin, con su validación") {
  const std::vector<u8> png = encodePng(Image(64, 64, 0xFF336699));
  SkinMessage m;
  m.uuid = uuidFromString("25a6e036-1424-3aad-8eb7-dac5960d29b6");
  m.slim = true;
  m.png = png;
  // Del servidor al cliente (con UUID) y del cliente al servidor (sin él)
  const auto withUuid = decodeSkinMessage(encodeSkinMessage(m, true), true);
  REQUIRE(withUuid.has_value());
  CHECK(withUuid->uuid == m.uuid);
  CHECK(withUuid->slim);
  CHECK(withUuid->png == png);
  const auto without = decodeSkinMessage(encodeSkinMessage(m, false), false);
  REQUIRE(without.has_value());
  CHECK_FALSE(without->png.empty());
  // 64x32 también vale
  m.png = encodePng(Image(64, 32, 0xFF000000));
  CHECK(decodeSkinMessage(encodeSkinMessage(m, false), false).has_value());
  // Lo que no vale: otro tamaño, algo que no es un PNG, demasiado grande, vacío o de otra versión
  m.png = encodePng(Image(100, 100, 0xFF000000));
  CHECK_FALSE(decodeSkinMessage(encodeSkinMessage(m, false), false).has_value());
  m.png = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
  CHECK_FALSE(decodeSkinMessage(encodeSkinMessage(m, false), false).has_value());
  m.png = png;
  m.png.resize(kMaxSkinBytes + 1, 0);
  CHECK_FALSE(decodeSkinMessage(encodeSkinMessage(m, false), false).has_value());
  m.png.clear();
  CHECK_FALSE(decodeSkinMessage(encodeSkinMessage(m, false), false).has_value());
  std::vector<u8> wrongVersion = encodeSkinMessage({{}, false, png}, false);
  wrongVersion[0] = 2;
  CHECK_FALSE(decodeSkinMessage(wrongVersion, false).has_value());
  CHECK_FALSE(decodeSkinMessage(std::vector<u8>{}, false).has_value());
}

TEST_CASE("Skins: el servidor reparte la skin de cada jugador, también a los que entran después") {
  NetFlatWorld fw;
  GameSession session(fw, 1);
  session.setSpawn({0.5, 64, 0.5});
  Server::Config cfg;
  cfg.viewDistance = 2;
  cfg.hostName = "";  // servidor dedicado: no hay jugador local
  Server server(session, cfg);
  const std::vector<u8> hostPng = encodePng(Image(64, 64, 0xFF111111)), anaPng = encodePng(Image(64, 64, 0xFF22AA22)),
                        betoPng = encodePng(Image(64, 32, 0xFF3333CC));
  server.setHostSkin(hostPng, false, 0x7F);
  std::string err;
  REQUIRE(server.start(0, &err));

  struct Got {
    std::map<std::string, std::vector<u8>> skins;  // uuid -> png
    std::map<std::string, bool> slim;
  };
  double t = 0;
  auto pump = [&](std::vector<std::pair<Client*, Got*>> clients, auto&& done, int steps = 400) {
    for (int i = 0; i < steps && !done(); i++, t += 0.05) {
      server.tick(t, 1000);
      for (auto& [c, got] : clients) {
        c->poll();
        for (const auto& e : c->takeEvents()) {
          if (e.type == ClientEvent::Type::PlayerSkin) {
            got->skins[e.uuid] = e.data;
            got->slim[e.uuid] = e.flag;
          }
          if (e.type == ClientEvent::Type::Disconnected) FAIL("desconectado: " << e.text);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  };

  Client ana(connectTcp("127.0.0.1", server.port()), "127.0.0.1", server.port(), "Ana");
  ana.setSkin(anaPng, true);
  ana.setSkinParts(0x0A);  // solo chaqueta y manga derecha
  Got anaGot;
  const std::string hostUuid = offlineUuid("Anfitrion"), anaUuid = offlineUuid("Ana"), betoUuid = offlineUuid("Beto");
  // Sin anfitrión (servidor dedicado) no hay skin suya que repartir, aunque se la hayan puesto
  pump({{&ana, &anaGot}}, [&] { return server.playerCount() == 1 && ana.playing(); });
  for (int i = 0; i < 40; i++, t += 0.05) {  // un poco más para que llegue lo último
    server.tick(t, 1000);
    ana.poll();
    ana.takeEvents();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(anaGot.skins.empty());  // sin anfitrión (hostName vacío) no hay skin suya que repartir

  // Con anfitrión: otro servidor
  server.stop();
  Server::Config cfg2;
  cfg2.viewDistance = 2;
  cfg2.hostName = "Anfitrion";
  Server host(session, cfg2);
  host.setHostSkin(hostPng, false, 0x7F);
  REQUIRE(host.start(0, &err));
  auto pump2 = [&](std::vector<std::pair<Client*, Got*>> clients, auto&& done) {
    for (int i = 0; i < 500 && !done(); i++, t += 0.05) {
      host.tick(t, 1000);
      for (auto& [c, got] : clients) {
        c->poll();
        for (const auto& e : c->takeEvents()) {
          if (e.type == ClientEvent::Type::PlayerSkin) {
            got->skins[e.uuid] = e.data;
            got->slim[e.uuid] = e.flag;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  };
  Client ana2(connectTcp("127.0.0.1", host.port()), "127.0.0.1", host.port(), "Ana");
  ana2.setSkin(anaPng, true);
  ana2.setSkinParts(0x0A);
  Got ana2Got;
  pump2({{&ana2, &ana2Got}}, [&] { return ana2Got.skins.count(hostUuid) != 0; });
  // Ana recibe la del anfitrión...
  REQUIRE(ana2Got.skins.count(hostUuid) == 1);
  CHECK(ana2Got.skins[hostUuid] == hostPng);
  CHECK_FALSE(ana2Got.slim[hostUuid]);
  // ...y el anfitrión (la partida) ve la de Ana en su lista de jugadores
  pump2({{&ana2, &ana2Got}}, [&] {
    const auto v = host.players();
    return !v.empty() && v[0].skin != nullptr;
  });
  auto views = host.players();
  REQUIRE(views.size() == 1);
  REQUIRE(views[0].skin != nullptr);
  CHECK(*views[0].skin == anaPng);
  CHECK(views[0].skinSlim);
  CHECK(views[0].skinVersion == 1);
  CHECK(views[0].skinParts == 0x0A);

  // Beto entra después y recibe la del anfitrión y la de Ana; manda la suya y Ana la recibe
  Client beto(connectTcp("127.0.0.1", host.port()), "127.0.0.1", host.port(), "Beto");
  beto.setSkin(betoPng, false);
  Got betoGot;
  bool betoSawAnaSpawn = false;
  u8 anaParts = 0;
  for (int i = 0; i < 600 && !(betoGot.skins.count(hostUuid) && betoGot.skins.count(anaUuid) && ana2Got.skins.count(betoUuid)); i++, t += 0.05) {
    host.tick(t, 1000);
    ana2.poll();
    for (const auto& e : ana2.takeEvents())
      if (e.type == ClientEvent::Type::PlayerSkin) ana2Got.skins[e.uuid] = e.data;
    beto.poll();
    for (const auto& e : beto.takeEvents()) {
      if (e.type == ClientEvent::Type::PlayerSkin) betoGot.skins[e.uuid] = e.data;
      if (e.type == ClientEvent::Type::SpawnPlayer && e.uuid == anaUuid) {
        betoSawAnaSpawn = true;
        if (const auto* p = e.meta.find(10)) anaParts = static_cast<u8>(p->i);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(betoGot.skins[hostUuid] == hostPng);
  CHECK(betoGot.skins[anaUuid] == anaPng);
  CHECK(ana2Got.skins[betoUuid] == betoPng);
  // Y las capas visibles de Ana llegan en los metadatos de su entidad
  for (int i = 0; i < 200 && !betoSawAnaSpawn; i++, t += 0.05) {
    host.tick(t, 1000);
    ana2.poll();
    ana2.takeEvents();
    beto.poll();
    for (const auto& e : beto.takeEvents())
      if (e.type == ClientEvent::Type::SpawnPlayer && e.uuid == anaUuid) {
        betoSawAnaSpawn = true;
        if (const auto* p = e.meta.find(10)) anaParts = static_cast<u8>(p->i);
      }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(betoSawAnaSpawn);
  CHECK(anaParts == 0x0A);

  // Una skin que no vale (100x100) no se reparte
  Client carla(connectTcp("127.0.0.1", host.port()), "127.0.0.1", host.port(), "Carla");
  Got carlaGot;
  // (el cliente no deja mandar un PNG fuera de medida, así que se manda a mano con un paquete a pelo)
  carla.setSkin(encodePng(Image(100, 100, 0xFF000000)), false);
  for (int i = 0; i < 300; i++, t += 0.05) {
    host.tick(t, 1000);
    carla.poll();
    for (const auto& e : carla.takeEvents())
      if (e.type == ClientEvent::Type::PlayerSkin) carlaGot.skins[e.uuid] = e.data;
    beto.poll();
    for (const auto& e : beto.takeEvents())
      if (e.type == ClientEvent::Type::PlayerSkin) betoGot.skins[e.uuid] = e.data;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(betoGot.skins.count(offlineUuid("Carla")) == 0);
  CHECK(carlaGot.skins.count(hostUuid) == 1);  // ella sí recibe las de los demás
  host.stop();
}
