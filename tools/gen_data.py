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


def load(name):
    return json.loads((SRC / name).read_text())


def write(name, lines):
    (OUT / name).write_text(HEADER + "".join(lines))


def ingredient(v):
    """(id, meta) de un ingrediente; meta -1 = cualquier variante."""
    if v is None:
        return (0, 0)
    if isinstance(v, int):
        return (v, -1)
    return (v["id"], v.get("metadata", -1))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    blocks = load("blocks.json")
    materials = sorted({b.get("material") or "" for b in blocks})

    lines = []
    for b in blocks:
        hardness = b["hardness"] if b.get("hardness") is not None else -1
        lines.append("{%d, %s, %s, %sf, %sf, %d, %s, %s, %s, %d, %d, %s},\n" % (
            b["id"], cstr(b["name"]), cstr(b["displayName"]), float(hardness), float(b.get("resistance") or 0),
            b.get("stackSize") or 0, "true" if b.get("diggable") else "false",
            "true" if b.get("boundingBox") == "block" else "false",
            "true" if b.get("transparent") else "false", b.get("emitLight") or 0, b.get("filterLight") or 0,
            cstr(b.get("material") or "")))
    write("blocks.inc", lines)

    biomes = load("biomes.json")
    write("biomes.inc", ["{%d, %s, %s, %sf, %sf, 0x%06X},\n" % (
        b["id"], cstr(b["name"]), cstr(b["displayName"]), float(b["temperature"]), float(b["rainfall"]), b["color"] or 0)
        for b in biomes])

    items = load("items.json")
    write("items.inc", ["{%d, %s, %s, %d, %d},\n" % (
        i["id"], cstr(i["name"]), cstr(i["displayName"]), i.get("stackSize") or 64, i.get("maxDurability") or 0)
        for i in items])

    # Constantes con el id de cada ítem por nombre (ItemId::stick, ItemId::wooden_pickaxe...)
    seen = set()
    lines = []
    for i in items:
        if i["name"] in seen:
            continue
        seen.add(i["name"])
        lines.append("inline constexpr int %s = %d;\n" % (i["name"], i["id"]))
    write("item_ids.inc", lines)

    lines = []
    for src in (items, blocks):
        for i in src:
            for v in i.get("variations") or []:
                lines.append("{%d, %d, %s},\n" % (i["id"], v["metadata"], cstr(v["displayName"])))
    write("variations.inc", lines)

    lines = []
    for b in blocks:
        for tool in sorted((b.get("harvestTools") or {}).keys(), key=int):
            lines.append("{%d, %s},\n" % (b["id"], tool))
    write("harvest_tools.inc", lines)

    lines = []
    for b in blocks:
        for d in b.get("drops") or []:
            drop = d["drop"]
            did, dmeta = (drop, -1) if isinstance(drop, int) else (drop["id"], drop.get("metadata", 0))
            lines.append("{%d, %d, %d, %sf, %sf},\n" % (b["id"], did, dmeta, float(d.get("minCount", 1)), float(d.get("maxCount", d.get("minCount", 1)))))
    write("drops.inc", lines)

    lines = []
    for mat, tools in load("materials.json").items():
        for tool, mult in sorted(tools.items(), key=lambda kv: int(kv[0])):
            lines.append("{%s, %s, %sf},\n" % (cstr(mat), tool, float(mult)))
    write("tool_speeds.inc", lines)

    # Recetas: con forma (ancho x alto, celdas fila a fila) y sin forma (lista de ingredientes)
    shaped, shapeless = [], []
    for result_id, recs in load("recipes.json").items():
        for r in recs:
            res = r["result"]
            head = "%d, %d, %d" % (res["id"], res.get("metadata", 0), res.get("count", 1))
            if "inShape" in r:
                rows = r["inShape"]
                h = len(rows)
                w = max(len(row) for row in rows)
                cells = []
                for row in rows:
                    for x in range(w):
                        cid, cmeta = ingredient(row[x] if x < len(row) else None)
                        cells.append("{%d, %d}" % (cid, cmeta))
                cells += ["{0, 0}"] * (9 - len(cells))
                shaped.append("{%s, %d, %d, {%s}},\n" % (head, w, h, ", ".join(cells)))
            elif "ingredients" in r:
                ings = [ingredient(v) for v in r["ingredients"]]
                cells = ["{%d, %d}" % c for c in ings] + ["{0, 0}"] * (9 - len(ings))
                shapeless.append("{%s, %d, {%s}},\n" % (head, len(ings), ", ".join(cells)))
    write("recipes_shaped.inc", shaped)
    write("recipes_shapeless.inc", shapeless)

    # Formas de colisión: tabla de formas (cajas) y, por bloque, la forma de cada metadata
    cs = load("blockCollisionShapes.json")
    shape_ids = sorted(cs["shapes"].keys(), key=int)
    boxes, index = [], []
    for sid in shape_ids:
        start = len(boxes)
        for bx in cs["shapes"][sid]:
            boxes.append("{%s},\n" % ", ".join("%sf" % float(v) for v in bx))
        index.append("{%s, %d, %d},\n" % (sid, start, len(boxes) - start))
    write("collision_boxes.inc", boxes)
    write("collision_shapes.inc", index)
    name_to_id = {b["name"]: b["id"] for b in blocks}
    lines = []
    for name, v in cs["blocks"].items():
        if name not in name_to_id:
            continue
        metas = v if isinstance(v, list) else [v] * 16
        metas = (metas + [metas[-1]] * 16)[:16]
        lines.append("{%d, {%s}},\n" % (name_to_id[name], ", ".join(str(m) for m in metas)))
    write("collision_blocks.inc", lines)
    print(f"{len(blocks)} bloques, {len(items)} ítems, {len(shaped)} recetas con forma, {len(shapeless)} sin forma, "
          f"{len(shape_ids)} formas de colisión -> {OUT}")


if __name__ == "__main__":
    main()
