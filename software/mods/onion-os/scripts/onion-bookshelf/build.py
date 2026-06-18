#!/usr/bin/env python3
"""Bundle books/*.txt into a single installable Onion Bookshelf Lua script.

Each book is a plain-text file in books/ with a small header:

    TITLE: An Onion
    AUTHOR: Dostoevsky

    <body... a blank line separates paragraphs; single newlines are kept
     as hard line breaks so verse holds its shape>

Files are read in filename order (use an NN- prefix). This folds every book
into the `books` table of reader.template.lua and writes the self-contained
result to onion-bookshelf.lua, ready to install on the badge.

Run from anywhere:  python3 build.py
"""

import glob
import os
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
BOOKS_DIR = os.path.join(HERE, "books")
TEMPLATE = os.path.join(HERE, "reader.template.lua")
OUTPUT = os.path.join(HERE, "onion-bookshelf.lua")
PLACEHOLDER = "__BOOKS__"


def fold_ascii(s: str) -> str:
    """Convert smart typography to plain ASCII (the e-paper font is ASCII-only)."""
    s = (s.replace("—", "--").replace("–", "-").replace("…", "...")
          .replace("‘", "'").replace("’", "'")
          .replace("“", '"').replace("”", '"')
          .replace(" ", " "))
    s = unicodedata.normalize("NFKD", s)
    return s.encode("ascii", "ignore").decode("ascii")


def parse_book(path: str) -> dict:
    title, author = "Untitled", ""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines) and lines[i].strip() != "":
        key, _, val = lines[i].partition(":")
        k = key.strip().lower()
        if k == "title":
            title = val.strip()
        elif k == "author":
            author = val.strip()
        i += 1
    body = "\n".join(lines[i:]).strip("\n")
    return {
        "title": fold_ascii(title),
        "author": fold_ascii(author),
        "text": fold_ascii(body),
    }


def lua_quote(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def lua_long_string(s: str) -> str:
    """Pick a long-bracket level that does not appear in the text."""
    level = 0
    while ("]" + "=" * level + "]") in s:
        level += 1
    eq = "=" * level
    # Lua swallows the first newline after [[, so add one to keep the text intact.
    return "[" + eq + "[\n" + s + "]" + eq + "]"


def main() -> int:
    paths = sorted(glob.glob(os.path.join(BOOKS_DIR, "*.txt")))
    if not paths:
        print("No books found in", BOOKS_DIR, file=sys.stderr)
        return 1

    entries = []
    for p in paths:
        b = parse_book(p)
        entries.append(
            "  {\n"
            f"    title = {lua_quote(b['title'])},\n"
            f"    author = {lua_quote(b['author'])},\n"
            f"    text = {lua_long_string(b['text'])},\n"
            "  },"
        )
        print(f"  + {os.path.basename(p)}: {b['title']} ({len(b['text'])} chars)")

    table = "{\n" + "\n".join(entries) + "\n}"

    with open(TEMPLATE, encoding="utf-8") as f:
        template = f.read()
    if PLACEHOLDER not in template:
        print(f"Template missing {PLACEHOLDER}", file=sys.stderr)
        return 1

    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write(template.replace(PLACEHOLDER, table))

    print(f"Wrote {os.path.basename(OUTPUT)} ({len(paths)} books)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
