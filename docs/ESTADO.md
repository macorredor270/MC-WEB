# MC-WEB — Estado del proyecto (2026-10-19)

Reimplementación "sala limpia" de Minecraft 1.8 en C++20 / SDL3 (nativo + Emscripten/web).
Rama de trabajo: `claude/zen-cannon-3h6qbm`. Nada de Mojang en el repo (jar en `.gitignore`, recursos oficiales se descargan en ejecución).

## 1. Último trabajo: B10 biomas, guardián y monumento (terminado); el faro cerró B9

La suite pasa (386 pruebas). Detalles, para no tener que releer el código:

- **Faro** (B9): lógica en `src/game/beacon.*`, ventana en `hud.cpp`, rayo en `tile_renderer.cpp`, red con `MC|Beacon` y Block Entity 0x35 acción 3. Los faros dan efectos también en servidor dedicado.
- **Biomas** (`src/world/generator.cpp`, `src/data/biomes.*`): jungla (+borde y colinas), mesa (+mesetas, con terrazas y franjas de arcilla), isla de hongos (+orilla), taiga gigante, bosque oscuro, pinchos de hielo, río helado, montañas con árboles y las variantes de colinas. Se eligen por clima (`temperature_`, `humidity_`) y por un ruido de variantes (`variant_`). `baseBiome()` reduce una variante a su bioma de base (se usa en árboles, plantas y criaturas). Árboles nuevos: jungla (con lianas), jungla gigante 2x2, roble oscuro 2x2, picea gigante 2x2, setas gigantes y pinchos de hielo. `tests/test_biomes.cpp` comprueba que el mundo normal da todos los biomas de la lista de exploración.
- **Aventurero** (`ExploreAllBiomes`): los biomas pisados se guardan como estadísticas `mcweb.biome.<id>`; se concede al llegar a los 39 de 1.8 y tener "El fin" (su logro previo).
- **Templo de la jungla** (`temples.cpp`, `jungleTempleSite`): adoquín y adoquín musgoso, sala principal, sala alta con dos cofres y cámara sellada con otro. **Sin trampas de flechas ni palancas** (no existe aún el cable de trampa).
- **Guardián y anciano** (`MobType::Guardian`, entidad 68): rayo que se carga 4 s (3 el anciano), púas que devuelven el golpe, fatiga de minado III del anciano. Metadatos de red 16 (bit 2 púas, bit 4 anciano) y 17 (a quién apunta). `/summon Guardian [x y z] [elder]`. El rayo se dibuja de azul a rojo según se carga; en un cliente de red el tiempo de carga se cuenta en local (aproximado).
- **Monumento oceánico** (`temples.cpp`, `monumentSite`): 58x58 y 23 de alto, cuatro cuerpos de ladrillo de prismarina, anillo de pasillos, radios, sala del tesoro con ocho bloques de oro, torre, dos salas de esponja húmeda y linternas marinas; 3 ancianos y 10 guardianes como criaturas de generación (aparecen al generar el chunk, **no reaparecen** si se matan). Una prueba recorre la estructura por dentro desde la entrada.
- `jungleTempleSite` y `monumentSite` cachean por región (generar un chunk bajó de 0,82 a 0,46 ms).
- **B11: logros de invitados y "Diamantes para ti"**: los logros viven en `Player::achievements` (con `actAs` cambian con el invitado). El servidor avisa con Statistics (0x37, al unirse todo y luego cada logro nuevo), lo cuenta a todos por el chat y lo guarda en el `.dat` del invitado (`MCWebStats`); `trackPlayerProgress` y `notePickup` se llaman por invitado (también ahora cobran experiencia al fundir). Los objetos lanzados llevan `thrower`; `awardTo(uid, logro)` se lo da a quien lo lanzó. Un cliente de red no concede nada por su cuenta (`award` no hace nada con `remote()`), solo `grantAchievement` desde el servidor; el primer paquete se aplica sin ventanita.
- Criaturas por bioma: champiñacas sobre micelio, ocelotes en jungla, lobos también en taiga gigante.
- **No verificado a mano**: el aspecto del faro, del monumento por dentro y del reparto real de biomas en una partida larga (solo capturas sueltas con el pack libre y pruebas lógicas).
- El job **Interop 1.8 de CI falla de forma intermitente** ("nuestro cliente contra flying-squid": el mundo no termina de cargar en 60 s); ya fallaba antes del faro y pasa a ratos. Los de Linux, Windows y Web van bien.

## 2. Ya terminado y subido (commits recientes)
Wither completo (`05a29f0`): montaje en T, IA, calaveras, barra de jefe, red, modelo/texturas libres. Estructuras (`7020ce7`): aldeas, templo del desierto, cabaña de bruja, pozo, minas. Comercio con aldeanos (`c754079`). Pesca (`54b9486`). Criaturas nuevas (`06f127c`). Marcos, cuadros y soporte de armadura. Tiempo (lluvia/tormenta/nieve). Pociones, atril, caldero. Nether, End, dragón. Vagonetas y raíles. Encantamientos y yunque. Armadura, experiencia. Entidades de bloque (carteles, estandartes, cabezas, cofres). Creativo con pestañas. Rendimiento estructural (arena de mallas, oclusión, adaptativo). Controles táctiles (joystick fijo). Multijugador protocolo 47 (LAN, servidor, interop con mineflayer).
CI: verde en comercio, estructuras y Wither (el Interop del Wither falló una vez y volvió a pasar en el commit siguiente). El del faro (primera mitad) estaba en marcha al retomar.

## 3. Pendiente (orden del plan)
1. **Mapas** (tarea #75): objeto, datos 128x128, mostrar en marco, paquete 0x34.
2. Camas que explotan en el Nether; fuegos artificiales; icono de caña "lanzada".
3. **Paridad multijugador**: dimensiones y portales en servidor, tiempo/clima en servidor dedicado, invitados lanzando objetos. Guardianes: reaparición en el monumento.
4. Sonidos oficiales en web (y sonidos del guardián, que no tiene ninguno).
5. **H5**: editor táctil de disposición, arrastrar/soltar, pulsación larga, inercia.
6. **C2.3 online-mode** (AES-CFB8 + RSA + login Microsoft): necesita un *client ID* de Azure aprobado por Mojang, solo el usuario puede obtenerlo. Documentar (C2.4).
7. README completo.
8. **Release v1.0.0** al final (sin betas): `release.yml` con .exe/zip, .deb, AppImage, tar.gz, zip web, `mcweb-server`, `mcweb-wsproxy`. Después recordar activar GitHub Pages (Settings > Pages > gh-pages / root): https://macorredor270.github.io/MC-WEB/

## 4. Límites conocidos
Aldeas en bosques se solapan con árboles; la capa de remolino de la armadura del Wither no se dibuja; apuntado lateral de las cabezas del Wither simplificado; el monumento no tiene trampas ni reaparición de guardianes y el templo de la jungla no tiene trampas ni palancas; los climas cálidos (desierto, jungla, mesa) son raros porque el generador reparte pocas zonas calientes; mapas sin empezar; un invitado con ventana de cofre abierta cuyo cofre se rompe deja puntero colgante (preexistente).

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
