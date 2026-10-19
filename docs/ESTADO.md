# MC-WEB — Estado del proyecto (2026-10-19)

Reimplementación "sala limpia" de Minecraft 1.8 en C++20 / SDL3 (nativo + Emscripten/web).
Rama de trabajo: `claude/zen-cannon-3h6qbm`. Nada de Mojang en el repo (jar en `.gitignore`, recursos oficiales se descargan en ejecución).

## 1. Qué estaba haciendo justo antes de parar: EL FARO (beacon, B9 segunda mitad)

Todo compila y la suite pasa (359 pruebas), pero el faro está **a medias** (solo lógica de partida + parte del menú).

### Hecho del faro (en este commit)
- `src/data/blocks.h`: constante `B::beacon = 138`.
- `src/game/beacon.{h,cpp}` (nuevo, en `src/game/CMakeLists.txt`):
  - `beaconPyramidLevels` (0–4 niveles de hierro/oro/esmeralda/diamante), `beaconSkyClear`, `beaconBeamColors` (cristales teñidos sobre el faro),
  - `beaconPrimaryAllowed` / `beaconSecondaryAllowed` (velocidad/prisa nivel 1; resistencia/salto nivel 2; fuerza nivel 3; secundario solo con 4 niveles: regeneración o repetir el principal), `beaconPayment`,
  - `GameSession::confirmBeacon` (valida, cobra 1 objeto de pago salvo en creativo, guarda `BeaconData`, avisa a `tileListener_`),
  - `GameSession::tickBeacons` (cada 80 ticks, radio `nivel*10+10`, duración `(9+2*nivel)*20` ticks, logro `FullBeacon` con 4 niveles y ambos poderes).
- `block_entity.h`: `BeaconData{primary,secondary}`, `TileEntities::beacons` (any/clear/forget/blockSet).
- `session.{h,cpp}`: `UseResult::Kind::Beacon`, `beaconPos_`, `tickCount_`, llamada a `tickBeacons()`, usar el bloque 138 abre el menú.
- `menu.{h,cpp}`: `MenuKind::Beacon`, `SlotRole::BeaconPay`, casilla de pago en (136,110), inventario del jugador desplazado (x0=36), reglas de shift-click, devolver el pago al cerrar, accesores `beaconLevels/Primary/Secondary/Pay`.

### FALTA del faro (en este orden)
1. **Guardado NBT**: `beaconToNbt/beaconFromNbt` en `src/save/anvil.{h,cpp}` (id `"Beacon"`, `Levels`, `Primary`, `Secondary`) y engancharlo en `src/save/chunk_io.cpp` (lectura junto a `skullFromNbt`, escritura junto al bucle de `te.skulls`).
2. **Confirmar desde la ventana**: en `Game`/HUD llamar a `confirmBeacon` con el pago de `menu_->beaconPay()`; cerrar menú tras confirmar.
3. **Ventana en el HUD** (`src/client/hud.cpp`, `drawMenu`): textura `gui/container/beacon.png` (230x219; con jar la oficial), botones de poder por nivel, check de confirmar y X de cancelar, iconos de efecto; **fallback libre** en `src/assets/cc0_pack.cpp` (pintar `beacon.png` procedural).
4. **Rayo de luz**: en `EntityRenderer::drawBlockEntities` (`src/client/tile_renderer.cpp`), usar `textures/entity/beacon_beam.png`, teñido por `beaconBeamColors`, girando; textura libre de fallback. Hay que mandar la lista de faros al renderer (como cofres/carteles).
5. **Red**: ventana `minecraft:beacon` (Open Window 0x2D, 1 casilla de pago + 36 = 37), plugin message serverbound `MC|Beacon` (dos ints: primario, secundario) → `confirmBeacon` bajo `actAs` en `src/net/server.cpp`; cliente `src/net/client.cpp` + `src/client/game_net.cpp` (`sendBeacon`); sincronizar el tile al cliente (Block Entity 0x35 al cargar chunk/cambiar). Tick de faros para invitados (`activePlayers()` ya los incluye; comprobar `localActive_`).
6. **Iconos de efecto** en el HUD y recetas (la receta del faro ya está en `recipes_shaped.inc`: 5 cristal + estrella del Nether + 3 obsidiana).
7. **Pruebas** `tests/test_beacon.cpp` (añadir a `tests/CMakeLists.txt`): niveles de pirámide, cielo despejado, validación de poderes, cobro, tick aplica efectos por radio, guardado NBT, menú (pago/shift-click/cerrar), red.
8. Suite completa, commit, push, mirar CI (Linux gcc/clang, Windows MSVC —ojo raw strings dentro de `CHECK`—, Web).

