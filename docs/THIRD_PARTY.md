# Código de terceros

## Cubiomes (MIT)

La generación de **biomas** y del **relieve base** del Overworld (`src/world/legacy_gen.h` y `.cpp`) sigue el
algoritmo de Minecraft 1.8 tal como lo reproduce [Cubiomes](https://github.com/Cubitect/cubiomes), una
biblioteca independiente de Cubitect. Es una reescritura en C++ para la arquitectura de este proyecto, no una
copia literal. Cubiomes cubre: la pila de capas de biomas (continentes, zoom, clima, colinas, ríos, orillas), el
ruido de superficie de 1.8 (octavas de Perlin con las particularidades de Java), las alturas base y variación de
cada bioma y las reglas de posición de las estructuras (aldeas, templos, monumentos, minas, fortines).

Con la misma semilla, los biomas coinciden con los del juego original: `tests/test_legacy_gen.cpp` repite la prueba de
referencia de Cubiomes (4096 semillas, y un millón con `--no-skip`) y da el mismo hash.

No lo cubre Cubiomes y por tanto es propio de este proyecto: las cuevas y barrancos, las menas, los árboles y la
vegetación, la capa superficial del suelo y el contenido de todas las estructuras.

```
MIT License

Copyright (c) 2020 Cubitect

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
