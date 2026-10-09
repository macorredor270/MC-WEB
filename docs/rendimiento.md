# Rendimiento

Este documento recoge **cómo se mide** y **qué se ha conseguido**. Los números de esta máquina de
pruebas (4 núcleos, sin GPU, OpenGL por software con Mesa/llvmpipe) sirven para comparar CPU y número
de llamadas de dibujo antes y después de un cambio; **no** reflejan una GPU de verdad. Para eso, el
propio juego enseña sus números (F3) y escribe `fps` y CPU por segundo con `--log-perf`.

## Cómo medir

- Juego (nativo): `mcweb --cc0 --seed 42 --rd 12 --pos 0,90,0 --mode creative --time 6000 --freeze-time --log-perf`
  escribe cada segundo `perf: N fps, CPU X ms (peor Y), chunks, gen, malla, dibujadas`.
- Banco de pruebas de generación y mallado (sin ventana): `mcweb-bench [radio] [jar]`.
- F3 en el juego: fps, CPU por frame, chunks, llamadas de dibujo y secciones visibles.

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
