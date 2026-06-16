# Onion Bookshelf

A tiny **plain-text** book reader for the badge's e-paper panel. It carries a
small shelf of books in a single Lua script: a main menu (**Library / About /
Help**), a library list, paged reading with per-book bookmarks, and an
e-paper-friendly layout tuned on the real panel. A curated onion-themed shelf
ships by default, including an original fable.

You author books as plain `.txt` files and run one Python script to fold them
into an installable `onion-bookshelf.lua` — no EPUB, no XML, no firmware build.

## What it does

- **Main menu:** Library, About, Help (UP/DOWN, SELECT, CANCEL).
- **Library:** pick from the bundled books.
- **Reading:** RIGHT/DOWN next page, LEFT/UP previous, CANCEL back to the library.
- **Bookmarks:** your page in each book is saved to NVS (`onion.kv_set`) and
  resumes after a reboot.
- **Verse-aware paging:** blank lines separate paragraphs; single newlines are
  kept as hard breaks, so poems hold their line shape while prose still wraps.
- **Tuned layout:** FreeMono9pt7b, 23 chars/line, **11 lines/page** at 14px
  spacing, right-aligned page counter.

## Which module variant does it need?

**None.** Runs on a bare badge.

## Which GPIOs does it touch?

**None directly.** It is a pure Onion OS Lua script that uses the firmware's
display (J4 socket) and button (`PBINT` / TCA9534) APIs only.

## How do I build & install?

You need an Onion OS build with the deferred-frame display API
(`display_begin` / `display_commit`) and the Lua kv store — current
`mods/onion-os` has both.

**1. Generate the script** (Python 3, stdlib only):

```sh
cd software/mods/onion-os/scripts/onion-bookshelf
python3 build.py            # books/*.txt -> onion-bookshelf.lua
```

A ready-to-run `onion-bookshelf.lua` is also committed, so this is only needed
after you change a book.

**2. Install on the badge** over WiFi with the manifest flow:

```sh
# serve this folder from your laptop (same WiFi as the badge)
python3 -m http.server 8000
```

Edit `manifest.json` so the URL points at your laptop's IP, then over serial:

```text
scripts-url http://<laptop-ip>:8000/manifest.json
```

On the badge: home menu → **Scripts** → **Update Scripts**, then run
`onion-bookshelf.lua`. (Or publish `onion-bookshelf.lua` to the Onion server's
Lua registry and push it from the portal — it is well under the 256 KB cap.)

## Adding a book

Drop a file in `books/` named `NN-title.txt`:

```text
TITLE: My Book
AUTHOR: Some Name

First paragraph...

A blank line starts a new paragraph. Single newlines are kept,
so verse keeps its line breaks.
```

Then re-run `python3 build.py`. The library menu is generated from whatever is
present — no Lua edits. Titles longer than the panel are clipped with `..`;
text is auto-folded to ASCII (smart quotes, em dashes, accents) for the
ASCII-only panel font.

## Bundled shelf

| Title | Author | Why it's here |
|-------|--------|---------------|
| An Onion | Dostoevsky | Grushenka's onion fable, *The Brothers Karamazov* |
| The Onion of Fable | Claude & Tippi Fifestarr | an original onion allegory |
| Chicago | Carl Sandburg | the city named for the wild onion |
| Recipe for a Salad | Sydney Smith | "Let onion atoms lurk within the bowl" |

All public domain (or our own). A fifth onion piece is an open slot. 🧅

## Relationship to `epub-reader` (PR #5)

We built this independently and only discovered [PR
#5](https://github.com/OnionDAO-git/oniondao-badge/pull/5) afterward — and Tippi
Fifestarr does love a 5. They turn out to be **different tools**:

- **`epub-reader`** is a full EPUB pipeline (unzip-free `.epub` parsing, TOC,
  multi-part split for huge books, chapter menu, tests). Great for reading whole
  novels you bring yourself.
- **Onion Bookshelf** is a zero-EPUB **`.txt`** reader: a curated multi-book
  shelf in one script, verse-aware, with a denser 11-lines/page layout
  (vs. 8), authored by dropping text files in a folder.

They're complementary, and we'd happily explore merging the best of both — a
shared reader runtime with both a `.txt` shelf and an EPUB importer. Pinging
@quindelin in the PR.

## Credits

Reader and converter by **Claude & Tippi Fifestarr**. MIT (see repo default).
Book texts are public domain; *The Onion of Fable* is original, released MIT.
