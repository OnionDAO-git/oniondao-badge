#!/usr/bin/env python3
"""Convert an EPUB (or plain .txt) book into Onion OS badge reader scripts.

The OnionDAO badge runs Lua scripts but has no unzip or XML support, so this
tool does the heavy lifting on a laptop: it extracts the book text, reflows it
for the 264x176 e-paper panel (FreeMono9pt7b, 11 px per char, ASCII only), and
emits one or more self-contained .lua files = generated BOOK table + the
reader runtime from reader_template.lua. Books larger than one script's budget
are split into parts (alice-p1.lua, alice-p2.lua, ...).

Stdlib only. See README.md for the install-on-badge workflow.
"""

import argparse
import hashlib
import html.parser
import json
import posixpath
import re
import sys
import textwrap
import unicodedata
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

# Onion OS firmware caps scripts at 256 KB (MAX_SCRIPT_BYTES); SPIFFS is
# 1.5 MB shared with everything else. Keep generous headroom.
HARD_FILE_LIMIT = 250_000
DEFAULT_BUDGET = 170_000

# FreeMono9pt7b: every glyph advances 11 px, ascent ~11 px, descent ~4 px.
CHAR_W = 11
ASCENT = 11
DESCENT = 4
PANEL_W = 264
PANEL_H = 176
TEXT_X = 3

MENU_TITLE_MAX = 21


# --------------------------------------------------------------------------
# ASCII transliteration (the badge font covers 0x20-0x7E only)

CHAR_MAP = {
    "\u2018": "'", "\u2019": "'", "\u201a": "'", "\u201b": "'",
    "\u2032": "'", "\u02bc": "'",
    "\u201c": '"', "\u201d": '"', "\u201e": '"', "\u201f": '"',
    "\u2033": '"', "\u00ab": '"', "\u00bb": '"',
    "\u2010": "-", "\u2011": "-", "\u2012": "-", "\u2013": "-",
    "\u2212": "-", "\u2014": "--", "\u2015": "--",
    "\u2026": "...",
    "\u2022": "*", "\u00b7": "*", "\u25aa": "*", "\u25cf": "*",
    "\u2044": "/",
    # nbsp, ogham, en/em/thin/hair/figure/punctuation spaces, narrow nbsp,
    # math space, ideographic space
    "\u00a0": " ", "\u1680": " ", "\u2000": " ", "\u2001": " ",
    "\u2002": " ", "\u2003": " ", "\u2004": " ", "\u2005": " ",
    "\u2006": " ", "\u2007": " ", "\u2008": " ", "\u2009": " ",
    "\u200a": " ", "\u202f": " ", "\u205f": " ", "\u3000": " ",
    # zero-width space/joiners, soft hyphen, BOM, directional marks
    "\u200b": "", "\u200c": "", "\u200d": "", "\u00ad": "",
    "\ufeff": "", "\u200e": "", "\u200f": "",
    "\u00d7": "x", "\u00f7": "/", "\u00b0": " deg",
}

_replaced_chars = 0


def to_ascii(text: str) -> str:
    """Map text into the FreeMono 0x20-0x7E range, counting lossy chars."""
    global _replaced_chars
    out = []
    for ch in text:
        if ch in CHAR_MAP:
            out.append(CHAR_MAP[ch])
            continue
        o = ord(ch)
        if ch == "\n" or 32 <= o <= 126:
            out.append(ch)
            continue
        if o < 32:
            out.append(" ")
            continue
        decomposed = unicodedata.normalize("NFKD", ch)
        kept = ""
        for d in decomposed:
            if d in CHAR_MAP:
                kept += CHAR_MAP[d]
            elif 32 <= ord(d) <= 126:
                kept += d
            elif unicodedata.combining(d):
                continue
        if kept:
            out.append(kept)
        else:
            out.append("?")
            _replaced_chars += 1
    return "".join(out)


# --------------------------------------------------------------------------
# HTML -> text blocks

BLOCK_TAGS = {
    "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "ul", "ol",
    "blockquote", "pre", "tr", "td", "th", "table", "section", "article",
    "aside", "figure", "figcaption", "dd", "dt", "dl", "nav", "header",
    "footer", "address", "center",
}
SKIP_TAGS = {"script", "style", "head", "title", "svg", "math", "noscript"}
HEADING_TAGS = {"h1", "h2", "h3", "h4", "h5", "h6"}


