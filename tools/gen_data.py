#!/usr/bin/env python3
"""Genera src/data/generated/*.inc a partir de data/minecraft-data/*.json."""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "data" / "minecraft-data"
OUT = ROOT / "src" / "data" / "generated"

HEADER = "// Generado por tools/gen_data.py a partir de minecraft-data (MIT). No editar a mano.\n"


def cstr(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    blocks = json.loads((SRC / "blocks.json").read_text())
    lines = [HEADER]
    for b in blocks:
        hardness = b["hardness"] if b["hardness"] is not None else -1
        lines.append(
            "{%d, %s, %s, %sf, %sf, %d, %s, %s, %s, %d, %d},\n" % (
                b["id"], cstr(b["name"]), cstr(b["displayName"]), float(hardness), float(b["resistance"] or 0),
                b["stackSize"] or 0, "true" if b["diggable"] else "false",
                "true" if b["boundingBox"] == "block" else "false",
                "true" if b["transparent"] else "false", b["emitLight"] or 0, b["filterLight"] or 0))
    (OUT / "blocks.inc").write_text("".join(lines))

    biomes = json.loads((SRC / "biomes.json").read_text())
    lines = [HEADER]
    for b in biomes:
        lines.append("{%d, %s, %s, %sf, %sf, 0x%06X},\n" % (
            b["id"], cstr(b["name"]), cstr(b["displayName"]), float(b["temperature"]), float(b["rainfall"]), b["color"] or 0))
    (OUT / "biomes.inc").write_text("".join(lines))
    print(f"{len(blocks)} bloques, {len(biomes)} biomas -> {OUT}")


if __name__ == "__main__":
    main()
