#!/usr/bin/env python3
"""
Static sanity checks for the Millhaven C++ module.

This is NOT a compiler. It catches the specific classes of mistake that are
easy to make when writing Unreal C++ without an engine to build against:

  1. Type names that collide with well-known global engine types.
  2. `.generated.h` not being the last include in a UHT-parsed header (a hard
     UnrealHeaderTool error).
  3. Member functions declared in a header but never defined in the matching
     .cpp (link error), or defined but never declared (compile error).
  4. Unbalanced braces / parens / brackets.
  5. `#include "Local.h"` naming a file that does not exist.
  6. UCLASS types missing GENERATED_BODY().

Run:  python3 Tools/check_sources.py
Exit code is non-zero if any error-level finding is reported.
"""

from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass, field

SOURCE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "Millhaven")

# Global types the engine already defines. Redeclaring any of these at global
# scope is a redefinition error once the shared PCH or a unity blob pulls the
# engine header in.
ENGINE_RESERVED_TYPES = {
    "FMeshBatch", "FMeshBatchElement", "FPrimitiveSceneProxy", "FSceneView",
    "FDynamicMeshBuilder", "FMaterialRenderProxy", "FVertexFactory",
    "FRawMesh", "FMeshDescription", "FStaticMeshVertexBuffers",
    "FBoxSphereBounds", "FConvexVolume", "FSceneViewFamily", "FViewport",
    "FRenderTarget", "FCanvas", "FTexture", "FLightSceneInfo",
    "FMeshElementCollector", "FPrimitiveViewRelevance", "FMaterial",
}

# Statements that look like a declaration but are not.
NON_DECL_KEYWORDS = {
    "if", "for", "while", "switch", "return", "else", "do", "case",
    "UCLASS", "USTRUCT", "UENUM", "UPROPERTY", "UFUNCTION", "UINTERFACE",
    "GENERATED_BODY", "GENERATED_UCLASS_BODY", "DECLARE_DELEGATE",
    "public", "private", "protected", "friend", "using", "typedef",
    "static_assert", "template", "namespace", "struct", "class", "enum",
    "UE_LOG", "check", "checkf", "ensure", "delete", "new", "throw",
}


@dataclass
class Findings:
    errors: list = field(default_factory=list)
    warnings: list = field(default_factory=list)

    def error(self, path: str, msg: str, line: int = 0) -> None:
        self.errors.append((path, line, msg))

    def warn(self, path: str, msg: str, line: int = 0) -> None:
        self.warnings.append((path, line, msg))


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and string/char literals, preserving line structure."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                if text[i] == "\n":
                    out.append("\n")
                i += 1
            i += 2
        elif c in ('"', "'"):
            quote = c
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    i += 1
                if i < n and text[i] == "\n":
                    out.append("\n")
                i += 1
            i += 1
            out.append('""')
        else:
            out.append(c)
            i += 1
    return "".join(out)


def check_balance(path: str, code: str, f: Findings) -> None:
    pairs = {")": "(", "]": "[", "}": "{"}
    openers = {"(": ")", "[": "]", "{": "}"}
    stack = []
    line = 1
    for ch in code:
        if ch == "\n":
            line += 1
        elif ch in openers:
            stack.append((ch, line))
        elif ch in pairs:
            if not stack:
                f.error(path, "unmatched closing '%s'" % ch, line)
                return
            open_ch, open_line = stack.pop()
            if open_ch != pairs[ch]:
                f.error(path, "'%s' opened at line %d closed by '%s'"
                        % (open_ch, open_line, ch), line)
                return
    if stack:
        open_ch, open_line = stack[-1]
        f.error(path, "'%s' opened here is never closed" % open_ch, open_line)


def check_includes(path: str, text: str, all_files: set, f: Findings) -> None:
    is_header = path.endswith(".h")
    includes = []
    for idx, raw in enumerate(text.splitlines(), start=1):
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', raw)
        if m:
            includes.append((idx, m.group(1)))

    for idx, inc in includes:
        base = os.path.basename(inc)
        # Only local project headers are resolvable here; engine paths are not.
        if base.startswith("Millhaven") or base == "ProcMeshLib.h":
            if base.endswith(".generated.h"):
                continue
            if base not in all_files:
                f.error(path, "includes '%s' which does not exist in Source/Millhaven" % inc, idx)

    if is_header:
        gen = [(i, inc) for i, inc in includes if inc.endswith(".generated.h")]
        if gen:
            last_idx = includes[-1][0]
            gen_idx = gen[-1][0]
            if gen_idx != last_idx:
                f.error(path, "'%s' must be the LAST include (UHT requirement)"
                        % gen[-1][1], gen_idx)


def check_reserved_types(path: str, code: str, f: Findings) -> None:
    for m in re.finditer(r'\b(?:struct|class)\s+(\w+)\s*(?:final\s*)?[:{]', code):
        name = m.group(1)
        if name in ENGINE_RESERVED_TYPES:
            line = code[:m.start()].count("\n") + 1
            f.error(path, "declares '%s', which collides with an engine type" % name, line)


def check_generated_body(path: str, code: str, f: Findings) -> None:
    for m in re.finditer(r'\bUCLASS\s*\([^)]*\)\s*(?:class|struct)\s+(?:\w+_API\s+)?(\w+)', code):
        name = m.group(1)
        start = m.end()
        window = code[start:start + 2000]
        if "GENERATED_BODY" not in window and "GENERATED_UCLASS_BODY" not in window:
            line = code[:m.start()].count("\n") + 1
            f.error(path, "UCLASS '%s' has no GENERATED_BODY()" % name, line)


