"""Writes the mod's JSON resources: block states, models, item definitions, loot tables, recipes and tags.

    python tools/make_assets.py

There are five grades, each with a block and a stair block, so these files are repetitive enough to be
worth generating. The formats follow Minecraft 26.x as used by the Fabric documentation's reference mod
(https://github.com/FabricMC/fabric-docs/tree/main/reference). The English text lives in
src/main/resources/assets/rustbuilding/lang/en_us.json and is edited by hand.
"""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(ROOT, "src", "main", "resources")
ASSETS = os.path.join(RESOURCES, "assets", "rustbuilding")
DATA = os.path.join(RESOURCES, "data")
NS = "rustbuilding"

TIERS = ["twig", "wood", "stone", "sheet_metal", "armored"]
DOORS = ["sheet_metal_door", "armored_door"]
FACINGS = {"north": 0, "east": 90, "south": 180, "west": 270}


def write(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2)
        handle.write("\n")


def blockstate(name, value):
    write(os.path.join(ASSETS, "blockstates", name + ".json"), value)


def block_model(name, value):
    write(os.path.join(ASSETS, "models", "block", name + ".json"), value)


def item_model(name, value):
    write(os.path.join(ASSETS, "models", "item", name + ".json"), value)


def item_definition(name, model):
    write(os.path.join(ASSETS, "items", name + ".json"), {"model": {"type": "minecraft:model", "model": model}})


def building_blocks():
    for tier in TIERS:
        texture = "%s:block/%s" % (NS, tier)
        block_model(tier, {"parent": "minecraft:block/cube_all", "textures": {"all": texture}})
        # Slab, wall and support blocks of a grade all look the same.
        blockstate(tier, {"variants": {"kind=%s" % kind: {"model": "%s:block/%s" % (NS, tier)} for kind in ("slab", "wall", "support")}})

        stairs = tier + "_stairs"
        textures = {"bottom": texture, "side": texture, "top": texture}
        block_model(stairs, {"parent": "minecraft:block/stairs", "textures": textures})
        block_model(stairs + "_inner", {"parent": "minecraft:block/inner_stairs", "textures": textures})
        block_model(stairs + "_outer", {"parent": "minecraft:block/outer_stairs", "textures": textures})
        blockstate(stairs, stairs_variants(stairs))


def stairs_variants(name):
    """The same 40 variants as vanilla stairs (facing x half x shape)."""
    base_y = {"east": 0, "south": 90, "west": 180, "north": 270}
    variants = {}
    for facing, y in base_y.items():
        for half in ("bottom", "top"):
            for shape in ("straight", "inner_left", "inner_right", "outer_left", "outer_right"):
                model = name if shape == "straight" else name + ("_inner" if shape.startswith("inner") else "_outer")
                rotation = y
                if half == "bottom" and shape in ("inner_left", "outer_left"):
                    rotation = y - 90
                if half == "top" and shape in ("inner_right", "outer_right"):
                    rotation = y + 90
                variant = {"model": "%s:block/%s" % (NS, model)}
                if half == "top":
                    variant["x"] = 180
                rotation %= 360
                if rotation:
                    variant["y"] = rotation
                if variant.get("x") or variant.get("y"):
                    variant["uvlock"] = True
                variants["facing=%s,half=%s,shape=%s" % (facing, half, shape)] = variant
    return {"variants": variants}


def tool_cupboard():
    block_model("tool_cupboard", {
        "parent": "minecraft:block/orientable",
        "textures": {
            "front": "%s:block/tool_cupboard_front" % NS,
            "side": "%s:block/tool_cupboard_side" % NS,
            "top": "%s:block/tool_cupboard_top" % NS,
        },
    })
    variants = {}
    for facing, y in FACINGS.items():
        variant = {"model": "%s:block/tool_cupboard" % NS}
        if y:
            variant["y"] = y
        variants["facing=" + facing] = variant
    blockstate("tool_cupboard", {"variants": variants})
    item_definition("tool_cupboard", "%s:block/tool_cupboard" % NS)


def doors():
    # Rotations copied from vanilla door block states (they match the Fabric reference mod's ruby_door).
    closed = {"east": 0, "north": 270, "south": 90, "west": 180}
    for door in DOORS:
        textures = {"bottom": "%s:block/%s_bottom" % (NS, door), "top": "%s:block/%s_top" % (NS, door)}
        for part in ("bottom_left", "bottom_left_open", "bottom_right", "bottom_right_open",
                     "top_left", "top_left_open", "top_right", "top_right_open"):
            block_model("%s_%s" % (door, part), {"parent": "minecraft:block/door_" + part, "textures": textures})

        variants = {}
        for facing, y in closed.items():
            for half, prefix in (("lower", "bottom"), ("upper", "top")):
                for hinge in ("left", "right"):
                    for opened in ("false", "true"):
                        model = "%s:block/%s_%s_%s%s" % (NS, door, prefix, hinge, "_open" if opened == "true" else "")
                        rotation = y
                        if opened == "true":
                            rotation = y + (90 if hinge == "left" else 270)
                        variant = {"model": model}
                        if rotation % 360:
                            variant["y"] = rotation % 360
                        variants["facing=%s,half=%s,hinge=%s,open=%s" % (facing, half, hinge, opened)] = variant
        blockstate(door, {"variants": variants})
        item_model(door, {"parent": "minecraft:item/generated", "textures": {"layer0": "%s:item/%s" % (NS, door)}})
        item_definition(door, "%s:item/%s" % (NS, door))