## 2. Ya terminado y subido (commits recientes)
Wither completo (`05a29f0`): montaje en T, IA, calaveras, barra de jefe, red, modelo/texturas libres. Estructuras (`7020ce7`): aldeas, templo del desierto, cabaña de bruja, pozo, minas. Comercio con aldeanos (`c754079`). Pesca (`54b9486`). Criaturas nuevas (`06f127c`). Marcos, cuadros y soporte de armadura. Tiempo (lluvia/tormenta/nieve). Pociones, atril, caldero. Nether, End, dragón. Vagonetas y raíles. Encantamientos y yunque. Armadura, experiencia. Entidades de bloque (carteles, estandartes, cabezas, cofres). Creativo con pestañas. Rendimiento estructural (arena de mallas, oclusión, adaptativo). Controles táctiles (joystick fijo). Multijugador protocolo 47 (LAN, servidor, interop con mineflayer).
CI: verde hasta el commit de pesca; **sin comprobar** los de comercio, estructuras y Wither.

## 3. Pendiente después del faro (orden del plan)
1. **B10 biomas**: jungla, mesa, champiñón, taiga gigante, bosque oscuro, pinchos de hielo, variantes de colinas; templo de la jungla; monumento oceánico + guardián; logro `ExploreAllBiomes` (Aventurero).
2. **B11**: logros de invitados y "Diamantes a ti".
3. **Mapas** (tarea #75): objeto, datos 128x128, mostrar en marco, paquete 0x34.
4. Camas que explotan en el Nether; fuegos artificiales; icono de caña "lanzada".
5. **Paridad multijugador**: dimensiones y portales en servidor, tiempo/clima en servidor dedicado, invitados lanzando objetos.
6. Sonidos oficiales en web.
7. **H5**: editor táctil de disposición, arrastrar/soltar, pulsación larga, inercia.
8. **C2.3 online-mode** (AES-CFB8 + RSA + login Microsoft): necesita un *client ID* de Azure aprobado por Mojang, solo el usuario puede obtenerlo. Documentar (C2.4).
9. README completo.
10. **Release v1.0.0** al final (sin betas): `release.yml` con .exe/zip, .deb, AppImage, tar.gz, zip web, `mcweb-server`, `mcweb-wsproxy`. Después recordar activar GitHub Pages (Settings > Pages > gh-pages / root): https://macorredor270.github.io/MC-WEB/

## 4. Límites conocidos
Aldeas en bosques se solapan con árboles; la capa de remolino de la armadura del Wither no se dibuja; apuntado lateral de las cabezas del Wither simplificado; monumento oceánico y templo de la jungla aplazados (B10); mapas sin empezar; un invitado con ventana de cofre abierta cuyo cofre se rompe deja puntero colgante (preexistente).

## 5. Cómo trabajar en local
```
git pull origin claude/zen-cannon-3h6qbm
CMAKE_POLICY_VERSION_MINIMUM=3.5 cmake --preset linux-release
cmake --build --preset linux-release -j8
./build/linux-release/apps/mcweb/mcweb
ctest --test-dir build/linux        # o build/linux/tests/mcweb_tests
```
Reglas: comentarios/UI en español, sala limpia, minecraft.wiki solo como referencia de comportamiento (no copiar texto), commits con los trailers `Co-Authored-By` y `Claude-Session`, sin nombres de modelo dentro del repo, no abrir PR sin pedirlo.

Trampas conocidas: MSVC rechaza raw strings dentro de `CHECK` de doctest; no consumir `rng_` en constructores; `spawnMob` invalida punteros `Mob*` (re-obtener por id); `TickInput` pitch/yaw se reinician cada tick.
