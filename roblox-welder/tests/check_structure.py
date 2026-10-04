"""Rough structural check for the Luau sources (no Luau runtime needed).

It strips comments and strings, then checks that
  * every block opener (function, statement-if, do) has a matching `end`
  * (), [] and {} are balanced
This is not a real parser. It catches the most common slip (a missing or
extra `end`) and nothing more; Roblox Studio's Script Analysis is the real check.

Run: python3 tests/check_structure.py
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "src"

TOKEN = re.compile(
    r"--\[(=*)\[.*?\]\1\]"  # long comment
    r"|--[^\n]*"  # line comment
    r"|\[(=*)\[.*?\]\2\]"  # long string
    r'|"(?:\\.|[^"\\\n])*"'  # double-quoted string
    r"|'(?:\\.|[^'\\\n])*'"  # single-quoted string
    r"|`(?:\\.|[^`\\])*`"  # interpolated string
    r"|[A-Za-z_][A-Za-z_0-9]*"  # name / keyword
    r"|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?"  # number
    r"|\.\.\.|\.\.=?|::|==|~=|<=|>=|->|[-+*/%^#]=?|[=<>(){}\[\];:,.]",
    re.S,
)

# After these tokens an `if` is an if-expression (no `end`).
EXPR_CONTEXT = {
    "=", "(", ",", "{", "[", "return", "..", "and", "or", "not", "+", "-", "*",
    "/", "%", "^", "==", "~=", "<", ">", "<=", ">=", "::", "+=", "-=", "*=",
    "/=", "..=", "then", "else", "in",
}


def check(path: pathlib.Path) -> list[str]:
    text = path.read_text()
    tokens = []
    for m in TOKEN.finditer(text):
        tok = m.group(0)
        if tok.startswith("--"):
            continue
        line = text.count("\n", 0, m.start()) + 1
        if tok[0] in "\"'`" or re.match(r"\[=*\[", tok):
            tok = "<string>"  # keep a placeholder: it is a value
        tokens.append((tok, line))

    errors = []
    stack = []  # (opener, line)
    brackets = []
    prev = None
    for i, (tok, line) in enumerate(tokens):
        # Assumption: if-expressions fit on one line (true for this project),
        # so they are dropped once the line ends.
        while stack and stack[-1][0] == "ifexpr" and stack[-1][1] != line:
            stack.pop()
        if tok == "function":
            stack.append(("function", line))
        elif tok == "if":
            # `then if` / `else if` are statements only if they begin a statement;
            # inside expressions they follow an operator.
            is_expr = prev in EXPR_CONTEXT and prev not in ("then", "else")
            if prev in ("then", "else"):
                # Statement-if inside a block, unless the enclosing if was an
                # expression on the same line (a nested if-expression).
                is_expr = bool(stack) and stack[-1][0] == "ifexpr"
            stack.append(("ifexpr" if is_expr else "if", line))
        elif tok == "else" and stack and stack[-1][0] == "ifexpr":
            pass
        elif tok == "do":
            stack.append(("do", line))
        elif tok == "end":
            # Close any if-expressions that ended before this `end`.
            while stack and stack[-1][0] == "ifexpr":
                stack.pop()
            if not stack:
                errors.append(f"{path.name}:{line}: unexpected 'end'")
            else:
                stack.pop()
        elif tok in "([{":
            brackets.append((tok, line))
        elif tok in ")]}":
            want = {")": "(", "]": "[", "}": "{"}[tok]
            if not brackets or brackets[-1][0] != want:
                errors.append(f"{path.name}:{line}: unbalanced '{tok}'")
            else:
                brackets.pop()
        prev = tok

    for opener, line in stack:
        if opener != "ifexpr":
            errors.append(f"{path.name}:{line}: '{opener}' is never closed")
    for b, line in brackets:
        errors.append(f"{path.name}:{line}: '{b}' is never closed")
    return errors


def main() -> int:
    files = sorted(ROOT.rglob("*.luau"))
    problems = []
    for f in files:
        problems += check(f)
    for p in problems:
        print(p)
    print(f"checked {len(files)} files: {'OK' if not problems else f'{len(problems)} problem(s)'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
