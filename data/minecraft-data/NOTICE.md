# minecraft-data

`blocks.json` y `biomes.json` son una versión recortada de las tablas de Minecraft 1.8 de
[PrismarineJS/minecraft-data](https://github.com/PrismarineJS/minecraft-data)
(licencia MIT, Copyright (c) 2015 PrismarineJS). Son tablas de datos (ids, nombres, dureza,
luz, biomas), no código ni assets de Mojang.

Se regeneran con `python3 tools/update_minecraft_data.py`. Después hay que ejecutar
`python3 tools/gen_data.py`, que produce `src/data/generated/*.inc`.