class Block:
    __slots__ = ("kind", "text", "level")

    def __init__(self, kind, text, level=0):
        self.kind = kind    # "p", "h", "hr"
        self.text = text
        self.level = level  # heading level for kind == "h"


class TextExtractor(html.parser.HTMLParser):
    """Flatten one XHTML document into Blocks plus id->block anchors."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.blocks = []
        self.anchors = {}
        self._buf = []
        self._skip = 0
        self._heading = 0
        self._prefix = ""

    def _flush(self):
        raw = "".join(self._buf)
        self._buf = []
        segments = []
        for seg in raw.split("\n"):
            seg = re.sub(r"\s+", " ", seg).strip()
            if seg:
                segments.append(seg)
        if not segments:
            self._prefix = ""
            return
        text = "\n".join(segments)
        if self._prefix:
            text = self._prefix + text
            self._prefix = ""
        if self._heading:
            self.blocks.append(Block("h", text, self._heading))
        else:
            self.blocks.append(Block("p", text))

    def handle_starttag(self, tag, attrs):
        if tag in SKIP_TAGS:
            self._skip += 1
            return
        if self._skip:
            return
        attrs = dict(attrs)
        anchor = attrs.get("id") or (attrs.get("name") if tag == "a" else None)
        if anchor:
            self.anchors.setdefault(anchor, len(self.blocks))
        if tag == "br":
            self._buf.append("\n")
        elif tag == "hr":
            self._flush()
            self.blocks.append(Block("hr", ""))
        elif tag in BLOCK_TAGS:
            self._flush()
            if tag in HEADING_TAGS:
                self._heading = int(tag[1])
            elif tag == "li":
                self._prefix = "- "

    def handle_endtag(self, tag):
        if tag in SKIP_TAGS:
            if self._skip:
                self._skip -= 1
            return
        if self._skip:
            return
        if tag in BLOCK_TAGS:
            self._flush()
            if tag in HEADING_TAGS:
                self._heading = 0

    def handle_data(self, data):
        if not self._skip and data:
            # Newlines in the source markup are formatting, not line breaks;
            # only <br> inserts a real "\n" into the buffer.
            self._buf.append(re.sub(r"[\r\n\t]+", " ", data))

    def close(self):
        super().close()
        self._flush()
        for k, v in self.anchors.items():
            if v >= len(self.blocks):
                self.anchors[k] = max(0, len(self.blocks) - 1)


class NavTocExtractor(html.parser.HTMLParser):
    """Pull (href, title) pairs out of an EPUB3 nav document's toc <nav>."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.entries = []
        self._nav_depth = 0
        self._in_toc = False
        self._href = None
        self._text = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "nav":
            self._nav_depth += 1
            nav_type = attrs.get("epub:type", attrs.get("type", ""))
            if "toc" in nav_type.split():
                self._in_toc = True
        elif self._in_toc and tag == "a" and attrs.get("href"):
            self._href = attrs["href"]
            self._text = []

    def handle_endtag(self, tag):
        if tag == "nav":
            self._nav_depth -= 1
            if self._nav_depth <= 0:
                self._in_toc = False
        elif tag == "a" and self._href is not None:
            title = re.sub(r"\s+", " ", "".join(self._text)).strip()
            if title:
                self.entries.append((self._href, title))
            self._href = None

    def handle_data(self, data):
        if self._href is not None:
            self._text.append(data)


# --------------------------------------------------------------------------
# EPUB container

def ns_strip(tag):
    return tag.rsplit("}", 1)[-1]


