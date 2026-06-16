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

----------------------------------------------------------------------
-- 1. THE LIBRARY  (generated from books/*.txt — see tools/build-books.py)
----------------------------------------------------------------------
local books = __BOOKS__

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
  "Onion Bookshelf",
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
