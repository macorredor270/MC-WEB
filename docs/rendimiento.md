# Rendimiento

Este documento recoge **cómo se mide** y **qué se ha conseguido**. Los números de esta máquina de
pruebas (4 núcleos, sin GPU, OpenGL por software con Mesa/llvmpipe) sirven para comparar CPU y número
de llamadas de dibujo antes y después de un cambio; **no** reflejan una GPU de verdad. Para eso, el
propio juego enseña sus números (F3) y escribe `fps` y CPU por segundo con `--log-perf`.

## Cómo medir

- Juego (nativo): `mcweb --cc0 --seed 42 --rd 12 --pos 0,90,0 --mode creative --time 6000 --freeze-time --log-perf`
  escribe cada segundo los fps, el tiempo de CPU por fase del frame (`red`, `carga`, `tick`, `prep`, `cielo`,
  `terreno`, `entid`, `transl`, `mano`, `ui` y `swap`), el de GPU (si el sistema lo permite), chunks, llamadas
  de dibujo, quads y secciones dibujadas.
- Medición con recorrido fijo: `mcweb ... --bench 8 --bench-spin` espera a que el mundo esté listo, calienta un
  segundo, mide 8 segundos con la cámara dando vueltas (90 grados por segundo, sin criaturas nuevas), escribe el
  resumen (`bench:` para leer y `bench-json:` para scripts) y sale. `tools/bench/render-bench.sh` lo lanza en
  dos escenas (`bosque` en la superficie y `cueva`, un hueco grande bajo tierra que da
  `mcweb-bench --find-cave 42`) a varias distancias y escribe la tabla en markdown.
- Banco de pruebas de generación y mallado (sin ventana): `mcweb-bench [radio] [jar]`.
- F3 en el juego: fps, CPU por frame y GPU, los tiempos por fase, llamadas de dibujo, quads y secciones visibles.
  El tiempo de GPU usa consultas de tiempo de OpenGL 3.3 y, en el navegador, la extensión
  `EXT_disjoint_timer_query_webgl2` (Chrome de escritorio; muchos móviles y Safari no la tienen: sale `n/d`).

## Lo que se pide al sistema (uso de los componentes)

| Qué | Dónde | Efecto |
|---|---|---|
| GPU potente (Windows) | `NvOptimusEnablement` y `AmdPowerXpressRequestHighPerformance` en `apps/mcweb/main.cpp` | En portátiles con dos GPU, el controlador abre el juego en la dedicada |
| GPU potente (navegador) | `powerPreference: 'high-performance'` al crear el contexto WebGL2 en `web/index.html` | Chrome y Edge usan la GPU dedicada |
| GPU potente (Linux) | `DRI_PRIME=1`, o `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia` | Lo decide el sistema, no el programa |
| LTO / LTCG | `MCWEB_LTO` (activado por defecto en Release) | El compilador inlina entre módulos (mallado, luz, física) |
| WebGL sin comprobación de errores | `-sGL_TRACK_ERRORS=0` en el build web | Cada llamada a GL deja de consultar `getError` |

## Historial

### 1. LTO en Release y GPU potente (2026-10-09)

`mcweb-bench 8` (289 columnas, GCC 13, Release `-O3`; media de 3 ejecuciones):

| Paso | Antes | Después |
|---|---|---|
| Generar | 1,91 ms por columna | 0,92 ms por columna |
| Insertar y coser luz | 0,04 ms por columna | 0,04 ms por columna |
| Mallar | 0,26 ms por sección | 0,26 ms por sección |

El "antes" es el binario sin LTO del 28 de septiembre; la generación se beneficia de inlinar el ruido
entre módulos y el mallado ya estaba bien inlinado. El tamaño del `.wasm` baja de 3,31 a 3,27 MB aunque
el juego ha crecido (armadura, experiencia, encantamientos).

Giro táctil: antes se aplicaba en el tick de 20 Hz, ahora en cada frame. Prueba en el navegador
(`tools/e2e/touch-look.mjs`): un arrastre de 198 px gira la cámara 0,893 rad (esperado 0,891).

### 2. Medición y línea base (2026-10-09)

Escenas con la semilla 42, creativo y mediodía, ventana de 480x270, Release con LTO, **OpenGL por software**
(Mesa llvmpipe, 4 núcleos): los milisegundos de GPU y de "terreno" son de la CPU rasterizando, no de una GPU de
verdad, así que solo sirven para comparar antes y después de un cambio. Lo que sí es independiente de la
máquina son las llamadas de dibujo y los quads que se mandan.

| escena | rd | fps | ms media | p95 | p99 | terreno (CPU ms) | GPU ms | llamadas | quads | secciones |
|---|---|---|---|---|---|---|---|---|---|---|
| bosque | 8 | 15.7 | 63.6 | 87.2 | 92.6 | 47.4 | 52.8 | 141 | 155086 | 311/992 |
| bosque | 16 | 4.9 | 205.4 | 269.5 | 317.2 | 172.5 | 203.9 | 533 | 552439 | 1087/3664 |
| cueva | 8 | 16.8 | 59.5 | 85.0 | 94.9 | 46.6 | 53.9 | 79 | 224278 | 322/1030 |
| cueva | 16 | 5.6 | 178.7 | 258.6 | 368.3 | 150.5 | 174.2 | 361 | 739939 | 1146/3790 |