class EpubBook:
    def __init__(self, path: Path):
        self.zf = zipfile.ZipFile(path)
        self.names = set(self.zf.namelist())
        container = ET.fromstring(self.zf.read("META-INF/container.xml"))
        rootfile = None
        for el in container.iter():
            if ns_strip(el.tag) == "rootfile":
                rootfile = el.get("full-path")
                break
        if not rootfile:
            raise SystemExit("EPUB: no rootfile in META-INF/container.xml")
        self.opf_path = rootfile
        self.opf_dir = posixpath.dirname(rootfile)
        self.opf = ET.fromstring(self.zf.read(rootfile))
        self.title = "Untitled"
        self.author = ""
        self.manifest = {}       # id -> (href, media_type, properties)
        self.spine = []          # zip paths in reading order
        self.ncx_path = None
        self.nav_path = None
        self._parse_opf()

    def resolve(self, href, base_dir=None):
        href = urllib.parse.unquote(href.split("#")[0])
        base = self.opf_dir if base_dir is None else base_dir
        return posixpath.normpath(posixpath.join(base, href)) if base else posixpath.normpath(href)

    def _parse_opf(self):
        for el in self.opf.iter():
            tag = ns_strip(el.tag)
            if tag == "title" and el.text and self.title == "Untitled":
                self.title = el.text.strip() or "Untitled"
            elif tag == "creator" and el.text and not self.author:
                self.author = el.text.strip()
            elif tag == "item":
                item_id = el.get("id")
                href = el.get("href", "")
                media = el.get("media-type", "")
                props = el.get("properties", "")
                self.manifest[item_id] = (href, media, props)
                if media == "application/x-dtbncx+xml":
                    self.ncx_path = self.resolve(href)
                if "nav" in props.split():
                    self.nav_path = self.resolve(href)
        spine_ncx = None
        for el in self.opf.iter():
            tag = ns_strip(el.tag)
            if tag == "spine":
                spine_ncx = el.get("toc")
            elif tag == "itemref":
                if el.get("linear", "yes") == "no":
                    continue
                item = self.manifest.get(el.get("idref"))
                if not item:
                    continue
                href, media, _props = item
                if media in ("application/xhtml+xml", "text/html", "application/html"):
                    path = self.resolve(href)
                    if path in self.names:
                        self.spine.append(path)
        if spine_ncx and not self.ncx_path:
            item = self.manifest.get(spine_ncx)
            if item:
                self.ncx_path = self.resolve(item[0])
        if not self.spine:
            raise SystemExit("EPUB: spine has no readable XHTML documents")

    def read_text(self, zip_path):
        data = self.zf.read(zip_path)
        for enc in ("utf-8", "utf-16", "latin-1"):
            try:
                return data.decode(enc)
            except UnicodeDecodeError:
                continue
        return data.decode("utf-8", errors="replace")

    def toc_entries(self):
        """[(zip_path, fragment_or_None, title)] from NCX or EPUB3 nav."""
        entries = []
        if self.ncx_path and self.ncx_path in self.names:
            try:
                ncx = ET.fromstring(self.zf.read(self.ncx_path))
                ncx_dir = posixpath.dirname(self.ncx_path)
                for nav_point in ncx.iter():
                    if ns_strip(nav_point.tag) != "navPoint":
                        continue
                    title, src = None, None
                    for child in nav_point:
                        tag = ns_strip(child.tag)
                        if tag == "navLabel":
                            for t in child.iter():
                                if ns_strip(t.tag) == "text" and t.text:
                                    title = t.text.strip()
                                    break
                        elif tag == "content":
                            src = child.get("src")
                    if title and src:
                        frag = src.split("#")[1] if "#" in src else None
                        entries.append((self.resolve(src, ncx_dir), frag, title))
            except ET.ParseError:
                pass
        if not entries and self.nav_path and self.nav_path in self.names:
            nav = NavTocExtractor()
            nav.feed(self.read_text(self.nav_path))
            nav_dir = posixpath.dirname(self.nav_path)
            for href, title in nav.entries:
                frag = href.split("#")[1] if "#" in href else None
                entries.append((self.resolve(href, nav_dir), frag, title))
        return entries


# --------------------------------------------------------------------------
# Layout / pagination

class Layout:
    def __init__(self, rows, cols, line_h):
        self.rows = rows
        self.cols = cols
        self.line_h = line_h
        self.text_y = 14                       # first baseline
        bottom = self.text_y + line_h * (rows - 1) + DESCENT
        self.rule_y = bottom + 10
        self.foot_y = min(self.rule_y + 25, PANEL_H - 6)
        if TEXT_X + cols * CHAR_W > PANEL_W:
            raise SystemExit(f"--cols {cols} does not fit: max {(PANEL_W - TEXT_X) // CHAR_W}")
        if self.foot_y - self.rule_y < 18 or self.rule_y >= PANEL_H - 12:
            raise SystemExit(f"--rows {rows} x --line-height {line_h} does not fit the 176 px panel")
        band = bottom - (self.text_y - ASCENT) + 1
        self.partial_ok = band / PANEL_H <= 0.74

    def describe(self):
        mode = "partial (fast) page turns" if self.partial_ok else \
               "FULL (flashy ~1.7 s) page turns - text band exceeds the 75% partial budget"
        return f"{self.rows} rows x {self.cols} cols, line height {self.line_h}px -> {mode}"


