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
  7. Names that shadow a member inherited from an engine base (C4458).
  8. `static` UObject pointers - invisible to the garbage collector.
  9. UObject pointer members with no UPROPERTY() - same GC hazard.
 10. CreateDefaultSubobject used on a UDataAsset type rather than a component.
 11. float initialised from a double-returning LWC helper with no cast (C4244).

Checks 7-11 all exist because the corresponding bug was actually shipped once;
see CLAUDE.md section 5.

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

# Data members inherited from common engine bases. A parameter or member with
# one of these names hides the inherited one; UE builds with warnings-as-errors,
# so a shadowed name is C4458 and fails the build outright.
#
# These are per-base, not a flat list: 'Mesh' only shadows something inside an
# ACharacter, and nothing at all in a free function.
_ACTOR_MEMBERS = {
    "Role", "RemoteRole", "Owner", "Instigator", "Children", "RootComponent",
    "Tags", "InputComponent", "NetDriverName", "ReplicatedMovement",
    "CustomTimeDilation", "InitialLifeSpan",
}
_PAWN_MEMBERS = {"Controller", "PlayerState", "BaseEyeHeight", "AutoPossessPlayer"}
_CHARACTER_MEMBERS = {"Mesh", "CharacterMovement", "CapsuleComponent"}
_HUD_MEMBERS = {"Canvas", "DebugCanvas", "PlayerOwner"}

