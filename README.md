# MC-WEB

**Minecraft 1.8 reimplementado desde cero en C++20**, open source (MIT). Se compila de forma
nativa para **Linux y Windows** y el mismo código funciona en el **navegador** (WebAssembly + WebGL2).

![Captura con el pack libre integrado](docs/captura-pack-libre.png)
*Captura con el pack libre (CC0) que trae el juego. Con tu `1.8.8.jar` se ven las texturas originales.*

![Inventario de supervivencia con una receta en la rejilla de 2x2](docs/captura-supervivencia.png)
*Inventario de supervivencia: cuatro tablones en la rejilla de 2x2 dan una mesa de trabajo.*

## Qué es (y qué no es)

MC-WEB es una **reimplementación en sala limpia**: el juego, el motor y el servidor están
escritos desde cero usando Minecraft 1.8 como *especificación*. Se basa en el comportamiento
observable del juego, en la documentación pública de [minecraft.wiki](https://minecraft.wiki)
y en los formatos de datos públicos (modelos JSON, NBT, Anvil y el protocolo de red 1.8).

- **No contiene código de Mojang.** No se usa código descompilado ni mappings, y nada de
  Eaglercraft.
- **No contiene assets de Mojang.** Las texturas, la fuente y los sonidos se cargan en tiempo de
  ejecución desde **tu propio** `1.8.8.jar`. Si no lo tienes, el juego usa un pack libre (CC0)
  generado por código.

Las reglas completas están en [CONTRIBUTING.md](CONTRIBUTING.md).

## Estado

| Fase | Contenido | Estado |
|---|---|---|
| F0 | CMake, SDL3 + OpenGL 3.3 / WebGL2, Linux + Windows + web, tests, CI | ✅ |
| F1 | Ver el mundo: packs, modelos JSON, texture array, mallado en hilos, AO y luz suave, cielo, nubes, niebla, generador de terreno propio | ✅ |
| F2 | Jugar: físicas, romper/colocar, día/noche ✅ · servidor integrado con protocolo 1.8, fluidos que corren | 🟡 |
| F3 | Supervivencia: vida, hambre, inventario, crafteo, horno, 8 criaturas, sonido, partículas ✅ · más criaturas, cría, armaduras | 🟡 |
| F4 | Guardado en formato Anvil (compatible con mundos 1.8) | ⏳ |
| F5 | Multijugador: servidores 1.8 reales y servidor dedicado propio | ⏳ |
| F6 | Redstone, Nether/End, opciones, empaquetado | ⏳ |

Lo que ya funciona:
- **Carga de assets de Minecraft**:
  - En nativo busca solo `.minecraft/versions/1.8.x`; en web eliges el jar y se guarda en IndexedDB.
  - Lee blockstates y modelos JSON con herencia, rotaciones, `uvlock` y variantes con peso.
  - Mete todas las texturas en un texture array con mipmaps y aplica las animaciones de los
    `.mcmeta` (agua, lava…).
  - Tiñe hierba y hojas con los colormaps de bioma.
- **Mundo infinito** con generador propio basado en ruido Perlin:
  - Biomas: océanos, playas, ríos, llanuras, bosques, abedules, taiga, taiga nevada,
    tundra, desierto, sabana, pantanos y montañas.
  - Cuevas y cavernas con lava en el fondo, y vetas de menas.
  - Árboles, flores, hierba, cactus, caña de azúcar y nenúfares.
- **Luz de cielo y de bloque** con propagación entre chunks y actualizaciones incrementales.
- **Render**:
  - Mallado en varios hilos, con luz suave y oclusión ambiental por vértice.
  - Pasadas sólido, cutout y translúcido; agua con alturas por esquina.
  - Cielo con sol, luna con fases, estrellas y amanecer; nubes y niebla.
- **Modos supervivencia y creativo**:
  - Físicas del jugador con las constantes de 1.8: andar a 4,317 bloques/s, correr, saltar
    1,25 bloques, agacharse sin caerse por los bordes, nadar, escalones de 0,6 y daño por caída.
  - En creativo: volar (dos veces espacio), romper al instante y todos los bloques en el inventario.
  - Cambio de modo en el menú de pausa (Esc).
- **Romper y colocar** con los tiempos de 1.8 (dureza, herramienta, bajo el agua, en el aire),
  grietas, contorno del bloque apuntado, drops (semillas, pedernal, manzanas, brotes…) y objetos
  que caen, giran y se recogen.
- **Colocación como en 1.8**: troncos según la cara, antorchas en paredes, hornos mirando al
  jugador, plantas de dos bloques; las flores y antorchas sin soporte caen y la arena y la grava
  caen al quitar lo de debajo.
- **Inventario completo**: barra rápida, inventario con rejilla de 2x2, mesa de trabajo (3x3,
  recetas con forma y sin forma de 1.8), horno con combustible y progreso, inventario creativo
  con desplazamiento y tooltips. Clic, clic derecho (partir montón), mayús+clic (mover rápido) y
  tirar objetos (Q).
- **Vida, hambre, saturación y aire**, regeneración, comer (mantener clic derecho), durabilidad de
  herramientas, pantalla de muerte y reaparecer (se sueltan los objetos).
- **Criaturas**: cerdo, vaca, oveja, gallina, zombi, esqueleto, creeper y araña.
  - IA: los animales pasean, huyen si les pegas y siguen a quien lleva su comida (zanahoria, trigo,
    semillas); las ovejas comen hierba y se esquilan; las gallinas ponen huevos. Los zombis
    persiguen, las arañas trepan y saltan, los esqueletos disparan flechas y el creeper se enciende
    y explota.
  - Aparecen como en 1.8: animales al generarse el mundo; monstruos de noche o en cuevas oscuras.
    Zombis y esqueletos arden al sol.
  - Combate con el daño de cada arma de 1.8, críticos, retroceso y botín (chuletas, cuero, lana,
    plumas, huesos, pólvora, hilo...).
  - **Arco y flechas**: mantén el clic derecho para tensarlo (1 s = a tope, con el zoom y el
    frenado de 1.8) y suelta para disparar; la potencia, el daño (y el crítico a plena potencia),
    la caída de la flecha y la dispersión son los de 1.8. Gasta una flecha y 1 de durabilidad
    (nada de eso en creativo), y las flechas clavadas se recogen. En táctil, mantén el dedo.
    Abatir a un esqueleto desde 50 bloques da el logro *Francotirador*.
  - Modelos propios hechos a partir de la disposición estándar de las texturas: con tu jar se ven
    con sus texturas originales; sin él, con las del pack libre.
- **Sonido sintetizado**: romper y pisar según el material, criaturas, explosiones, arco, comer...
  Se genera todo al arrancar (1.8 no trae los sonidos en el jar), con sonido posicional.
- **Partículas**: trozos de bloque al romper, polvo al correr, humo, chispas de crítico, llamas.
- **Ajustes** (Esc → Ajustes), con páginas y se guardan:
  - *Gráficos*: calidad (baja/media/alta/ultra), distancia hasta 32 chunks, resolución 3D,
    FPS máximos, VSync, campo de visión, brillo, luz suave, hojas detalladas o rápidas, nubes,
    niebla, mipmaps, partículas, distancia de las criaturas y balanceo al andar.
  - *Música y sonidos*: volumen general, música (piano generativo), bloques, criaturas,
    jugador, interfaz y subtítulos.
  - *Controles*: sensibilidad, invertir ratón, correr/agacharse manteniendo o alternando, salto
    automático, y en táctil: sensibilidad, tamaño y opacidad de los botones y joystick fijo o
    flotante. *Teclas*: todas se pueden cambiar.
  - *Partida*: modo, dificultad (pacífica, fácil, normal, difícil, como en 1.8), ciclo de día y
    noche, hora, aparición de criaturas y conservar el inventario.
  - *Interfaz*: escala, cámara en primera o tercera persona, mano, punto de mira, FPS,
    coordenadas y subtítulos.
- **Pantalla de depuración (F3)** con la fuente del juego.

## Rendimiento

- **Tantos FPS como dé la pantalla**: 60, 120, 144 Hz... El juego va por ticks (20 por segundo) e
  interpola, así que la animación es fluida a cualquier frecuencia.
- **Todos los núcleos, también en el navegador**: en web, la generación del mundo y el mallado van
  a varios Web Workers (sin necesidad de COOP/COEP); en nativo, a hilos.
- **Hilo principal ligero**: cada columna de chunk se dibuja con una sola llamada y lo que llega de
  los workers se sube a la GPU con un presupuesto por frame (una cuarta parte del frame).
- `mcweb-bench` mide lo que cuesta generar y mallar; `--log-perf` escribe fps y CPU cada segundo.

**iPhone con pantalla de 120 Hz (ProMotion)**: Safari limita las páginas a 60 Hz. Para jugar a
120 Hz, desactiva *Prefer Page Rendering Updates near 60fps* en Ajustes → Apps → Safari → Avanzado
→ Feature Flags. La página te dice a cuántos Hz va tu pantalla.

## Jugar

### Linux / Windows

Descarga el ejecutable de los artefactos de la [CI](../../actions) o compílalo (ver abajo).
Si tienes Minecraft instalado y has abierto la 1.8.8 al menos una vez, el juego encuentra el
jar solo. Si no, pásaselo:

```bash
./mcweb --jar /ruta/a/1.8.8.jar
```

### Navegador

```bash
python3 web/serve.py build/web/apps/mcweb     # abre http://localhost:8080
```

El servidor añade las cabeceras COOP/COEP que necesitan los hilos (SharedArrayBuffer).
Si la página se sirve sin esas cabeceras (GitHub Pages, cualquier hosting estático), carga sola
la variante sin hilos (`mcweb-st.js`): va igual, pero carga el mundo algo más despacio.
Al abrir la página eliges tu `1.8.8.jar` (o juegas con el pack libre).

### Móvil y tablet

En pantallas táctiles la página activa sola los controles táctiles (al estilo de la edición de
bolsillo):
- **Joystick** (abajo a la izquierda): moverse. Llevándolo al borde hacia delante, corres.
- **▲**: saltar. Dos toques seguidos en creativo: volar; manteniéndolo, subir.
- **▼**: agacharse (se queda activado). Volando: bajar.
- **Arrastrar** en el resto de la pantalla: mirar.
- **Tocar** un bloque: colocar o usar (abrir mesa de trabajo u horno). Tocar una criatura: golpearla
  (con tijeras, esquilar ovejas).
- **Mantener** el dedo sobre un bloque: romperlo (sobre una criatura: seguir golpeando). Con comida
  en la mano: comer.
- **Barra rápida**: tocar una casilla para elegirla. **⋯** abre el inventario y **II** la pausa.
- **En los inventarios**: tocar = clic, mantener = clic derecho, arrastrar = desplazar la lista
  del creativo, **✕** para cerrar.

Funcionan también en portátiles táctiles con Windows o Linux (y se fuerzan con `--touch`).
Para usar tus texturas en el móvil, copia el `1.8.8.jar` a iCloud Drive, Google Drive o la
carpeta de descargas y elígelo en la página.

### Controles

| Tecla | Acción |
|---|---|
| Clic | Capturar el ratón |
| WASD / ratón | Moverse / mirar |
| Espacio | Saltar (dos veces seguidas: volar en creativo) |
| Mayús | Agacharse (volando: bajar) |
| W dos veces / Ctrl | Correr |
| Clic izquierdo | Romper (mantener) · golpear criaturas |
| Clic derecho | Colocar, usar, comer, esquilar (tijeras) |
| Clic central | Coger el bloque apuntado (creativo) |
| 1–9 / rueda | Casilla de la barra rápida |
| E | Inventario |
| Q / Ctrl+Q | Tirar un objeto / el montón |
| Esc | Menú de pausa y Ajustes (también en el navegador) |
| F5 | Cámara: primera persona, detrás, delante |
| F1 | Ocultar la interfaz |
| F3 | Pantalla de depuración |
| RePág / AvPág | Distancia de render |
| F2 | Captura de pantalla |
| F11 | Pantalla completa |

Opciones de línea de comandos: `mcweb --help` (por ejemplo `--mode creative`). En web se pasan
como parámetros de la URL, por ejemplo `index.html?seed=1234&rd=10&mode=creative`; la página
también tiene un selector de modo.

## Skins

En el menú principal, **Skins**: elige una de serie (Steve y Alex, que salen del paquete de
texturas activo) o sube tu PNG de 64x64 o 64x32. El personaje se ve girando y la skin queda
elegida al momento; **Capas...** enciende o apaga el sombrero, la chaqueta, las mangas y las
perneras, como la pantalla de personalizar skin de 1.8.

- Las skins de 64x32 (las antiguas) se convierten solas, y los **brazos finos** (el modelo Alex)
  se detectan al subirlas; se pueden cambiar con el botón *Brazos*.
- Tus skins se guardan en la carpeta `skins/` de los datos de usuario (en la web, en el
  navegador). También vale copiar ahí un PNG a mano.
- En multijugador, los jugadores de MC-WEB ven tu skin: se manda por un canal propio
  (`MCWEB|Skin`) que solo se usa con servidores de MC-WEB. Un servidor de Minecraft 1.8 normal no
  recibe nada de eso (y no tiene cómo pasar skins que no estén en los servidores de Mojang).
  Quien no manda skin se ve con Steve o Alex según su UUID, como en 1.8.

## Multijugador

MC-WEB habla el protocolo de Minecraft 1.8 (versión 47) en modo offline: se puede jugar con
otros MC-WEB y con el Minecraft 1.8 oficial.

- **Abrir en LAN** (menú de pausa, versión de escritorio): tu partida se convierte en servidor.
  Se anuncia en la red local, como en 1.8, y la pantalla te dice la dirección para compartir.
  Los invitados se guardan con el mundo (`playerdata/`).
- **Multijugador** (menú principal): servidores guardados con su ping, partidas de la LAN,
  conexión directa y tu nombre de jugador.
- **Servidor dedicado** (`mcweb-server`, sin ventana): lee `server.properties` (puerto, MOTD,
  modo, dificultad, semilla, tipo de mundo, distancia de visión, lista blanca). Tiene consola con
  `help`, `list`, `say`, `kick`, `time`, `whitelist`, `save-all` y `stop`.

  ```bash
  mcweb-server --dir mi-servidor          # crea mi-servidor/server.properties y el mundo
  ```
- **Desde el navegador**: la web no puede abrir conexiones TCP, así que usa el puente
  `mcweb-wsproxy`. Arráncalo en tu equipo y, en Multijugador, pon `ws://localhost:25500` como
  proxy (es el valor por defecto).

  ```bash
  mcweb-wsproxy                            # ws://127.0.0.1:25500, solo este equipo
  mcweb-wsproxy --listen 0.0.0.0:25500 --allow mi.servidor.org:25565   # para otros, limitado
  ```

Las cuentas premium (servidores con `online-mode=true`) aún no están soportadas: necesitan
iniciar sesión con Microsoft.

## Compilar

Necesitas CMake ≥ 3.25, Ninja y un compilador de C++20. Las dependencias se descargan solas y
quedan fijadas a una versión: SDL3, glm, miniz, nlohmann/json, stb y doctest.

```bash
# Linux (dependencias de sistema de SDL3: ver .github/workflows/ci.yml)
cmake --preset linux-release && cmake --build --preset linux-release
ctest --preset linux-release

# Windows (Visual Studio 2022)
cmake --preset windows-msvc && cmake --build --preset windows-msvc

# Windows desde Linux (MinGW-w64)
cmake --preset windows-mingw && cmake --build --preset windows-mingw

# Web (con emsdk activado: source emsdk/emsdk_env.sh)
cmake --preset web && cmake --build --preset web
```

Para validar las tablas contra tus assets reales, descomprime `assets/` de tu jar **fuera del
repo** y ejecuta los tests con `MCWEB_ASSETS_DIR=/esa/carpeta`.

## Arquitectura

```
src/core      buffers/VarInt, zip, hilos, rutas, log
src/data      bloques, ítems, recetas, herramientas, drops, formas de colisión, biomas y el mapa
              estado→blockstate de 1.8 (tablas de minecraft-data, MIT)
src/world     chunks, ruido, generador de terreno, motor de luz
src/assets    packs (jar/zip/carpeta/CC0), modelos JSON de bloques e ítems, texturas, pack libre generado
src/game      reglas del juego sin gráficos: jugador y físicas, criaturas e IA, combate, explosiones,
              inventario, menús, crafteo, horno, romper/colocar, drops (probado con tests)
src/save      NBT, regiones Anvil, level.dat, playerdata y estadísticas (formato de 1.8)
src/net       protocolo 1.8 (47): tramas, chunks, cliente, servidor, sockets, LAN, WebSocket
src/client    mallador, renderer (GL 3.3 / WebGL2), modelos de criaturas, partículas, sonido
              sintetizado, cielo, HUD e inventarios, menús, opciones, controles táctiles, Web Workers
apps/worker   módulo de los Web Workers del build web (genera y malla sin gráficos)
apps/bench    medidor de rendimiento de la carga del mundo
apps/mcweb    punto de entrada (SDL3 main callbacks)
apps/mcweb-server   servidor dedicado sin ventana
apps/mcweb-wsproxy  puente WebSocket <-> TCP para jugar en servidores desde la web
web/          página del build web y servidor local
```

## Licencia

[MIT](LICENSE). Minecraft es una marca de Mojang AB; este proyecto no está afiliado ni
respaldado por Mojang ni por Microsoft.
