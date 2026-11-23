#!/usr/bin/env python3
"""Writes Docs/API.md, the reference of what a game can use, from the engine's headers.

The reference is the headers' own comments: what `#include "MoltenMetal.hpp"` brings in, each
header with the comment at its top, then every declaration that has a `///` comment above it
(or a `//` comment after it, on a member) under the class or struct it belongs to. Nothing
is written by hand here, so the reference cannot drift from the code; run this after changing
a public header:

    python3 Tools/make_api_docs.py          # writes Docs/API.md
    python3 Tools/make_api_docs.py --check  # fails if Docs/API.md is out of date
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "Engine"
OUT = ROOT / "Docs" / "API.md"

# Headers that are included for the engine's own use and say nothing to a game.
SKIPPED = {"Core/Log.hpp"}

OPENS_TYPE = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?(class|struct)\s+(?:MOLTEN_\w+\s+)?(\w+)\b(?!\s*;)")
OPENS_ENUM = re.compile(r"^\s*enum\s+(?:class\s+)?(\w+)")
NAMESPACE = re.compile(r"^\s*namespace\s+([\w:]+)\s*\{")


def public_headers():
    seen = []
    for line in (ENGINE / "MoltenMetal.hpp").read_text().splitlines():
        m = re.match(r'#include "(.+)"', line)
        if m and m.group(1) not in SKIPPED and (ENGINE / m.group(1)).exists():
            seen.append(m.group(1))
    return seen


def header_intro(lines):
    """The comment at the top of a header, without its first line (the file's name)."""
    intro = []
    for line in lines:
        s = line.strip()
        if not s.startswith("//"):
            break
        intro.append(s[2:].strip() if not s.startswith("// ") else s[3:])
    if intro and intro[0].endswith(".hpp"):
        intro = intro[1:]
    return intro


def strip_body(decl):
    """A declaration as a reader wants it: one line, without its body or default member values that run long."""
    decl = " ".join(decl.split())
    # an inline body: keep what comes before it
    depth = 0
    for i, c in enumerate(decl):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "{" and depth == 0:
            head = decl[:i].rstrip()
            # "Vec3 axis{0, 1, 0};" is a value, not a body: keep it
            if re.search(r"[\w\]>]\s*$", head) and not head.endswith(")") and not head.endswith("const") and "operator" not in head:
                return decl.rstrip(";").rstrip() + ";" if decl.endswith(";") else decl
            return head + (";" if not head.endswith(";") else "")
    return decl if decl.endswith(";") else decl + ";"


def parse(path):
    lines = (ENGINE / path).read_text().splitlines()
    intro = header_intro(lines)
    sections = {}          # scope name -> list of (declaration, comment)
    order = []
    scope = []             # (name or None, depth it opened at)
    depth = 0
    pending = []           # the /// lines waiting for their declaration
    i = 0
    in_block_comment = False

    def add(name, decl, comment):
        key = name or ""
        if key not in sections:
            sections[key] = []
            order.append(key)
        sections[key].append((decl, comment))

    while i < len(lines):
        raw = lines[i]
        s = raw.strip()
        if in_block_comment:
            if "*/" in s:
                in_block_comment = False
            i += 1
            continue
        if s.startswith("/*"):
            in_block_comment = "*/" not in s
            i += 1
            continue
        if s.startswith("///"):
            pending.append(s[3:].strip())
            i += 1
            continue
        if s.startswith("#define"):
            # a macro: its name and arguments, and none of its body
            m = re.match(r"#define\s+(\w+(?:\([^)]*\))?)", s)
            if m and pending and not m.group(1).startswith(("MOLTEN_PRINTF", "MOLTEN_EXPORT")):
                add(scope[-1][0] if scope else None, m.group(1), " ".join(pending))
            pending = []
            while lines[i].rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
            i += 1
            continue
        if not s or s.startswith("//") or s.startswith("#"):
            if not s.startswith("//"):
                pending = [] if not s else pending
            i += 1
            continue
        here = scope[-1][0] if scope else None
        private = here is not None and scope[-1][2]

        m_type = OPENS_TYPE.match(raw)
        m_enum = OPENS_ENUM.match(raw)
        if s in ("private:", "protected:"):
            if scope:
                scope[-1] = (scope[-1][0], scope[-1][1], True)
            pending = []
            i += 1
            continue
        if s == "public:":
            if scope:
                scope[-1] = (scope[-1][0], scope[-1][1], False)
            i += 1
            continue

        # gather a whole declaration: up to ; or the { that opens a body, at this depth
        decl = s
        j = i
        while not re.search(r"[;{}]\s*(//.*)?$", decl) and j + 1 < len(lines) and not lines[j + 1].strip().startswith("//"):
            j += 1
            decl += " " + lines[j].strip()
        trailing = ""
        m_trail = re.search(r"\s//\s?(.*)$", decl)
        if m_trail and "///" not in decl:
            trailing = m_trail.group(1).strip()
            decl = decl[: m_trail.start()].rstrip()

        if NAMESPACE.match(raw):
            pending = []
            depth += decl.count("{") - decl.count("}")
            i = j + 1
            continue

        if m_type and "{" in decl and not decl.rstrip().endswith(";") or (m_type and decl.rstrip().endswith("{")):
            name = m_type.group(2)
            full = (here + "::" + name) if here else name
            if not private:
                comment = " ".join(pending) or trailing
                if comment or not here:
                    add(here, m_type.group(1) + " " + name, comment)
                if full not in sections:
                    sections[full] = []
                    order.append(full)
            pending = []
            depth += decl.count("{") - decl.count("}")
            is_struct = m_type.group(1) == "struct"
            scope.append((full, depth, (not is_struct) or private))
            i = j + 1
            continue

        if m_enum and "{" in decl:
            body = decl
            k = j
            while body.count("{") > body.count("}") and k + 1 < len(lines):
                k += 1
                body += " " + lines[k].strip()
            values = re.sub(r"//[^,}]*", "", body[body.find("{") + 1 : body.rfind("}")])
            values = ", ".join(v.split("=")[0].strip() for v in values.split(",") if v.strip())
            if not private:
                add(here, f"enum {m_enum.group(1)} {{ {values} }}", " ".join(pending) or trailing)
            pending = []
            i = k + 1
            continue

        opened = decl.count("{") - decl.count("}")
        comment = " ".join(pending) or trailing
        is_member_like = not s.startswith(("}", "using namespace", "friend", "static_assert", "MOLTEN_", "return", "if ", "for "))
        if comment and is_member_like and not private:
            add(here, strip_body(decl), comment)
        pending = []
        depth += opened
        # a body that opened here: skip to where it closes
        if opened > 0 and not (m_type or m_enum):
            k = j
            while depth > (scope[-1][1] if scope else 0) and k + 1 < len(lines) and (not scope or depth > scope[-1][1]):
                k += 1
                t = lines[k]
                t = re.sub(r"//.*$", "", t)
                t = re.sub(r'"(\\.|[^"\\])*"', '""', t)
                t = re.sub(r"'(\\.|[^'\\])*'", "''", t)
                depth += t.count("{") - t.count("}")
                if scope and depth < scope[-1][1]:
                    break
            j = k
        while scope and depth < scope[-1][1]:
            scope.pop()
        i = j + 1
    return intro, [(k, sections[k]) for k in order]


def render_intro(intro):
    """A header's top comment as markdown: paragraphs, its indented code as code, its numbered lines as a list."""
    out, para, code = [], [], []

    def flush():
        if para:
            out.append(" ".join(para))
            out.append("")
            para.clear()
        if code:
            while code and not code[-1].strip():
                code.pop()
            out.extend(["```cpp"] + code + ["```", ""])
            code.clear()

    for line in intro:
        if line.startswith("    ") and not re.match(r"\s+(\d+\.|-)\s", line):
            if para:
                flush()
            code.append(line[4:])
        elif not line.strip():
            if code:
                code.append("")
            else:
                flush()
        elif re.match(r"\s*(\d+\.|-)\s", line):
            if code:
                flush()
            if para:
                flush()
            out.append(line.strip())
        else:
            if code:
                flush()
            if out and re.match(r"(\d+\.|-)\s", out[-1]) and not para:
                out.append("")
            para.append(line.strip())
    flush()
    while out and not out[-1]:
        out.pop()
    return out


def anchor(text):
    return re.sub(r"[^a-z0-9 -]", "", text.lower()).replace(" ", "-")


def render():
    headers = public_headers()
    out = [
        "# API reference",
        "",
        "Everything `#include \"MoltenMetal.hpp\"` gives a game, header by header, in the headers' own words.",
        "This page is generated by `Tools/make_api_docs.py` from the comments in `Engine/`; edit those, not this.",
        "How a game is put together is in [MakingGames.md](MakingGames.md) and [GameCode.md](GameCode.md); the",
        "files the editor writes are in [FileFormats.md](FileFormats.md).",
        "",
        "What is here is the promise: a game that uses only what this page lists keeps building across 1.x releases.",
        "A change to any of it is written in the ROADMAP's release notes with what to do instead.",
        "",
        "## Headers",
        "",
    ]
    bodies = []
    for h in headers:
        intro, sections = parse(h)
        documented = sum(len(items) for _, items in sections)
        if documented == 0 and not intro:
            continue
        first = intro[0] if intro else ""
        out.append(f"- [{h}](#{anchor(h)}){(': ' + first) if first else ''}")
        body = [f"## {h}", ""]
        if intro:
            body += render_intro(intro) + [""]
        for scope, items in sections:
            if not items:
                continue
            if scope:
                body += [f"### {scope}", ""]
            for decl, comment in items:
                body.append(f"- `{decl}`" + (f" — {comment}" if comment else ""))
            body.append("")
        bodies.append("\n".join(body))
    out.append("")
    return "\n".join(out) + "\n" + "\n".join(bodies)


def main():
    text = render()
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text() != text:
            print("Docs/API.md is out of date: run python3 Tools/make_api_docs.py")
            return 1
        return 0
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(ROOT)}: {text.count(chr(10))} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
