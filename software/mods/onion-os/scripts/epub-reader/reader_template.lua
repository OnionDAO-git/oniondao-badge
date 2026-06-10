-- EPUB reader runtime for Onion OS.
--
-- This file is not run directly: epub2badge.py prepends a generated
-- `BOOK = { ... }` table (title, layout, chapters, pages) and installs the
-- combined script on the badge. See README.md in this folder.
--
-- Controls:
--   RIGHT/DOWN: next page (hold to keep turning)
--   LEFT/UP: previous page (hold to keep turning)
--   SELECT: chapter menu (UP/DOWN move, SELECT jump, CANCEL back)
--   CANCEL: save bookmark and return to Onion OS
--
-- Page turns are drawn as two deferred-frame commits — the text band and the
-- footer band — so each dirty region stays under the firmware's 75% partial
-- refresh threshold. That keeps a turn at ~2 fast partial refreshes instead
-- of one ~1.7 s full-flash refresh. The firmware still forces a ghost-clearing
-- full refresh every 30 partials.

local B = BOOK

local size = onion.display_size()
local W, H = size.width, size.height

local TEXT_X = B.layout.x
local TEXT_Y = B.layout.y
local LINE_H = B.layout.lh
local RULE_Y = B.layout.rule
local FOOT_Y = B.layout.foot

local CHAR_W = 11 -- FreeMono9pt7b advance
local HOLD_MS = 600
local REPEAT_MS = 350
local SAVE_AFTER_MS = 2000

local pages = B.pages
local total = #pages
local bookmark_key = "bk_" .. B.id .. "_" .. B.part

if total == 0 then
    onion.log("Book has no pages")
    onion.release_display()
    return
end

local page = tonumber(onion.kv_get(bookmark_key) or "") or 1
if page < 1 then page = 1 end
if page > total then page = total end

local dirty = false
local dirty_at = 0

local function save_bookmark()
    if not dirty then return end
    local ok, err = onion.kv_set(bookmark_key, tostring(page))
    if not ok and err then onion.log("Bookmark: " .. err) end
    dirty = false
end

local function footer_right()
    local pct = math.floor(page * 100 / total)
    return page .. "/" .. total .. " " .. pct .. "%"
end

-- Footer band only: blank below the rule, then label + progress. Caller wraps
-- this in display_begin/commit.
local function draw_footer()
    onion.display_rect(0, RULE_Y + 1, W, H - RULE_Y - 1,
        { fill = true, color = "white", clear = false })
    local right = footer_right()
    local right_x = W - 3 - #right * CHAR_W
    local label_max = math.floor((right_x - 8) / CHAR_W)
    local label = string.sub(B.label, 1, label_max)
    onion.display_text(label, TEXT_X, FOOT_Y, { clear = false })
    onion.display_text(right, right_x, FOOT_Y, { clear = false })
end

-- full=true redraws the whole screen in one commit (used for the first page
-- and when returning from the menu, where a full refresh happens anyway).
-- full=false splits text and footer into two commits to stay partial.
local function draw_page(full)
    onion.display_begin()
    if full then
        onion.display_rect(0, 0, W, H, { fill = true, color = "white", clear = false })
    else
        onion.display_rect(0, 0, W, RULE_Y - 1, { fill = true, color = "white", clear = false })
    end
    onion.display_lines(pages[page], TEXT_X, TEXT_Y, LINE_H, { clear = false })
    if full then
        onion.display_line(0, RULE_Y, W - 1, RULE_Y, false)
        draw_footer()
        onion.display_commit()
    else
        onion.display_commit()
        onion.display_begin()
        draw_footer()
        onion.display_commit()
    end
end

local function turn(delta)
    local target = page + delta
    if target < 1 or target > total then return end
    page = target
    dirty = true
    dirty_at = onion.millis()
    draw_page(false)
end

-- Chapter menu -------------------------------------------------------------

local MENU_ROWS = 6

local function menu_entries()
    local entries = { { t = "Resume", p = page } }
    for i = 1, #B.chapters do
        local c = B.chapters[i]
        entries[#entries + 1] = { t = c.t, p = c.p }
    end
    return entries
end

local function draw_menu(entries, cursor, top)
    onion.display_begin()
    onion.display_rect(0, 0, W, H, { fill = true, color = "white", clear = false })
    onion.display_text(string.sub(B.title, 1, 21), 3, 16, { clear = false, font = "bold" })
    onion.display_text("Page " .. footer_right(), 3, 34, { clear = false })
    onion.display_line(0, 40, W - 1, 40, false)
    local lines = {}
    for row = 1, MENU_ROWS do
        local i = top + row - 1
        if i > #entries then break end
        local prefix = (i == cursor) and "> " or "  "
        lines[#lines + 1] = prefix .. string.sub(entries[i].t, 1, 21)
    end
    onion.display_lines(lines, 3, 56, 18, { clear = false })
    onion.display_text("SEL jump  CANCEL back", 3, FOOT_Y, { clear = false })
    onion.display_commit()
end

-- Returns the page to jump to, or nil to just resume.
local function run_menu()
    save_bookmark()
    local entries = menu_entries()
    local cursor, top = 1, 1
    draw_menu(entries, cursor, top)
    local last = onion.buttons()
    while true do
        local buttons = onion.buttons()
        local moved = false
        if buttons.cancel and not last.cancel then
            return nil
        elseif buttons.select and not last.select then
            if cursor == 1 then return nil end
            return entries[cursor].p
        elseif buttons.down and not last.down then
            cursor = cursor + 1
            if cursor > #entries then cursor = 1 end
            moved = true
        elseif buttons.up and not last.up then
            cursor = cursor - 1
            if cursor < 1 then cursor = #entries end
            moved = true
        end
        if moved then
            if cursor < top then top = cursor end
            if cursor > top + MENU_ROWS - 1 then top = cursor - MENU_ROWS + 1 end
            draw_menu(entries, cursor, top)
        end
        last = buttons
        onion.sleep(60)
    end
end

-- Main loop ------------------------------------------------------------------

while true do
    local buttons = onion.buttons()
    if not (buttons.left or buttons.down or buttons.up or buttons.right or
            buttons.select or buttons.cancel) then
        break
    end
    onion.sleep(50)
end

draw_page(true)

local last = onion.buttons()
local held_dir = 0
local held_since = 0
local last_repeat = 0

while true do
    local buttons = onion.buttons()
    local now = onion.millis()
    local fwd = buttons.right or buttons.down
    local back = buttons.left or buttons.up
    local last_fwd = last.right or last.down
    local last_back = last.left or last.up

    if buttons.cancel and not last.cancel then
        save_bookmark()
        onion.release_display()
        return
    elseif buttons.select and not last.select then
        local target = run_menu()
        if target then
            page = target
            dirty = true
            save_bookmark()
        end
        draw_page(true)
        held_dir = 0
    elseif fwd and not last_fwd then
        turn(1)
        held_dir = 1
        held_since = now
        last_repeat = now
    elseif back and not last_back then
        turn(-1)
        held_dir = -1
        held_since = now
        last_repeat = now
    elseif held_dir == 1 and fwd then
        if now - held_since >= HOLD_MS and now - last_repeat >= REPEAT_MS then
            turn(1)
            last_repeat = now
        end
    elseif held_dir == -1 and back then
        if now - held_since >= HOLD_MS and now - last_repeat >= REPEAT_MS then
            turn(-1)
            last_repeat = now
        end
    else
        held_dir = 0
    end

    if dirty and onion.millis() - dirty_at >= SAVE_AFTER_MS then
        save_bookmark()
    end

    last = buttons
    onion.sleep(60)
end
