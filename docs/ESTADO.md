# MC-WEB — Estado del proyecto (2026-10-19)

Reimplementación "sala limpia" de Minecraft 1.8 en C++20 / SDL3 (nativo + Emscripten/web).
Rama de trabajo: `claude/zen-cannon-3h6qbm`. Nada de Mojang en el repo (jar en `.gitignore`, recursos oficiales se descargan en ejecución).

## 1. Último trabajo: EL FARO (beacon, B9 segunda mitad) — terminado, falta mirar el CI

Todo compila y la suite pasa (373 pruebas: 13 de `tests/test_beacon.cpp` y 1 de red en `tests/test_net.cpp`).

- Lógica: `src/game/beacon.{h,cpp}` (pirámide, cielo, colores del rayo, poderes permitidos, pago, `confirmBeacon`, `confirmOpenBeacon`, `tickBeacons`). Los faros también dan efectos con servidor dedicado (`GameSession::tick` ya los llama en la rama sin jugador local).
- Guardado: `beaconToNbt/beaconFromNbt` (id `Beacon`, `Primary`, `Secondary`; los niveles se calculan, no se guardan) enganchado en `chunk_io.cpp`.
- Ventana: `hud.cpp` (`beaconButtons`, `beaconButtonAt`, `drawBeaconExtras`), clic en `game.cpp`; textura libre `gui/container/beacon.png` en `cc0_pack.cpp`. Los iconos de efecto son cuadros de color con inicial (como los efectos activos del HUD), no la hoja de iconos.
- Rayo: `drawBlockEntities` (`tile_renderer.cpp`), dos prismas con mezcla aditiva, el de dentro gira; textura libre `entity/beacon_beam.png`. Solo se dibuja en faros con poderes ya elegidos (los que están en `tiles.beacons`), y todos los cristales de color tiñen el rayo entero (no por tramos).
- Red: ventana `minecraft:beacon` (propiedades 0/1/2 = niveles/principal/secundario), mensaje de plugin `MC|Beacon` (dos enteros), Block Entity 0x35 acción 3.
- Shift-click del pago: pasa solo uno a la casilla y el resto se queda en su sitio.
- **No verificado a mano**: el aspecto de la ventana y del rayo en pantalla (solo compila y pasan las pruebas lógicas y de red).

## 2. Ya terminado y subido (commits recientes)
Wither completo (`05a29f0`): montaje en T, IA, calaveras, barra de jefe, red, modelo/texturas libres. Estructuras (`7020ce7`): aldeas, templo del desierto, cabaña de bruja, pozo, minas. Comercio con aldeanos (`c754079`). Pesca (`54b9486`). Criaturas nuevas (`06f127c`). Marcos, cuadros y soporte de armadura. Tiempo (lluvia/tormenta/nieve). Pociones, atril, caldero. Nether, End, dragón. Vagonetas y raíles. Encantamientos y yunque. Armadura, experiencia. Entidades de bloque (carteles, estandartes, cabezas, cofres). Creativo con pestañas. Rendimiento estructural (arena de mallas, oclusión, adaptativo). Controles táctiles (joystick fijo). Multijugador protocolo 47 (LAN, servidor, interop con mineflayer).
CI: verde en comercio, estructuras y Wither (el Interop del Wither falló una vez y volvió a pasar en el commit siguiente). El del faro (primera mitad) estaba en marcha al retomar.

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
