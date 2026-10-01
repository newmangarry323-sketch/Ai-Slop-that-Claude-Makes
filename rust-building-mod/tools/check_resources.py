"""Catches resource mistakes before Minecraft does.

    python tools/check_resources.py

Checks that every JSON file parses, that every model, texture and parent a block state or model points
at exists, and that every translation key the Java code uses is in en_us.json. Exits non-zero on any
problem, so CI can run it before the much slower Gradle build.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "src", "main", "resources", "assets", "rustbuilding")
SOURCES = [os.path.join(ROOT, "src", name, "java") for name in ("main", "client", "gametest")]

problems = []


def load_json_files():
    files = {}
    for base in (os.path.join(ROOT, "src", "main", "resources"), os.path.join(ROOT, "src", "gametest", "resources")):
        for folder, _, names in os.walk(base):
            for name in names:
                if name.endswith(".json"):
                    path = os.path.join(folder, name)
                    try:
                        with open(path, encoding="utf-8") as handle:
                            files[path] = json.load(handle)
                    except ValueError as error:
                        problems.append("%s does not parse: %s" % (os.path.relpath(path, ROOT), error))
    return files


def resource_exists(reference, kind, extension):
    namespace, _, path = reference.partition(":")
    if not path:
        namespace, path = "minecraft", reference
    if namespace == "minecraft":
        return True  # vanilla's own models and textures
    return os.path.exists(os.path.join(ROOT, "src", "main", "resources", "assets", namespace, kind, path + extension))


def check_models(files):
    for path, data in files.items():
        relative = os.path.relpath(path, ASSETS)
        if relative.startswith("blockstates"):
            for variant in data.get("variants", {}).values():
                for entry in variant if isinstance(variant, list) else [variant]:
                    if not resource_exists(entry["model"], "models", ".json"):
                        problems.append("%s points at missing model %s" % (relative, entry["model"]))
        elif relative.startswith("models"):
            parent = data.get("parent")
            if parent and not resource_exists(parent, "models", ".json"):
                problems.append("%s has missing parent %s" % (relative, parent))
            for texture in data.get("textures", {}).values():
                if not texture.startswith("#") and not resource_exists(texture, "textures", ".png"):
                    problems.append("%s points at missing texture %s" % (relative, texture))
        elif relative.startswith("items"):
            model = data["model"]["model"]
            if not resource_exists(model, "models", ".json"):
                problems.append("%s points at missing model %s" % (relative, model))


def check_translations():
    with open(os.path.join(ASSETS, "lang", "en_us.json"), encoding="utf-8") as handle:
        lang = json.load(handle)
    used = set()
    for source in SOURCES:
        for folder, _, names in os.walk(source):
            for name in names:
                if name.endswith(".java"):
                    with open(os.path.join(folder, name), encoding="utf-8") as handle:
                        text = handle.read()
                    used.update(re.findall(r'"((?:item|block|tier|material|piece|cost|hud|message|screen|itemGroup)\.rustbuilding[a-z0-9_.]*)"', text))
                    # Keys built from a prefix plus a reason: fail("too_high"), deny(level, player, "locked").
                    used.update("message.rustbuilding." + reason for reason in re.findall(r'(?:fail|deny)\([^"]*"([a-z_]+)"\)', text))
    for key in sorted(used):
        if key.endswith("."):
            continue
        if key not in lang:
            problems.append("translation key %s is used but missing from en_us.json" % key)
    # Keys assembled at run time from enum names.
    for tier in ("twig", "wood", "stone", "sheet_metal", "armored"):
        for prefix in ("tier", "material"):
            if "%s.rustbuilding.%s" % (prefix, tier) not in lang:
                problems.append("missing %s.rustbuilding.%s" % (prefix, tier))
    for piece in ("foundation", "floor", "wall", "doorway", "window", "half_wall", "low_wall", "stairs"):
        if "piece.rustbuilding." + piece not in lang:
            problems.append("missing piece.rustbuilding." + piece)
    for category in ("slab", "edge", "stairs"):
        if "message.rustbuilding.aim." + category not in lang:
            problems.append("missing message.rustbuilding.aim." + category)


def main():
    files = load_json_files()
    check_models(files)
    check_translations()
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    print("resources ok: %d JSON files" % len(files))


if __name__ == "__main__":
    main()