def wrap_block(text, cols, indent_first):
    lines = []
    for i, seg in enumerate(text.split("\n")):
        seg = seg.strip()
        if not seg:
            continue
        indent = "  " if (indent_first and i == 0) else ""
        wrapped = textwrap.wrap(
            seg, width=cols, initial_indent=indent,
            break_long_words=True, break_on_hyphens=True)
        lines.extend(wrapped or [])
    return lines


def center_lines(text, cols):
    out = []
    for seg in text.split("\n"):
        for line in textwrap.wrap(seg.strip(), width=cols) or []:
            out.append(line.center(cols).rstrip())
    return out


class Paginator:
    def __init__(self, layout: Layout):
        self.layout = layout
        self.pages = []
        self.cur = []
        self.chapters = []  # (global_page, title)

    def flush(self):
        while self.cur and not self.cur[-1].strip():
            self.cur.pop()
        if self.cur:
            self.pages.append("\n".join(self.cur))
        self.cur = []

    def mark_chapter(self, title):
        self.flush()
        title = re.sub(r"\s+", " ", to_ascii(title)).strip()[:MENU_TITLE_MAX]
        page = len(self.pages) + 1
        if self.chapters and self.chapters[-1][0] == page:
            return  # two TOC entries landing on one page: keep the first
        self.chapters.append((page, title or f"Chapter {len(self.chapters) + 1}"))

    def _add_line(self, line):
        if len(self.cur) >= self.layout.rows:
            self.flush()
        if not self.cur and not line.strip():
            return
        self.cur.append(line)

    def add_lines(self, lines, blank_before=False):
        if not lines:
            return
        if blank_before and self.cur:
            self._add_line("")
        for line in lines:
            self._add_line(line)

    def add_block(self, block: Block):
        cols = self.layout.cols
        if block.kind == "hr":
            self.add_lines(["* * *".center(cols).rstrip()], blank_before=True)
            self._add_line("")
        elif block.kind == "h":
            self.add_lines(center_lines(block.text, cols), blank_before=True)
            self._add_line("")
        else:
            self.add_lines(wrap_block(block.text, cols, indent_first=True))


# --------------------------------------------------------------------------
# Book assembly

def load_epub(path: Path):
    book = EpubBook(path)
    docs = []      # (zip_path, blocks, anchors)
    for zip_path in book.spine:
        extractor = TextExtractor()
        try:
            extractor.feed(book.read_text(zip_path))
            extractor.close()
        except Exception as exc:  # tolerate odd markup, keep going
            print(f"warning: {zip_path}: {exc}", file=sys.stderr)
        docs.append((zip_path, extractor.blocks, extractor.anchors))

    spine_index = {zip_path: i for i, (zip_path, _b, _a) in enumerate(docs)}
    chapter_marks = {}  # (file_idx, block_idx) -> title
    for zip_path, frag, title in book.toc_entries():
        file_idx = spine_index.get(zip_path)
        if file_idx is None:
            continue
        block_idx = 0
        if frag is not None:
            block_idx = docs[file_idx][2].get(frag, 0)
        chapter_marks.setdefault((file_idx, block_idx), title)

    if not chapter_marks:
        for file_idx, (_path, blocks, _anchors) in enumerate(docs):
            for block_idx, block in enumerate(blocks):
                if block.kind == "h" and block.level <= 2:
                    chapter_marks[(file_idx, block_idx)] = block.text.split("\n")[0]
                    break

    return book.title, book.author, docs, chapter_marks


