#!/usr/bin/env python3
"""Descarga las tablas de Minecraft 1.8 de PrismarineJS/minecraft-data (MIT) y guarda una
versión recortada en data/minecraft-data/. Son datos, no código de Mojang."""
import json
import pathlib
import urllib.request

BASE = "https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.8"
OUT = pathlib.Path(__file__).resolve().parent.parent / "data" / "minecraft-data"


def get(name):
    with urllib.request.urlopen(f"{BASE}/{name}.json") as r:
        return json.load(r)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    keys = ["id", "name", "displayName", "hardness", "resistance", "stackSize", "diggable",
            "boundingBox", "material", "transparent", "emitLight", "filterLight"]
    blocks = [{k: b.get(k) for k in keys} for b in get("blocks")]
    (OUT / "blocks.json").write_text(json.dumps(blocks, indent=1) + "\n")
    bkeys = ["id", "name", "displayName", "temperature", "rainfall", "color"]
    biomes = [{k: b.get(k, 0) for k in bkeys} for b in get("biomes")]
    (OUT / "biomes.json").write_text(json.dumps(biomes, indent=1) + "\n")
    print(f"blocks: {len(blocks)}, biomes: {len(biomes)}")


if __name__ == "__main__":
    main()
