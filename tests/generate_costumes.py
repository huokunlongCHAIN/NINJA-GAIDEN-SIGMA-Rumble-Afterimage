from pathlib import Path
import re
import sys

from inspect_assets import words

# Both Ashtar textures use the same vertex layout and draw descriptors.
OUTFITS = {
    86: ("Black", {33, 31}, {8, 10, 25}),
    122: ("Purple", set(), {10, 20, 22, 29}),
    3486: ("Blue", {15}, {9, 36, 39}),
    3579: ("Ashtar", set(), {8, 12, 14, 17, 19}),
    # Albedo 25 is the red curse pattern on the right arm and its torso seam.
    6221: ("Grip", {1, 25, 32}, {11, 17, 19, 21, 27}),
}
PARTS = {
    "chest": "Torso", "head": "Head", "hips": "Torso",
    "foot": "Foot", "fore": "Forearm", "hand": "Hand",
    "hip": "Thigh", "shin": "Calf", "shld": "UpperArm",
}


def align16(size):
    return (size + 15) & ~15


def draw_key(count, first, indices, vertices, stride):
    signature = (count, first, 0, align16(indices * 2), 57, 0,
                 align16(vertices * stride), stride, 0, *([0] * 21))
    key = 14695981039346656037
    for value in signature:
        key = ((key ^ value) * 1099511628211) & 0xffffffffffffffff
    return key


def read_meshes(data):
    result = {}
    for chunk in re.finditer(rb"ObjGeo\x00\x00", data):
        start = chunk.start()
        size, draws = words(data, start + 16, 2)
        table, _, declaration = words(data, start + 32, 3)
        decl = start + declaration
        if data[decl:decl + 8] != b"GeoDecl\0":
            continue
        node = re.search(rb"MOTryu[a-z0-9]([a-z]+)", data[start:start + size])
        name = node[1].decode() if node else ""
        part = PARTS.get(name, PARTS.get(name[1:], "Torso"))
        for i in range(draws):
            submesh = start + words(data, start + table + 4 * i)[0]
            _, material, group, _, first, count, textures = words(data, submesh, 7)
            layout = decl + words(data, decl + 0x30 + group * 4)[0]
            indices, vertices = words(data, layout + 12, 2)
            stride = words(data, layout + 0x34)[0]
            albedo = -1
            for j in range(textures):
                ref = submesh + words(data, submesh + 32 + j * 4)[0]
                role, _, index = words(data, ref, 3)
                if role == 0:
                    albedo = index
                    break
            key = draw_key(count, first, indices, vertices, stride)
            result.setdefault(key, (part, albedo, name == "chest", material))
    return result


def generate(models, legacy):
    sets = {index: read_meshes((models / f"{index:05}.tmc").read_bytes()) for index in OUTFITS}
    ashtar = read_meshes((models / "03324.tmc").read_bytes())
    assert ashtar == sets[3579]
    assert set(sets[86]) == legacy
    assert all(len(parts) >= 38 for parts in sets.values())
    lines = [
        "#pragma once",
        '#include "body_parts.h"',
        "#include <algorithm>",
        "",
        "// Draw signatures from the five playable costume geometries in databin.",
        "enum class Costume : unsigned { Black, Purple, Blue, Ashtar, Grip, Unknown };",
        "struct CostumeMesh {",
        "    uint64_t key;",
        "    BodyPart part;",
        "    GhostMaterial material;",
        "    bool torsoAnchor;",
        "};",
        "",
    ]
    for index, (name, skin, armor) in OUTFITS.items():
        lines.append(f"inline constexpr CostumeMesh {name}Meshes[]{{")
        for key, (part, albedo, anchor, _) in sorted(sets[index].items()):
            material = "Skin" if albedo in skin else "Armor" if albedo in armor else "Fabric"
            if name == "Grip" and albedo == 0:
                material = "GripAtlas"
            unique = all(key not in other for asset, other in sets.items() if asset != index)
            lines.append(f"    {{0x{key:016x}ull, BodyPart::{part}, GhostMaterial::{material}, {str(anchor and unique).lower()}}},")
        lines.extend(["};", ""])
    lines.extend([
        "struct CostumeDefinition {",
        "    const wchar_t* section;",
        "    const CostumeMesh* begin;",
        "    const CostumeMesh* end;",
        "};",
        "inline constexpr CostumeDefinition Costumes[]{",
        *[f'    {{L"Afterimage.{name}", {name}Meshes, {name}Meshes + std::size({name}Meshes)}},'
          for name, *_ in OUTFITS.values()],
        "};",
        "",
        "inline const CostumeMesh* FindCostumeMesh(Costume costume, uint64_t key) {",
        "    const auto index = static_cast<unsigned>(costume);",
        "    if (index >= std::size(Costumes)) return nullptr;",
        "    const auto& entry = Costumes[index];",
        "    auto item = std::lower_bound(entry.begin, entry.end, key,",
        "        [](const CostumeMesh& mesh, uint64_t value) { return mesh.key < value; });",
        "    return item != entry.end && item->key == key ? item : nullptr;",
        "}",
    ])
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    models, old_config, header = map(Path, sys.argv[1:])
    match = re.search(r"^MeshKeys=(.+)$", old_config.read_text(), re.MULTILINE)
    keys = {int(key, 16) for key in match[1].split(",")}
    header.write_text(generate(models, keys), encoding="ascii")
