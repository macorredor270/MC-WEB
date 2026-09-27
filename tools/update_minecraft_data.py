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


def pick(obj, keys):
    return {k: obj[k] for k in keys if k in obj}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    blocks = [pick(b, ["id", "name", "displayName", "hardness", "resistance", "stackSize", "diggable", "boundingBox",
                       "material", "transparent", "emitLight", "filterLight", "drops", "harvestTools", "variations"])
              for b in get("blocks")]
    (OUT / "blocks.json").write_text(json.dumps(blocks, indent=1) + "\n")
    biomes = [{k: b.get(k, 0) for k in ["id", "name", "displayName", "temperature", "rainfall", "color"]} for b in get("biomes")]
    (OUT / "biomes.json").write_text(json.dumps(biomes, indent=1) + "\n")
    items = [pick(i, ["id", "name", "displayName", "stackSize", "maxDurability", "variations"]) for i in get("items")]
    (OUT / "items.json").write_text(json.dumps(items, indent=1) + "\n")
    (OUT / "recipes.json").write_text(json.dumps(get("recipes"), separators=(",", ":")) + "\n")
    (OUT / "materials.json").write_text(json.dumps(get("materials"), indent=1) + "\n")
    (OUT / "blockCollisionShapes.json").write_text(json.dumps(get("blockCollisionShapes"), separators=(",", ":")) + "\n")
    print(f"blocks: {len(blocks)}, biomes: {len(biomes)}, items: {len(items)}")


if __name__ == "__main__":
    main()