Lo que enseña: bajo tierra se mandan **más** quads que en la superficie (casi todo está tapado por la roca y
se dibuja igualmente), y cada llamada de dibujo viene con otras dos (`glUniform3f` y `glBindVertexArray`): a
distancia 16 son más de 1.500 llamadas a GL por frame, que en WebGL cuestan CPU en el hilo principal. Los dos
problemas los atacan la arena de mallas con multi-draw (menos llamadas) y la oclusión (menos quads).

### 3. Arena de mallas con multi-draw (2026-10-09)

Antes, cada columna de 16 secciones tenía su propio buffer (y VAO) por pasada; cambiar **una** sección volvía a
crear y copiar la columna entera, y dibujar el terreno era, por columna y pasada, un `glUniform3f`, un
`glBindVertexArray` y un `glDrawElements`. Ahora las mallas de todas las secciones viven en unas pocas páginas de
buffer de 10 MB (`QuadAllocator` reparte el sitio con el mejor ajuste y funde los huecos al liberar), cada
vértice lleva el hueco de su sección (`ChunkVertex::slot`, 16 bits, en el sitio de los dos bytes que sobraban) y
el shader saca de ahí el origen de la sección de una textura de enteros (`texelFetch`), restando la cámara con
parte entera y fraccionaria. Dibujar es **una llamada por página** con todos los tramos visibles
(`glMultiDrawElements`; en el navegador `WEBGL_multi_draw`, y si el navegador no la tiene, un bucle de
`glDrawElements` sin cambiar uniformes ni VAO). Cambiar una sección es un `glBufferSubData` de esa sección.

Mismas escenas y mismo equipo que en la línea base (llvmpipe: los milisegundos no cambian, porque ahí manda la
CPU rasterizando; lo que cambia es el trabajo de CPU del hilo que manda las llamadas, que en WebGL es caro):

| escena | rd | llamadas antes | llamadas ahora | GL por frame antes (3 por llamada) | GL por frame ahora |
|---|---|---|---|---|---|
| bosque | 8 | 141 | 19 | 423 | 19 + enlaces de VAO |
| bosque | 16 | 533 | 49 | 1.599 | 49 + enlaces de VAO |
| cueva | 8 | 79 | 6 | 237 | 6 + enlaces de VAO |
| cueva | 16 | 361 | 39 | 1.083 | 39 + enlaces de VAO |

En el navegador (Chromium, `tools/e2e/terrain-draw.mjs`): 2 a 5 llamadas por frame a distancia 4, con multi-draw.
La imagen no cambia: con una cámara fija y la escena limpia (`--fixed-cam`, que además congela agua y lava) las
capturas del renderizador viejo y del nuevo difieren en menos de un 0,4 % de píxeles, todos en bordes (redondeo
de coma flotante).

### 4. Oclusión por grafo de visibilidad (2026-10-09)

El mallador calcula, para cada sección, qué pares de caras se ven entre sí a través de los bloques que no tapan la
vista (relleno por regiones de los 16x16x16 bloques: 15 bits, `MeshOutput::visibility`, que viaja también por los
Web Workers). Cada frame se recorre a lo ancho desde la sección de la cámara (`SectionTraversal`, en
`client/visibility.h`): se pasa a la vecina solo si la sección actual conecta alguna cara por la que se entró con
la de salida, nunca se da un paso hacia la cámara (un rayo no vuelve atrás) y solo se entra en secciones dentro de
la pirámide de visión y de la distancia. Solo se dibuja lo que se alcanza. Es conservador: una sección que aún no se
conoce o está vacía deja pasar la vista. Se puede apagar en Ajustes > Gráficos ("Ocultar lo tapado") o con
`--no-occlusion`.

Mismas escenas y mismo equipo que antes (llvmpipe: aquí el tiempo es de la CPU rasterizando, en una GPU de verdad
el efecto se nota en los vértices y en las llamadas):

| escena | rd | quads antes | quads ahora | fps antes | fps ahora | oclusión (ms) |
|---|---|---|---|---|---|---|
| bosque | 8 | 155.086 | 108.069 | 15,7 | 18,3 | 0,14 |
| bosque | 16 | 552.439 | 421.246 | 4,9 | 6,2 | 0,93 |
| cueva | 8 | 224.278 | 24.630 | 16,8 | 65,5 | 0,04 |
| cueva | 16 | 739.939 | 30.552 | 5,6 | 46,4 | 0,11 |

En la superficie el terreno es abierto y se ahorra un 25 %; bajo tierra casi todo está tapado y se dibuja un 90 %
menos. Comprobación de que no desaparece nada que se vea: la misma vista con y sin oclusión (cámara fija,
`--fixed-cam`) sale **idéntica, píxel a píxel**, en 11 de 13 vistas (cuevas, superficie en cuatro direcciones,
aérea y orilla); las dos que difieren son con la cámara dentro de la roca, donde sin oclusión se ve "a través" de
ella por el recorte de caras traseras (y con oclusión, no).