def load_txt(path: Path, title):
    raw = path.read_bytes()
    for enc in ("utf-8", "latin-1"):
        try:
            text = raw.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    else:
        text = raw.decode("utf-8", errors="replace")
    heading_re = re.compile(
        r"^(CHAPTER|Chapter|BOOK|Book|PART|Part|CANTO|[IVXLCDM]+\.?)([\s.:].{0,40})?$")
    blocks = []
    chapter_marks = {}
    for para in re.split(r"\n\s*\n", text):
        para = re.sub(r"[ \t]+", " ", para).strip()
        if not para:
            continue
        first = para.split("\n")[0].strip()
        if len(para.split("\n")) <= 2 and len(first) <= 48 and heading_re.match(first):
            chapter_marks[(0, len(blocks))] = first
            blocks.append(Block("h", para.replace("\n", " "), 1))
        else:
            blocks.append(Block("p", " ".join(s.strip() for s in para.split("\n"))))
    docs = [(str(path), blocks, {})]
    return title or path.stem.replace("_", " ").title(), "", docs, chapter_marks


def paginate(docs, chapter_marks, layout):
    pager = Paginator(layout)
    for file_idx, (_path, blocks, _anchors) in enumerate(docs):
        for block_idx, block in enumerate(blocks):
            title = chapter_marks.get((file_idx, block_idx))
            if title is not None:
                pager.mark_chapter(title)
            block.text = to_ascii(block.text)
            pager.add_block(block)
    pager.flush()
    pager.chapters = [(p, t) for p, t in pager.chapters if p <= len(pager.pages)]
    return pager.pages, pager.chapters


# --------------------------------------------------------------------------
# Lua emission

def lua_quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def bracket_level(pages):
    level = 2
    while any(("]" + "=" * level + "]") in page for page in pages):
        level += 1
    return level


def end_page(layout, part, parts, next_name):
    cols = layout.cols
    lines = ["", "* * *".center(cols).rstrip(), ""]
    if part < parts:
        lines.append(f"End of part {part}/{parts}".center(cols).rstrip())
        lines.append("Next:".center(cols).rstrip())
        lines.append(next_name.center(cols).rstrip())
    else:
        lines.append("THE END".center(cols).rstrip())
    return "\n".join(lines[: layout.rows])


def split_parts(pages, budget):
    parts, acc, acc_bytes = [], [], 0
    for page in pages:
        cost = len(page.encode()) + 12
        if acc and acc_bytes + cost > budget:
            parts.append(acc)
            acc, acc_bytes = [], 0
        acc.append(page)
        acc_bytes += cost
    if acc:
        parts.append(acc)
    return parts


def make_label(title, part, parts):
    base = re.sub(r"\s+", " ", title).strip()
    if parts > 1:
        return f"{base[:7].rstrip()} {part}/{parts}"
    return base[:12].rstrip()


def emit_part(out_dir, template, layout, title, book_id, slug,
              part_pages, part_no, parts_total, chapters_local):
    name = f"{slug}.lua" if parts_total == 1 else f"{slug}-p{part_no}.lua"
    next_name = None
    if part_no < parts_total:
        next_name = f"{slug}-p{part_no + 1}.lua"
    pages = list(part_pages)
    pages.append(end_page(layout, part_no, parts_total, next_name or ""))

    level = bracket_level(pages)
    op, cl = "[" + "=" * level + "[", "]" + "=" * level + "]"

    chapter_rows = "".join(
        f"        {{ p = {p}, t = {lua_quote(t)} }},\n" for p, t in chapters_local)
    page_rows = "".join(f"{op}\n{page}{cl},\n" for page in pages)

    header = (
        f"-- {title}" + (f" (part {part_no}/{parts_total})" if parts_total > 1 else "") + "\n"
        "-- Generated by epub2badge.py for the OnionDAO badge EPUB reader.\n"
        "-- RIGHT/DOWN next page, LEFT/UP previous, SELECT chapters, CANCEL exit.\n"
        "BOOK = {\n"
        f"    title = {lua_quote(title[:40])},\n"
        f"    label = {lua_quote(make_label(title, part_no, parts_total))},\n"
        f"    id = {lua_quote(book_id)},\n"
        f"    part = {part_no},\n"
        f"    parts = {parts_total},\n"
        f"    layout = {{ x = {TEXT_X}, y = {layout.text_y}, lh = {layout.line_h}, "
        f"rows = {layout.rows}, rule = {layout.rule_y}, foot = {layout.foot_y} }},\n"
        "    chapters = {\n" + chapter_rows + "    },\n"
        "    pages = {\n" + page_rows + "    },\n"
        "}\n\n"
    )
    content = header + template
    data = content.encode()
    if len(data) > HARD_FILE_LIMIT:
        raise SystemExit(
            f"{name} would be {len(data)} bytes (> {HARD_FILE_LIMIT}); lower --budget")
    out_path = out_dir / name
    out_path.write_bytes(data)
    return name, len(data), len(pages)