# Matches a whole declaration *statement*, which may span several lines.
DECL_RE = re.compile(
    r'^\s*(?:virtual\s+)?'
    r'(?P<ret>(?:static\s+|constexpr\s+|inline\s+|explicit\s+|FORCEINLINE\s+)*'
    r'(?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^;{}]*>)?\s*[*&]*\s+)'
    r'(?P<name>[A-Za-z_]\w*)\s*'
    r'\((?P<args>[^;{}]*)\)\s*'
    r'(?:const\s*|noexcept\s*|override\s*|final\s*)*$',
    re.DOTALL,
)


def iter_class_bodies(code: str):
    """Yield (class_name, body_text) for each class/struct definition."""
    for m in re.finditer(r'\b(?:class|struct)\s+(?:\w+_API\s+)?(\w+)\b[^;{]*\{', code):
        name = m.group(1)
        start = m.end()           # just past the opening brace
        depth = 1
        i = start
        while i < len(code) and depth > 0:
            if code[i] == "{":
                depth += 1
            elif code[i] == "}":
                depth -= 1
            i += 1
        if depth == 0:
            yield name, code[start:i - 1]


def split_top_level_statements(body: str):
    """Split a class body into statements, skipping nested braces (inline bodies)."""
    stmt = []
    depth = 0
    paren = 0
    for ch in body:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                stmt = []          # discard the inline definition we just passed
                continue
        elif ch == "(":
            paren += 1
        elif ch == ")":
            paren -= 1

        if depth > 0:
            continue
        if ch == ";" and paren == 0:
            yield "".join(stmt)
            stmt = []
        else:
            stmt.append(ch)


ACCESS_RE = re.compile(r'^\s*(?:public|private|protected)\s*:')
# GENERATED_BODY() carries no trailing semicolon, so it lands at the head of
# whatever declaration follows it.
REFLECT_RE = re.compile(
    r'^\s*(?:UPROPERTY|UFUNCTION|UE_DEPRECATED|GENERATED_BODY|GENERATED_UCLASS_BODY)'
    r'\s*\([^)]*\)'
)


def strip_statement_prefixes(stmt: str) -> str:
    """Drop access-specifier labels and reflection macros ahead of a declaration."""
    prev = None
    while prev != stmt:
        prev = stmt
        stmt = ACCESS_RE.sub("", stmt, count=1)
        stmt = REFLECT_RE.sub("", stmt, count=1)
    return stmt.strip()


def collect_declarations(header_code: str) -> dict:
    """Return {class_name: set(method_names)} for out-of-line declarations."""
    result = {}
    for cls, body in iter_class_bodies(header_code):
        methods = result.setdefault(cls, set())
        for stmt in split_top_level_statements(body):
            stmt = strip_statement_prefixes(stmt)
            if not stmt:
                continue
            d = DECL_RE.match(stmt)
            if not d:
                continue
            name = d.group("name")
            ret = d.group("ret").strip()
            if name in NON_DECL_KEYWORDS or ret in NON_DECL_KEYWORDS:
                continue
            # Macro invocations look like calls, e.g. GENERATED_BODY()
            if re.fullmatch(r'[A-Z0-9_]{4,}', name):
                continue
            if name == cls or name.startswith("~"):
                continue
            methods.add(name)
    return result


def collect_definitions(cpp_code: str) -> set:
    """Return {(class, method)} defined out-of-line in a .cpp."""
    found = set()
    for m in re.finditer(r'\b(\w+)::(~?\w+)\s*\(', cpp_code):
        found.add((m.group(1), m.group(2)))
    return found


def main() -> int:
    src = os.path.normpath(SOURCE_DIR)
    if not os.path.isdir(src):
        print("error: %s not found" % src)
        return 2

    files = sorted(os.listdir(src))
    all_files = set(files)
    f = Findings()

    headers = [x for x in files if x.endswith(".h")]
    cpps = [x for x in files if x.endswith(".cpp")]

    codes = {}
    for name in headers + cpps:
        path = os.path.join(src, name)
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
        code = strip_comments_and_strings(text)
        codes[name] = code

        check_balance(name, code, f)
        check_includes(name, text, all_files, f)
        check_reserved_types(name, code, f)
        if name.endswith(".h"):
            check_generated_body(name, code, f)

    # Declaration/definition cross-check
    for h in headers:
        cpp = h[:-2] + ".cpp"
        if cpp not in codes:
            continue
        decls = collect_declarations(codes[h])
        defs = collect_definitions(codes[cpp])
        defined_names = {(c, m) for (c, m) in defs}

        for cls, methods in decls.items():
            for method in sorted(methods):
                if (cls, method) not in defined_names:
                    f.warn(h, "%s::%s is declared but has no definition in %s"
                           % (cls, method, cpp))

        declared_any = {(c, m) for c, ms in decls.items() for m in ms}
        header_classes = set(decls.keys())
        for (cls, method) in sorted(defs):
            if cls not in header_classes:
                continue
            if method == cls or method.startswith("~"):
                continue
            if (cls, method) in declared_any:
                continue
            # Inline-defined in the header is fine.
            if re.search(r'\b%s\s*\([^;{}]*\)[^;]*\{' % re.escape(method), codes[h]):
                continue
            f.warn(cpp, "%s::%s is defined but not declared in %s" % (cls, method, h))

    for path, line, msg in f.errors:
        print("ERROR   %s:%d: %s" % (path, line, msg))
    for path, line, msg in f.warnings:
        print("WARNING %s:%d: %s" % (path, line, msg))

    print("\n%d file(s) checked, %d error(s), %d warning(s)."
          % (len(codes), len(f.errors), len(f.warnings)))
    return 1 if f.errors else 0


if __name__ == "__main__":
    sys.exit(main())
