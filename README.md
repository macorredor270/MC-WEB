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
| F3 | Supervivencia: vida, hambre, inventario, crafteo, horno ✅ · mobs, sonido, partículas | 🟡 |
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
- **Pantalla de depuración (F3)** con la fuente del juego.

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
- **Tocar** un bloque: colocar o usar (abrir mesa de trabajo u horno).
- **Mantener** el dedo sobre un bloque: romperlo. Con comida en la mano: comer.
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
| Ctrl | Correr |
| Clic izquierdo | Romper (mantener) |
| Clic derecho | Colocar, usar, comer |
| Clic central | Coger el bloque apuntado (creativo) |
| 1–9 / rueda | Casilla de la barra rápida |
| E | Inventario |
| Q / Ctrl+Q | Tirar un objeto / el montón |
| Esc | Menú de pausa: modo de juego, distancia de visión, hora |
| F3 | Pantalla de depuración |
| RePág / AvPág | Distancia de render |
| F2 | Captura de pantalla |
| F11 | Pantalla completa |

Opciones de línea de comandos: `mcweb --help` (por ejemplo `--mode creative`). En web se pasan
como parámetros de la URL, por ejemplo `index.html?seed=1234&rd=10&mode=creative`; la página
también tiene un selector de modo.

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
src/game      reglas del juego sin gráficos: jugador y físicas, inventario, menús, crafteo, horno,
              romper/colocar, drops, objetos en el suelo (probado con tests)
src/client    mallador, renderer (GL 3.3 / WebGL2), cielo, HUD e inventarios, controles táctiles
apps/mcweb    punto de entrada (SDL3 main callbacks)
web/          página del build web y servidor local
```

## Licencia

[MIT](LICENSE). Minecraft es una marca de Mojang AB; este proyecto no está afiliado ni
respaldado por Mojang ni por Microsoft.