def items():
    item_model("building_plan", {"parent": "minecraft:item/generated", "textures": {"layer0": "%s:item/building_plan" % NS}})
    item_model("hammer", {"parent": "minecraft:item/handheld", "textures": {"layer0": "%s:item/hammer" % NS}})
    item_model("code_lock", {"parent": "minecraft:item/generated", "textures": {"layer0": "%s:item/code_lock" % NS}})
    for name in ("building_plan", "hammer", "code_lock"):
        item_definition(name, "%s:item/%s" % (NS, name))


def loot_tables():
    path = os.path.join(DATA, NS, "loot_table", "blocks")
    write(os.path.join(path, "tool_cupboard.json"), {
        "type": "minecraft:block",
        "pools": [{
            "rolls": 1.0,
            "bonus_rolls": 0.0,
            "conditions": [{"condition": "minecraft:survives_explosion"}],
            "entries": [{"type": "minecraft:item", "name": "%s:tool_cupboard" % NS}],
        }],
    })
    for door in DOORS:
        # Only the bottom half drops the door, as with vanilla doors.
        write(os.path.join(path, door + ".json"), {
            "type": "minecraft:block",
            "pools": [{
                "rolls": 1.0,
                "bonus_rolls": 0.0,
                "conditions": [{"condition": "minecraft:survives_explosion"}],
                "entries": [{
                    "type": "minecraft:item",
                    "name": "%s:%s" % (NS, door),
                    "conditions": [{
                        "condition": "minecraft:block_state_property",
                        "block": "%s:%s" % (NS, door),
                        "properties": {"half": "lower"},
                    }],
                }],
            }],
        })


def recipes():
    path = os.path.join(DATA, NS, "recipe")
    write(os.path.join(path, "building_plan.json"), {
        "type": "minecraft:crafting_shapeless",
        "category": "misc",
        "ingredients": ["minecraft:paper", "minecraft:paper", "minecraft:stick"],
        "result": {"id": "%s:building_plan" % NS},
    })
    write(os.path.join(path, "hammer.json"), {
        "type": "minecraft:crafting_shaped",
        "category": "equipment",
        "key": {"P": "#minecraft:planks", "S": "minecraft:stick"},
        "pattern": ["PSP", " S ", " S "],
        "result": {"id": "%s:hammer" % NS},
    })
    write(os.path.join(path, "tool_cupboard.json"), {
        "type": "minecraft:crafting_shaped",
        "category": "building",
        "key": {"L": "#minecraft:logs", "C": "minecraft:chest"},
        "pattern": ["LLL", "LCL", "LLL"],
        "result": {"id": "%s:tool_cupboard" % NS},
    })
    write(os.path.join(path, "code_lock.json"), {
        "type": "minecraft:crafting_shaped",
        "category": "redstone",
        "key": {"N": "minecraft:iron_nugget", "R": "minecraft:redstone", "I": "minecraft:iron_ingot"},
        "pattern": ["NRN", "NIN"],
        "result": {"id": "%s:code_lock" % NS},
    })
    write(os.path.join(path, "sheet_metal_door.json"), {
        "type": "minecraft:crafting_shapeless",
        "category": "redstone",
        "ingredients": ["minecraft:iron_door", "minecraft:iron_ingot", "minecraft:iron_ingot"],
        "result": {"id": "%s:sheet_metal_door" % NS},
    })
    write(os.path.join(path, "armored_door.json"), {
        "type": "minecraft:crafting_shapeless",
        "category": "redstone",
        "ingredients": ["%s:sheet_metal_door" % NS, "minecraft:iron_block", "minecraft:iron_block"],
        "result": {"id": "%s:armored_door" % NS},
    })


def tags():
    tag_path = os.path.join(DATA, "minecraft", "tags", "block")
    solid = ["%s:%s" % (NS, tier) for tier in TIERS[1:]] + ["%s:%s_stairs" % (NS, tier) for tier in TIERS[1:]]
    # Withers and the dragon would otherwise chew through blocks that players cannot mine.
    write(os.path.join(tag_path, "wither_immune.json"), {"replace": False, "values": solid})
    write(os.path.join(tag_path, "dragon_immune.json"), {"replace": False, "values": solid})
    write(os.path.join(tag_path, "mineable", "pickaxe.json"), {"replace": False, "values": ["%s:%s" % (NS, door) for door in DOORS]})
    write(os.path.join(tag_path, "mineable", "axe.json"), {"replace": False, "values": [
        "%s:tool_cupboard" % NS, "%s:twig" % NS, "%s:twig_stairs" % NS]})


def main():
    building_blocks()
    tool_cupboard()
    doors()
    items()
    loot_tables()
    recipes()
    tags()
    print("wrote block states, models, items, loot tables, recipes and tags")


if __name__ == "__main__":
    main()
