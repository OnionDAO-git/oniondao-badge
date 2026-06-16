-- Headless tests for a generated badge book script.
--
--   lua run_tests.lua ../out/alice-p1.lua
--
-- Runs the real generated script (BOOK table + reader runtime) against the
-- onion_stub mock, drives it with scripted button presses, and asserts page
-- turning, bookmarking, resume, chapter jumps, hold-repeat, and that every
-- drawn line fits the 23x8 FreeMono layout.

local book_path = arg and arg[1]
if not book_path then
    io.stderr:write("usage: lua run_tests.lua <generated-book.lua>\n")
    os.exit(2)
end

local here = (arg[0] or "run_tests.lua"):match("(.*[/\\])") or "./"
local stub = dofile(here .. "onion_stub.lua")

local L = { left = 1, down = 2, up = 4, right = 8, select = 16, cancel = 32 }

local failures = 0
local function check(ok, what)
    if ok then
        print("  ok: " .. what)
    else
        failures = failures + 1
        print("  FAIL: " .. what)
    end
end

-- Every test queue starts with two all-released reads: one consumed by the
-- runtime's wait-for-release loop, one by its initial `last = onion.buttons()`.
local function press(mask) return { { mask = 0, times = 1 }, { mask = 0, times = 1 }, mask } end

local function run(presses, kv)
    local state = stub.install({ presses = presses, kv = kv or {} })
    BOOK = nil
    dofile(book_path)
    return state
end

local function tap(queue, mask, times)
    queue[#queue + 1] = { mask = mask, times = times or 1 }
    queue[#queue + 1] = { mask = 0, times = 1 }
end

local function page_renders(state)
    local out = {}
    for _, e in ipairs(state.events) do
        if e.kind == "lines" then out[#out + 1] = e.text end
    end
    return out
end

local function lines_fit(state)
    for _, e in ipairs(state.events) do
        if e.kind == "lines" or e.kind == "text" then
            local count = 0
            for line in (e.text .. "\n"):gmatch("(.-)\n") do
                count = count + 1
                if #line > 23 then return false, string.format("line %q is %d chars", line, #line) end
                if line:find("[^\032-\126]") then return false, string.format("non-ASCII in %q", line) end
            end
            if e.kind == "lines" and count > 8 then
                return false, string.format("page render has %d lines", count)
            end
        end
    end
    return true
end

print("book: " .. book_path)

-- Test 1: open, draw first page, cancel out -------------------------------
print("test 1: open and exit")
local queue = press()
tap(queue, L.cancel)
local state = run(queue)
check(state.released, "release_display called on CANCEL")
local renders = page_renders(state)
check(#renders == 1, "exactly one page drawn (got " .. #renders .. ")")
check(BOOK ~= nil and #BOOK.pages > 0, "BOOK table is populated")
check(renders[1] == BOOK.pages[1], "first draw is page 1")
check(select(2, lines_fit(state)) == nil, "all drawn lines fit 23x8 ASCII")
local page1, page2 = BOOK.pages[1], BOOK.pages[2]
local key = "bk_" .. BOOK.id .. "_" .. BOOK.part

-- Test 2: page turns + bookmark on exit -----------------------------------
print("test 2: page turns")
queue = press()
tap(queue, L.right)
tap(queue, L.right)
tap(queue, L.left)
tap(queue, L.cancel)
state = run(queue)
renders = page_renders(state)
check(#renders == 4, "four page draws for three turns (got " .. #renders .. ")")
check(renders[2] == page2, "RIGHT advances to page 2")
check(renders[3] == BOOK.pages[3], "second RIGHT advances to page 3")
check(renders[4] == page2, "LEFT goes back to page 2")
check(state.kv[key] == "2", "bookmark saved as page 2 (got " .. tostring(state.kv[key]) .. ")")
local commits = 0
for _, e in ipairs(state.events) do if e.kind == "commit" then commits = commits + 1 end end
check(commits == 7, "1 full + 2 commits per turn = 7 commits (got " .. commits .. ")")
check(lines_fit(state), "all drawn lines fit 23x8 ASCII")

-- Test 3: resume from bookmark ---------------------------------------------
print("test 3: resume from bookmark")
queue = press()
tap(queue, L.cancel)
state = run(queue, { [key] = "2" })
renders = page_renders(state)
check(renders[1] == page2, "opens at bookmarked page 2")

-- Test 4: chapter menu jump --------------------------------------------------
print("test 4: chapter menu")
if #BOOK.chapters > 0 then
    queue = press()
    tap(queue, L.select)  -- open menu (consumes one extra release for menu's own `last`)
    tap(queue, L.down)    -- cursor: Resume -> first chapter
    tap(queue, L.select)  -- jump
    tap(queue, L.cancel)
    state = run(queue)
    local target = BOOK.chapters[1].p
    check(state.kv[key] == tostring(target),
        "bookmark is chapter 1 page " .. target .. " (got " .. tostring(state.kv[key]) .. ")")
    renders = page_renders(state)
    local menu_seen = false
    for _, e in ipairs(state.events) do
        if e.kind == "text" and e.text:find("SEL jump", 1, true) then menu_seen = true end
    end
    check(menu_seen, "menu hint line drawn")
    check(renders[#renders] == BOOK.pages[target], "jumped to chapter page")
    check(lines_fit(state), "all drawn lines fit 23x8 ASCII")
else
    print("  ok: (book has no chapters; skipped)")
end

-- Test 5: hold-to-repeat ------------------------------------------------------
print("test 5: hold to repeat")
queue = press()
queue[#queue + 1] = { mask = L.right, times = 25 }  -- ~1.5 s hold at 60 ms polls
queue[#queue + 1] = { mask = 0, times = 1 }
tap(queue, L.cancel)
state = run(queue)
local expected = tostring(math.min(5, #BOOK.pages))  -- clamps at the last page
check(state.kv[key] == expected,
    "hold turned to page " .. expected .. ": 1 press + repeats at 600/950/1300 ms (got "
    .. tostring(state.kv[key]) .. ")")

-- Test 6: boundary clamps -----------------------------------------------------
print("test 6: boundaries")
queue = press()
tap(queue, L.left)            -- at page 1: no-op
tap(queue, L.cancel)
state = run(queue)
renders = page_renders(state)
check(#renders == 1, "LEFT on page 1 does not redraw")
local last_page = tostring(#BOOK.pages)
queue = press()
tap(queue, L.right)           -- at last page: no-op
tap(queue, L.cancel)
state = run(queue, { [key] = last_page })
renders = page_renders(state)
check(#renders == 1 and renders[1] == BOOK.pages[#BOOK.pages],
    "RIGHT on last page does not advance")
check(state.kv[key] == last_page, "bookmark unchanged at last page")

print(failures == 0 and "ALL TESTS PASSED" or (failures .. " FAILURE(S)"))
os.exit(failures == 0 and 0 or 1)
