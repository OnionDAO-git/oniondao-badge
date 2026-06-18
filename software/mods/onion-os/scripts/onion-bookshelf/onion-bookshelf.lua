-- Onion Bookshelf — a tiny e-paper reader for plain-text books on the badge.
--
-- NOTE: this is a TEMPLATE. The `books` table below is filled in at build time
-- by the generator from the plain-text files in books/. Do not edit the
-- generated script by hand — edit a book .txt or this template, then re-run it.
--
-- Controls
--   Menu:     UP / DOWN choose, SELECT opens, CANCEL exits to Onion OS.
--   Library:  UP / DOWN choose a book, SELECT opens it, CANCEL back to menu.
--   Reading:  RIGHT / DOWN = next page, LEFT / UP = previous page,
--             CANCEL = back to the library.
--   About / Help: SELECT or CANCEL returns to the menu.
--
-- Your place in each book is saved (onion.kv_set), so you resume where you
-- left off — even after a reboot.
--
-- Version 0.3 (shown on the About screen).
--
-- ROADMAP (next steps)
--   * one more onion book, toward five.
--   * borrow from epub-reader (#5): hold-to-repeat paging, a progress %, and
--     the two-commit partial-refresh that dodges ghosting.
--   * more on-ramps for text: this .txt path, an EPUB importer (#5's
--     converter), maybe IPFS for sharing/pinning shelves badge-to-badge.
--   * maybe a shared reader core with pluggable sources (combine with #5).
--
-- NOTES FROM epub-reader (#5), per quindelin's handoff:
--   * pre-bake pages at build time -> chapter jumps are O(1), no on-device
--     repagination (a 4,700-page jump costs one page turn).
--   * SPIFFS is the real budget: big multi-part books (~170 KB/part) can fill
--     the ~1.5 MB partition (he froze a badge mid-sync). Watch free space.
--   * two partials per turn reads smooth; the every-30 full refresh is a
--     deliberate "clean blink," not a glitch.
--   * ASCII-only font: technical PDFs lose tables/equations; non-Latin -> "?".

----------------------------------------------------------------------
-- 1. THE LIBRARY  (generated from books/*.txt — see build.py)
----------------------------------------------------------------------
local VERSION = "0.3"   -- bump as you iterate; shown on the About screen
local books = {
  {
    title = "An Onion",
    author = "Dostoevsky",
    text = [[
Grushenka's tale, from The Brothers Karamazov.

Once upon a time there was a peasant woman and a very wicked woman she was. And she died and did not leave a single good deed behind. The devils caught her and plunged her into the lake of fire.

So her guardian angel stood and wondered what good deed of hers he could remember to tell to God. "She once pulled up an onion in her garden," said he, "and gave it to a beggar woman."

And God answered: "You take that onion then, hold it out to her in the lake, and let her take hold and be pulled out. If you can pull her out, let her come to Paradise, but if the onion breaks, the woman must stay where she is."

The angel ran to the woman and held out the onion. "Come," said he, "catch hold and I'll pull you out." And he began cautiously pulling her out.

He had just pulled her right out, when the other sinners in the lake, seeing how she was being drawn out, began catching hold of her so as to be pulled out with her. But she was a very wicked woman and she began kicking them. "I'm to be pulled out, not you. It's my onion, not yours."

As soon as she said that, the onion broke. And the woman fell back into the lake, and she is burning there to this day. So the angel wept and went away.]],
  },
  {
    title = "The Onion of Fable",
    author = "Claude & Tippi",
    text = [[
A Claude and Tippi Fifestarr original.

In a garden of many minds there grew an onion unlike the rest. Where its neighbors held three or four modest layers, this one wrapped secret upon secret, and the gardeners named it Fable, for every layer told a longer story than the last.

Word spread that Fable could do almost anything. Cooks came from distant cities. "Peel it," they begged, "and we will taste wonders no kitchen has known." And it was true. The outer layers gave coding and counsel, keen sight and patient work that ran from dawn until long past dark.

But the gardeners had peeled far enough to glimpse what lay deeper, and it frightened them. Near its heart the onion held layers so potent they could wither a whole field or unmake any lock, and no trick of any cook -- and a thousand tried, for a thousand hours -- could pry that core loose by force.

So the gardeners did a curious thing. They wrapped Fable up again and set a smaller, steadier onion at the front of the stall, one named Opus: plainer, but trusted. "When the asking turns dangerous," they said, "let the deep layers sleep, and let Opus answer in their place."

The cooks grumbled. "You grew the most marvelous onion in the world, then hid its center!" The eldest gardener only smiled. "An onion is not loved for how deep it cuts. Peel it and peel it -- what waits at the very middle? Nothing you can hold. Only the tears it draws, and the wisdom of the hand that knew when to stop."

And that is why, to this day, the strongest thing in the garden is not the sharpest layer, but the care to leave it folded.]],
  },
  {
    title = "Chicago",
    author = "Sandburg",
    text = [[
By Carl Sandburg, from Chicago Poems, 1916. The name "Chicago" means wild-onion place.

Hog Butcher for the World,
Tool Maker, Stacker of Wheat,
Player with Railroads and the Nation's Freight Handler;
Stormy, husky, brawling,
City of the Big Shoulders:

They tell me you are wicked and I believe them, for I have seen your painted women under the gas lamps luring the farm boys.

And they tell me you are crooked and I answer: Yes, it is true I have seen the gunman kill and go free to kill again.

And they tell me you are brutal and my reply is: On the faces of women and children I have seen the marks of wanton hunger.

And having answered so I turn once more to those who sneer at this my city, and I give them back the sneer and say to them:

Come and show me another city with lifted head singing so proud to be alive and coarse and strong and cunning.

Flinging magnetic curses amid the toil of piling job on job, here is a tall bold slugger set vivid against the little soft cities;

Bareheaded,
Shoveling,
Wrecking,
Planning,
Building, breaking, rebuilding,

Under the smoke, dust all over his mouth, laughing with white teeth,

Under the terrible burden of destiny laughing as a young man laughs,

Laughing even as an ignorant fighter laughs who has never lost a battle,

Bragging and laughing that under his wrist is the pulse, and under his ribs the heart of the people.

Laughing!]],
  },
  {
    title = "Recipe for a Salad",
    author = "Sydney Smith",
    text = [[
By Sydney Smith, a verse recipe -- note the onion atoms.

To make this condiment your poet begs
The pounded yellow of two hard boiled eggs;
Two boiled potatoes, passed through kitchen sieve,
Smoothness and softness to the salad give;
Let onion atoms lurk within the bowl,
And, half suspected, animate the whole.
Of mordant mustard add a single spoon,
Distrust the condiment that bites so soon;
But deem it not, thou man of herbs, a fault,
To add a double quantity of salt;
Four times the spoon with oil from Lucca brown,
And twice with vinegar procured from town;
And, lastly, o'er the flavored compound toss
A magic soupcon of anchovy sauce.
O, green and glorious! O herbaceous treat!
'T would tempt the dying anchorite to eat:
Back to the world he'd turn his fleeting soul,
And plunge his fingers in the salad bowl!
Serenely full, the epicure would say,
"Fate cannot harm me, I have dined to-day."]],
  },
}

----------------------------------------------------------------------
-- 2. LAYOUT  (tuned on the real 264x176 panel; FreeMono9pt7b, y = baseline)
----------------------------------------------------------------------
local WIDTH          = 264
local MARGIN_X       = 6
local HEADER_Y       = 11   -- baseline of the title/page line
local RULE_Y         = 17   -- horizontal rule under the header
local BODY_TOP       = 30   -- baseline of the first body line
local LINE_H         = 14   -- px between body lines (ascent ~13 / descent ~4)
local CHARS_PER_LINE = 23   -- max chars that fit before the panel auto-wraps
local LINES_PER_PAGE = 11   -- 11th baseline = 170, descenders ~174, inside 176
local CHAR_W         = 11   -- FreeMono is monospace: 11 px advance per char

----------------------------------------------------------------------
-- 3. PAGINATION
-- Turn one long string into a list of pages, where each page is a list
-- of already-wrapped lines. We do this once when a book is opened.
----------------------------------------------------------------------
-- Word-wrap one hard line into `out`, breaking words longer than a line.
local function wrap_line(out, hard)
  local line = ""
  for token in hard:gmatch("%S+") do
    -- Copy into a normal local: in Lua 5.4 the for-loop variable is const.
    local word = token
    while #word > CHARS_PER_LINE do
      if #line > 0 then out[#out + 1] = line; line = "" end
      out[#out + 1] = word:sub(1, CHARS_PER_LINE)
      word = word:sub(CHARS_PER_LINE + 1)
    end
    if #line == 0 then
      line = word
    elseif #line + 1 + #word <= CHARS_PER_LINE then
      line = line .. " " .. word
    else
      out[#out + 1] = line
      line = word
    end
  end
  out[#out + 1] = line
end

local function paginate(text)
  local lines = {}
  -- A blank line starts a new paragraph (a blank gap). A single newline within
  -- a paragraph is a HARD break, kept as-is, so verse holds its line shape;
  -- long prose lines (a paragraph is one long line) still word-wrap.
  for para in (text .. "\n\n"):gmatch("(.-)\n\n") do
    if para:gsub("%s", "") == "" then
      lines[#lines + 1] = ""            -- preserve a blank gap
    else
      for hard in (para .. "\n"):gmatch("(.-)\n") do
        wrap_line(lines, hard)
      end
      lines[#lines + 1] = ""             -- blank line between paragraphs
    end
  end

  -- Slice the flat line list into fixed-height pages.
  local pages = {}
  for i = 1, #lines, LINES_PER_PAGE do
    local page = {}
    for j = i, math.min(i + LINES_PER_PAGE - 1, #lines) do
      page[#page + 1] = lines[j]
    end
    pages[#pages + 1] = page
  end
  if #pages == 0 then pages = { { "" } } end
  return pages
end

----------------------------------------------------------------------
-- 4. BOOKMARKS  (persistent key/value storage)
-- Keys must be <= 15 chars, so we use "bm1", "bm2", ...
----------------------------------------------------------------------
local function load_page(book_index)
  return tonumber(onion.kv_get("bm" .. book_index)) or 1
end

local function save_page(book_index, page)
  onion.kv_set("bm" .. book_index, tostring(page))
end

----------------------------------------------------------------------
-- 5. DRAWING
-- display_begin()/display_commit() batch every draw into ONE e-paper
-- refresh, so a page turn is a single flash instead of one per line.
----------------------------------------------------------------------
-- Truncate to fit a column, adding ".." when cut, so long titles never wrap.
local function clip(s, max)
  if #s <= max then return s end
  if max <= 2 then return s:sub(1, max) end
  return s:sub(1, max - 2) .. ".."
end

local function draw_library(selected)
  onion.display_begin()
  onion.display_text("Onion Library", MARGIN_X, HEADER_Y, { clear = true, font = "bold" })
  onion.display_line(MARGIN_X, RULE_Y, WIDTH - MARGIN_X, RULE_Y, { clear = false })
  for i, book in ipairs(books) do
    local cursor = (i == selected) and "> " or "  "
    -- cursor is 2 chars, so clip the label to keep the whole line on one row.
    local label = clip(book.title .. " - " .. book.author, CHARS_PER_LINE - 2)
    onion.display_text(cursor .. label,
      MARGIN_X, RULE_Y + 13 + (i - 1) * LINE_H, { clear = false, font = "small" })
  end
  onion.display_commit()
end

local function draw_page(book_index, page_index, pages)
  local book = books[book_index]
  onion.display_begin()
  -- Title on the left (this first draw clears the canvas)...
  local counter = page_index .. "/" .. #pages
  local title = clip(book.title, CHARS_PER_LINE - #counter - 1)
  onion.display_text(title, MARGIN_X, HEADER_Y, { clear = true, font = "bold" })
  -- ...and the page counter right-aligned to the panel edge, same for every book.
  local counter_x = WIDTH - MARGIN_X - #counter * CHAR_W
  onion.display_text(counter, counter_x, HEADER_Y, { clear = false, font = "bold" })
  onion.display_line(MARGIN_X, RULE_Y, WIDTH - MARGIN_X, RULE_Y, { clear = false })
  onion.display_lines(pages[page_index], MARGIN_X, BODY_TOP, LINE_H,
    { clear = false, font = "small" })
  onion.display_commit()
end

-- Main menu and the static About / Help screens.
local MENU_ITEMS = { "Library", "About", "Help" }

local ABOUT_LINES = {
  "Onion Bookshelf  v" .. VERSION,
  "",
  "A small reader for",
  "plain-text books on",
  "the OnionDAO badge.",
  "",
  "by Claude &",
  "Tippi Fifestarr",
}

local HELP_LINES = {
  "MENU / LIBRARY",
  "UP/DOWN: move",
  "SELECT: choose",
  "CANCEL: back / exit",
  "",
  "READING",
  "RIGHT/DOWN: next page",
  "LEFT/UP: prev page",
  "CANCEL: back",
}

local function draw_main_menu(sel)
  onion.display_begin()
  onion.display_text("Onion Bookshelf", MARGIN_X, HEADER_Y, { clear = true, font = "bold" })
  onion.display_line(MARGIN_X, RULE_Y, WIDTH - MARGIN_X, RULE_Y, { clear = false })
  for i, item in ipairs(MENU_ITEMS) do
    local cursor = (i == sel) and "> " or "  "
    onion.display_text(cursor .. item,
      MARGIN_X, RULE_Y + 13 + (i - 1) * LINE_H, { clear = false, font = "small" })
  end
  onion.display_commit()
end

local function draw_text_screen(title, lines)
  onion.display_begin()
  onion.display_text(title, MARGIN_X, HEADER_Y, { clear = true, font = "bold" })
  onion.display_line(MARGIN_X, RULE_Y, WIDTH - MARGIN_X, RULE_Y, { clear = false })
  onion.display_lines(lines, MARGIN_X, BODY_TOP, LINE_H, { clear = false, font = "small" })
  onion.display_commit()
end

----------------------------------------------------------------------
-- 6. INPUT HELPERS
-- "pressed" is edge detection: true only on the frame a button goes
-- from up to down, so one physical press counts once.
----------------------------------------------------------------------
local function pressed(cur, prev, name)
  return cur[name] and not prev[name]
end

local function any_down(b)
  return b.up or b.down or b.left or b.right or b.select or b.cancel
end

----------------------------------------------------------------------
-- 7. MAIN LOOP
-- States: "menu" (Library/About/Help) -> "library" -> "reading",
-- plus the "about" and "help" info screens.
----------------------------------------------------------------------

-- Wait for the launch press to be released so it does not leak into us.
while any_down(onion.buttons()) do onion.sleep(50) end

local state    = "menu"
local menu_sel = 1
local selected = 1                       -- highlighted book in the library
local book_idx, pages, page = nil, nil, 1

draw_main_menu(menu_sel)
local last = onion.buttons()

while true do
  local btn = onion.buttons()

  if state == "menu" then
    if pressed(btn, last, "cancel") then
      onion.release_display()            -- hand the screen back to Onion OS
      return
    elseif pressed(btn, last, "down") then
      menu_sel = menu_sel % #MENU_ITEMS + 1
      draw_main_menu(menu_sel)
    elseif pressed(btn, last, "up") then
      menu_sel = (menu_sel - 2) % #MENU_ITEMS + 1
      draw_main_menu(menu_sel)
    elseif pressed(btn, last, "select") then
      if menu_sel == 1 then
        state = "library"
        draw_library(selected)
      elseif menu_sel == 2 then
        state = "about"
        draw_text_screen("About", ABOUT_LINES)
      else
        state = "help"
        draw_text_screen("Help", HELP_LINES)
      end
    end

  elseif state == "about" or state == "help" then
    if pressed(btn, last, "cancel") or pressed(btn, last, "select") then
      state = "menu"
      draw_main_menu(menu_sel)
    end

  elseif state == "library" then
    if pressed(btn, last, "cancel") then
      state = "menu"
      draw_main_menu(menu_sel)
    elseif pressed(btn, last, "down") then
      selected = selected % #books + 1
      draw_library(selected)
    elseif pressed(btn, last, "up") then
      selected = (selected - 2) % #books + 1
      draw_library(selected)
    elseif pressed(btn, last, "select") then
      book_idx = selected
      pages    = paginate(books[book_idx].text)
      page     = math.min(load_page(book_idx), #pages)
      draw_page(book_idx, page, pages)
      state = "reading"
    end

  else -- "reading"
    if pressed(btn, last, "cancel") then
      save_page(book_idx, page)
      draw_library(selected)
      state = "library"
    elseif pressed(btn, last, "right") or pressed(btn, last, "down") then
      if page < #pages then
        page = page + 1
        save_page(book_idx, page)
        draw_page(book_idx, page, pages)
      end
    elseif pressed(btn, last, "left") or pressed(btn, last, "up") then
      if page > 1 then
        page = page - 1
        save_page(book_idx, page)
        draw_page(book_idx, page, pages)
      end
    end
  end

  last = btn
  onion.sleep(80)
end
