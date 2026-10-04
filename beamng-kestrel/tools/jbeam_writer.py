"""Writes Python data as JBeam text.

JBeam is JSON with a few relaxations (comments, optional commas). This writer
emits ordinary JSON plus // comment lines, which BeamNG accepts; strip the
comment lines and any JSON reader can load the file. Tables (lists of rows)
are written one row per line so the files read like hand-written ones.
"""

import json


class Comment:
    """A // comment line inside a JBeam list."""

    def __init__(self, text):
        self.text = text


def _scalar(v):
    if isinstance(v, float):
        if v != v or v in (float("inf"), float("-inf")):
            raise ValueError("use the string \"FLT_MAX\" for infinite values")
        s = ("%.6f" % v).rstrip("0").rstrip(".")
        return "0" if s in ("-0", "") else s
    return json.dumps(v)


def _inline(v):
    if isinstance(v, dict):
        return "{" + ", ".join("%s:%s" % (json.dumps(k), _inline(x)) for k, x in v.items()) + "}"
    if isinstance(v, (list, tuple)):
        return "[" + ", ".join(_inline(x) for x in v) + "]"
    return _scalar(v)


def _is_table(v):
    return isinstance(v, list) and any(isinstance(x, (list, Comment)) for x in v)


def _block(v, indent):
    pad = "    " * indent
    inner = "    " * (indent + 1)
    if isinstance(v, dict):
        if not v:
            return "{}"
        if all(not isinstance(x, (dict, list)) for x in v.values()) and len(v) <= 4:
            return _inline(v)
        lines = []
        for k, x in v.items():
            lines.append("%s%s: %s," % (inner, json.dumps(k), _block(x, indent + 1)))
        lines[-1] = lines[-1][:-1]
        return "{\n" + "\n".join(lines) + "\n" + pad + "}"
    if _is_table(v):
        lines = []
        items = [x for x in v]
        real = [i for i, x in enumerate(items) if not isinstance(x, Comment)]
        last = real[-1] if real else -1
        for i, x in enumerate(items):
            if isinstance(x, Comment):
                lines.append("%s// %s" % (inner, x.text))
            else:
                lines.append("%s%s%s" % (inner, _inline(x), "" if i == last else ","))
        return "[\n" + "\n".join(lines) + "\n" + pad + "]"
    return _inline(v)


def dumps(parts, header=None):
    out = []
    if header:
        out += ["// " + line if line else "//" for line in header.strip("\n").split("\n")]
    out.append(_block(parts, 0))
    return "\n".join(out) + "\n"
