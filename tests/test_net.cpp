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