def validate_pages(pages, layout):
    for i, page in enumerate(pages, 1):
        lines = page.split("\n")
        if len(lines) > layout.rows:
            raise SystemExit(f"internal error: page {i} has {len(lines)} lines")
        for line in lines:
            if len(line) > layout.cols:
                raise SystemExit(f"internal error: page {i} line too long: {line!r}")
            for ch in line:
                if not 32 <= ord(ch) <= 126:
                    raise SystemExit(f"internal error: page {i} non-ASCII {ch!r}")


def make_slug(value):
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return (slug[:10].rstrip("-")) or "book"


def main():
    ap = argparse.ArgumentParser(
        description="Convert an EPUB or plain-text book for the OnionDAO badge reader.")
    ap.add_argument("book", type=Path, help="input .epub or .txt file")
    ap.add_argument("-o", "--out-dir", type=Path, default=Path("out"))
    ap.add_argument("--slug", help="output base name (default: from title)")
    ap.add_argument("--title", help="override the book title")
    ap.add_argument("--rows", type=int, default=8,
                    help="text lines per page (default 8; >8 forces flashy full-refresh turns)")
    ap.add_argument("--cols", type=int, default=23, help="characters per line (max 23)")
    ap.add_argument("--line-height", type=int, default=16, help="pixels per line (default 16)")
    ap.add_argument("--budget", type=int, default=DEFAULT_BUDGET,
                    help=f"max page-text bytes per part (default {DEFAULT_BUDGET})")
    ap.add_argument("--base-url", default="http://BADGE-HOST:8000",
                    help="URL prefix written into manifest.json (your laptop's http server)")
    args = ap.parse_args()

    layout = Layout(args.rows, args.cols, args.line_height)
    template_path = Path(__file__).resolve().parent / "reader_template.lua"
    template = template_path.read_text()

    if args.book.suffix.lower() == ".epub":
        title, author, docs, chapter_marks = load_epub(args.book)
    else:
        title, author, docs, chapter_marks = load_txt(args.book, args.title)
    if args.title:
        title = args.title
    title = to_ascii(re.sub(r"\s+", " ", title)).strip() or "Untitled"

    pages, chapters = paginate(docs, chapter_marks, layout)
    if not pages:
        raise SystemExit("no text extracted from book")
    validate_pages(pages, layout)

    book_id = hashlib.sha1(f"{title}|{author}|{len(pages)}".encode()).hexdigest()[:6]
    slug = make_slug(args.slug or title)
    parts = split_parts(pages, args.budget)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    manifest_scripts = []
    print(f"Title:    {title}" + (f" — {author}" if author else ""))
    print(f"Layout:   {layout.describe()}")
    print(f"Pages:    {len(pages)}  Chapters: {len(chapters)}  Parts: {len(parts)}")

    start = 1
    for part_no, part_pages in enumerate(parts, 1):
        end = start + len(part_pages) - 1
        chapters_local = [(p - start + 1, t) for p, t in chapters if start <= p <= end]
        if part_no > 1 and (not chapters_local or chapters_local[0][0] != 1):
            chapters_local.insert(0, (1, f"Part {part_no} start"))
        name, size, page_count = emit_part(
            args.out_dir, template, layout, title, book_id, slug,
            part_pages, part_no, len(parts), chapters_local)
        manifest_scripts.append({
            "name": name,
            "url": args.base_url.rstrip("/") + "/" + name,
            "autorun": False,
        })
        print(f"  {name}: {size:,} bytes, {page_count} pages (global {start}-{end})")
        start = end + 1

    manifest_path = args.out_dir / "manifest.json"
    manifest_path.write_text(json.dumps({"scripts": manifest_scripts}, indent=2) + "\n")
    print(f"  manifest.json -> {args.base_url}")
    if _replaced_chars:
        print(f"Replaced {_replaced_chars} unmappable character(s) with '?'")


if __name__ == "__main__":
    main()