ENGINE_BASE_MEMBERS = {
    "AActor": _ACTOR_MEMBERS,
    "APawn": _ACTOR_MEMBERS | _PAWN_MEMBERS,
    "ACharacter": _ACTOR_MEMBERS | _PAWN_MEMBERS | _CHARACTER_MEMBERS,
    "AHUD": _ACTOR_MEMBERS | _HUD_MEMBERS,
    "AController": _ACTOR_MEMBERS | _PAWN_MEMBERS,
    "APlayerController": _ACTOR_MEMBERS | _PAWN_MEMBERS,
    "AGameModeBase": _ACTOR_MEMBERS,
    "AGameMode": _ACTOR_MEMBERS,
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


def collect_class_bases(headers_code: dict) -> dict:
    """Return {our_class_name: set_of_inherited_member_names}."""
    bases = {}
    for code in headers_code.values():
        for m in re.finditer(
                r'\b(?:class|struct)\s+(?:\w+_API\s+)?(\w+)\s*:\s*public\s+([\w:]+)', code):
            bases[m.group(1)] = m.group(2)

    resolved = {}
    for cls in bases:
        seen, cur = set(), cls
        while cur in bases and cur not in seen:
            seen.add(cur)
            cur = bases[cur]
        if cur in ENGINE_BASE_MEMBERS:
            resolved[cls] = ENGINE_BASE_MEMBERS[cur]
    return resolved


def _find_body_span(code: str, open_idx: int):
    """Given the index of a '{', return the index just past its matching '}'."""
    depth, i = 0, open_idx
    while i < len(code):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return len(code)


def check_shadowed_engine_members(path: str, code: str, class_members: dict, f: Findings) -> None:
    """
    Flag parameters and data members that hide a member inherited from an
    engine base. The classic offender is 'Role', which every AActor has.

    Only scanned inside classes that actually inherit the name - a free
    function's 'Mesh' parameter shadows nothing.
    """
    def scan_params(region: str, offset: int, names: set, what: str):
        if not names:
            return
        pattern = re.compile(
            r'[(,]\s*(?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^;{}()]*>)?\s*[&*]*\s+(%s)\s*[,)=]'
            % "|".join(sorted(names))
        )
        for m in pattern.finditer(region):
            line = code[:offset + m.start()].count("\n") + 1
            f.error(path, "%s parameter '%s' hides an inherited engine member (C4458)"
                    % (what, m.group(1)), line)

    if path.endswith(".h"):
        for cls, body in iter_class_bodies(code):
            names = class_members.get(cls)
            if not names:
                continue
            offset = code.find(body)
            member_re = re.compile(
                r'(?:^|;|\{|\})\s*(?:UPROPERTY\s*\([^)]*\)\s*)?'
                r'(?:mutable\s+|static\s+)?[A-Za-z_][\w:]*(?:\s*<[^;{}()]*>)?\s*[&*]*\s+'
                r'(%s)\s*(?:=[^;]*)?;' % "|".join(sorted(names))
            )
            for m in member_re.finditer(body):
                line = code[:offset + m.start()].count("\n") + 1
                f.error(path, "member '%s' hides an inherited engine member" % m.group(1), line)
            scan_params(body, offset, names, "declared")
        return

    # In a .cpp, only scan inside out-of-line member function definitions, so
    # that free helper functions are not judged against a class they aren't in.
    for m in re.finditer(r'\b(\w+)::~?\w+\s*\(', code):
        names = class_members.get(m.group(1))
        if not names:
            continue
        brace = code.find("{", m.end())
        if brace == -1:
            continue
        end = _find_body_span(code, brace)
        scan_params(code[m.start():end], m.start(), names, "member function")


# A `static UFoo*` variable - file scope or function local - is a raw UObject
# pointer the garbage collector cannot see, so the object can be collected out
# from under it. The fix is always a UPROPERTY() member.
#
# The trailing lookahead is what separates a *variable* from a static function
# that merely returns a pointer: a declarator is followed by '=', ';' or '[',
# never by '('.
STATIC_UOBJECT_RE = re.compile(
    r'\bstatic\s+(?:const\s+)?([UA][A-Z]\w*)\s*\*\s*(\w+)\s*(?=[;=\[])')

# UInputAction and UInputMappingContext are UDataAssets, not components.
# CreateDefaultSubobject is for CDO component subobjects only.
CDO_MISUSE_RE = re.compile(
    r'CreateDefaultSubobject\s*<\s*(UInputAction|UInputMappingContext|UDataAsset)\s*>')

# A whole-statement match for a bare UObject pointer data member.
UOBJ_PTR_MEMBER_RE = re.compile(
    r'^(?:mutable\s+)?(?:const\s+)?([UA][A-Z]\w*)\s*\*\s*(\w+)\s*(?:=[^;]*)?$')

# FVector/FVector2D are double-based under LWC, so these all return double.
LWC_DOUBLE_FUNCS = (
    "Dist2D", "DistSquared2D", "Size2D", "SizeSquared2D", "Atan2", "Distance",
)


def check_static_uobjects(path: str, code: str, f: Findings) -> None:
    for m in STATIC_UOBJECT_RE.finditer(code):
        line = code[:m.start()].count("\n") + 1
        f.error(path, "static '%s*' is invisible to the GC - use a UPROPERTY() member"
                % m.group(1), line)


def check_cdo_misuse(path: str, code: str, f: Findings) -> None:
    for m in CDO_MISUSE_RE.finditer(code):
        line = code[:m.start()].count("\n") + 1
        f.error(path, "CreateDefaultSubobject<%s> - that is a UDataAsset, use NewObject"
                % m.group(1), line)


def check_uproperty_members(path: str, code: str, f: Findings) -> None:
    """UObject pointer members must be UPROPERTY() or the GC cannot see them."""
    if not path.endswith(".h"):
        return
    for cls, body in iter_class_bodies(code):
        body_at = code.find(body)
        for stmt in split_top_level_statements(body):
            if "UPROPERTY" in stmt:
                continue
            decl = strip_statement_prefixes(stmt)
            m = UOBJ_PTR_MEMBER_RE.match(decl)
            if not m:
                continue
            # Point at the declarator, not at whatever access label or macro
            # the statement happens to begin with.
            stmt_at = body.find(stmt)
            name_at = stmt.find(m.group(2))
            at = stmt_at + (name_at if name_at >= 0 else 0)
            line = code[:body_at + at].count("\n") + 1
            f.error(path, "%s::%s is a '%s*' with no UPROPERTY() - invisible to the GC"
                    % (cls, m.group(2), m.group(1)), line)


def check_lwc_narrowing(path: str, code: str, f: Findings) -> None:
    """float x = <something returning double>; is C4244, which is fatal here."""
    for m in re.finditer(r'\bfloat\s+\w+\s*=\s*([^;]+);', code):
        init = m.group(1)
        if "(float)" in init or "float(" in init or "static_cast<float>" in init:
            continue
        for fn in LWC_DOUBLE_FUNCS:
            if re.search(r'\b%s\s*\(' % fn, init):
                line = code[:m.start()].count("\n") + 1
                f.warn(path, "float initialised from %s() with no explicit (float) cast (C4244)"
                       % fn, line)
                break


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
    raw_text = {}
    for name in headers + cpps:
        path = os.path.join(src, name)
        with open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
        raw_text[name] = text
        codes[name] = strip_comments_and_strings(text)

    # Base classes must be known before the shadowing check can run.
    class_members = collect_class_bases({h: codes[h] for h in headers})

    for name in headers + cpps:
        code = codes[name]
        check_balance(name, code, f)
        check_includes(name, raw_text[name], all_files, f)
        check_reserved_types(name, code, f)
        check_shadowed_engine_members(name, code, class_members, f)
        check_static_uobjects(name, code, f)
        check_cdo_misuse(name, code, f)
        check_uproperty_members(name, code, f)
        check_lwc_narrowing(name, code, f)
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
