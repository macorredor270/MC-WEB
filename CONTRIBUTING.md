# Cómo contribuir

MC-WEB es una **reimplementación en sala limpia** de Minecraft 1.8 para navegador. Para que
el proyecto sea open source de verdad (MIT), todo el código tiene que ser nuestro.
Estas reglas no son opcionales.

## Fuentes permitidas

- **Comportamiento observable del juego**: jugar a Minecraft 1.8 y medir o describir lo que
  pasa (físicas, tiempos, recetas, generación de estructuras vista desde fuera…).
- **Documentación pública**: [minecraft.wiki](https://minecraft.wiki), incluida la
  documentación del protocolo de red que antes estaba en wiki.vg.
- **Formatos de datos**: NBT, Anvil (`.mca`), `level.dat`, blockstates y modelos JSON de los
  resource packs, `sounds.json`, `.mcmeta`… Se pueden leer para entender su estructura.
- **Tablas de datos con licencia libre**: por ejemplo
  [minecraft-data](https://github.com/PrismarineJS/minecraft-data) (MIT). Hay que indicar la
  licencia en un `NOTICE.md`.
- **Código de proyectos open source con licencia compatible** (MIT, Apache-2.0, BSD, CC0),
  citando el origen.

## Fuentes prohibidas

- **Código de Mojang** en cualquier forma: descompilado, desofuscado (MCP, Yarn, mappings
  oficiales), traducido a otro lenguaje o "reescrito mirándolo". Si has leído el código
  descompilado de una parte concreta, no implementes esa parte.
- **Código de Eaglercraft/EaglercraftX** ni de otros ports del cliente oficial, porque son
  obras derivadas del código de Mojang y además su licencia es "All Rights Reserved".
- **Assets de Mojang en el repo**: texturas, sonidos, modelos, idiomas, el `.jar`, mundos de
  ejemplo… El juego los carga en el navegador desde el `1.8.8.jar` que aporta cada usuario.

## Texturas propias

Las texturas integradas (el pack "libre") se dibujan por código en `src/assets/cc0_pack.cpp`
y se publican como CC0. No pueden ser copias ni retoques de las texturas de Mojang.

## Flujo de trabajo

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Estilo: C++20, el de los archivos que ya existen. Comentarios y mensajes en español.
Cada cambio de comportamiento lleva su test en `tests/`.

Opcional: para validar que nuestras tablas y el horneado de modelos coinciden con los assets
reales, descomprime `assets/` de tu `1.8.8.jar` en una carpeta **fuera del repo** y ejecuta:

```bash
MCWEB_ASSETS_DIR=/ruta/a/la/carpeta ./build/linux-debug/tests/mcweb_tests
```
