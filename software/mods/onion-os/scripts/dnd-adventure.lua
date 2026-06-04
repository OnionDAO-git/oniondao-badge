-- D&D Forever Adventure
-- AI-powered infinite adventure for Onion OS badge
-- Configure AI key via serial: ai-key sk-ant-...

local API_URL = "https://api.anthropic.com/v1/messages"
local MODEL   = "claude-haiku-4-5-20251001"
local W, H    = 264, 176

-- Separator line spanning full width at small font size (~37 chars)
local SEP = string.rep("-", 37)

-- ─── ASCII Art ────────────────────────────────────────────────────────────────

local RACE_ART = {
  Human   = { "    \\O/    ", "     |     ", "    / \\    ", "   HUMAN   " },
  Elf     = { "   \\^O^/   ", "    )||(   ", "   / || \\  ", "    ELF    " },
  Dwarf   = { "   (O_O)   ", "  [|###|]  ", "   d | b   ", "   DWARF   " },
  Orc     = { "   {>O<}   ", "   /|||\\   ", "   d | b   ", "    ORC    " },
  Halfling= { "    (o)    ", "    /|\\    ", "   _/ \\_   ", " HALFLING  " },
  Tiefling= { "  /\\ O /\\  ", "    )||(   ", "   / || \\  ", " TIEFLING  " },
}

local CLASS_ART = {
  Warrior = { "   [###]   ", "   |/ \\|   ", "   |\\=/|   ", "  WARRIOR  " },
  Mage    = { "   _/\\_    ", "  (*   *)  ", "   |~ ~|   ", "   MAGE    " },
  Rogue   = { "   .-\"-.   ", "  /|_o_|\\  ", "   \\ | /   ", "   ROGUE   " },
  Ranger  = { "   )||(    ", "  /|    |\\ ", "   |->-|   ", "  RANGER   " },
  Paladin = { "   (+++)   ", "  /|###|\\  ", "   |___/   ", "  PALADIN  " },
  Bard    = { "    o_O    ", "  /|~~|\\   ", "   | J |   ", "   BARD    " },
}

-- All scene art exactly 5 lines, centered in 264px wide display
local SCENE_ART = {
  dungeon = {
    "  +-------+   +-------+  ",
    "  | door  |   | door  |  ",
    "  |  ___  |   |  ___  |  ",
    "  +-/ \\---+   +---/ \\-+  ",
    "     \\ /           \\ /   ",
  },
  forest = {
    "   /\\    /\\    /\\    /\\  ",
    "  /  \\  /  \\  /  \\  /  \\ ",
    " / .  \\/  . \\/  . \\/  . \\",
    "|      |        |      |  ",
    "|______|________|_______|  ",
  },
  town = {
    " /\\  +----+  /\\   /\\     ",
    "/  \\ | [] | /  \\ /  \\    ",
    "|   ||    ||    ||   |    ",
    "|   ||    ||    ||   |    ",
    "+---++----++----++---+    ",
  },
  combat = {
    "  \\O/        /O/          ",
    "   |   ><><   |           ",
    "  /|\\ *!*!*! /|\\          ",
    "   |   ><><   |           ",
    "  / \\        \\ \\          ",
  },
  tavern = {
    " +-------------------------+ ",
    " |   ~ THE RUSTY FLAGON ~  | ",
    " +--+-------------------+--+ ",
    " |[]|  fire   fire  fire |[]| ",
    " +--+-------------------+--+ ",
  },
  mystery = {
    "   * .  . *   . *  .  *   ",
    " .   ???   .   ???   .    ",
    "   . \\|/  *  . \\|/ .      ",
    "  *  -+-   .   -+- *      ",
    "     /|\\  . *  /|\\ .  *   ",
  },
}

-- ─── Options ──────────────────────────────────────────────────────────────────

local RACES      = { "Human", "Elf", "Dwarf", "Orc", "Halfling", "Tiefling" }
local CLASSES    = { "Warrior", "Mage", "Rogue", "Ranger", "Paladin", "Bard" }
local ALIGNMENTS = {
  "Lawful Good",    "Neutral Good",  "Chaotic Good",
  "Lawful Neutral", "True Neutral",  "Chaotic Neutral",
  "Lawful Evil",    "Neutral Evil",  "Chaotic Evil",
}

-- ─── Helpers ──────────────────────────────────────────────────────────────────

local function wait_release()
  while true do
    local b = onion.buttons()
    if not (b.up or b.down or b.left or b.right or b.select or b.cancel) then return end
    onion.sleep(30)
  end
end

-- Wraps text into lines of max_w chars, returns array of strings
local function wrap(text, max_w)
  local lines = {}
  while #text > max_w do
    local cut = max_w
    while cut > 1 and text:sub(cut, cut) ~= " " do cut = cut - 1 end
    if cut == 1 then cut = max_w end
    lines[#lines + 1] = text:sub(1, cut):match("^(.-)%s*$")
    text = text:sub(cut + 1):match("^%s*(.*)")
  end
  if #text > 0 then lines[#lines + 1] = text end
  return lines
end

-- ─── Selection Screen ─────────────────────────────────────────────────────────

local function select_screen(title, options, art_table)
  local idx = 1
  local n   = #options

  local function draw()
    local name = options[idx]
    local art  = art_table and art_table[name] or {}
    onion.display_begin_batch()
    onion.clear_display()
    -- Title bar
    onion.display_text(title, 6, 16, { font = "bold", clear = false })
    onion.display_line(0, 22, W, 22, { clear = false })
    -- Art (4 lines, 15px spacing, starting y=40)
    for i, line in ipairs(art) do
      onion.display_text(line, 6, 40 + (i-1)*15, { font = "regular", clear = false })
    end
    -- Separator
    onion.display_line(0, 106, W, 106, { clear = false })
    -- Selected name (large)
    onion.display_text(name, 6, 130, { font = "large", clear = false })
    -- Bottom hint + index
    onion.display_line(0, 148, W, 148, { clear = false })
    onion.display_text("UP/DOWN   SELECT to confirm", 6, 164, { font = "small", clear = false })
    onion.display_text(idx.."/"..n, W - 28, 164, { font = "small", clear = false })
    onion.display_end_batch()
  end

  draw()
  wait_release()

  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then wait_release(); return options[idx] end
    if b.up   then idx = (idx - 2) % n + 1; draw(); wait_release() end
    if b.down then idx = idx % n + 1;        draw(); wait_release() end
    onion.sleep(50)
  end
end

-- ─── Alignment Screen (3x3 grid) ─────────────────────────────────────────────

local function alignment_screen()
  local idx = 5  -- default True Neutral

  local function draw()
    onion.display_begin_batch()
    onion.clear_display()
    onion.display_text("ALIGNMENT", 6, 16, { font = "bold", clear = false })
    onion.display_line(0, 22, W, 22, { clear = false })
    for i, al in ipairs(ALIGNMENTS) do
      local col = (i - 1) % 3
      local row = math.floor((i - 1) / 3)
      local x   = 4  + col * 87
      local y   = 46 + row * 36
      if i == idx then
        onion.display_rect(x - 2, y - 14, 86, 20, { fill = true,  clear = false })
        onion.display_text(al, x, y, { font = "small", color = "white", clear = false })
      else
        onion.display_rect(x - 2, y - 14, 86, 20, { fill = false, clear = false })
        onion.display_text(al, x, y, { font = "small", clear = false })
      end
    end
    onion.display_line(0, 152, W, 152, { clear = false })
    onion.display_text("L/R/U/D move   SELECT confirm", 4, 168, { font = "small", clear = false })
    onion.display_end_batch()
  end

  draw()
  wait_release()

  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then wait_release(); return ALIGNMENTS[idx] end
    if b.up    then idx = (idx - 4) % 9 + 1; draw(); wait_release() end
    if b.down  then idx = idx % 9 + 1;        draw(); wait_release() end
    if b.left  then
      if (idx - 1) % 3 > 0 then idx = idx - 1 end
      draw(); wait_release()
    end
    if b.right then
      if (idx - 1) % 3 < 2 then idx = idx + 1 end
      draw(); wait_release()
    end
    onion.sleep(50)
  end
end

-- ─── Character Creation ───────────────────────────────────────────────────────

local function character_creation()
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_text(" +------------------+", 22, 30,  { font = "regular", clear = false })
  onion.display_text(" | D&D FOREVER      |", 22, 46,  { font = "bold",    clear = false })
  onion.display_text(" | ADVENTURE        |", 22, 62,  { font = "bold",    clear = false })
  onion.display_text(" +------------------+", 22, 78,  { font = "regular", clear = false })
  onion.display_line(0, 110, W, 110, { clear = false })
  onion.display_text("An AI-powered infinite quest", 10, 130, { font = "small", clear = false })
  onion.display_text("Press SELECT to begin", 10, 158, { font = "small", clear = false })
  onion.display_end_batch()

  wait_release()
  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then wait_release(); break end
    onion.sleep(50)
  end

  local race = select_screen("CHOOSE YOUR RACE", RACES, RACE_ART)
  if not race then return nil end

  local class = select_screen("CHOOSE YOUR CLASS", CLASSES, CLASS_ART)
  if not class then return nil end

  local alignment = alignment_screen()
  if not alignment then return nil end

  return { race = race, class = class, alignment = alignment }
end

-- ─── AI Request / Response ────────────────────────────────────────────────────

local function build_request(char, history, action)
  local system_prompt = string.format(
    'You are a terse D&D DM. Player: %s %s, %s alignment. ' ..
    'Respond ONLY with valid JSON, no extra text: ' ..
    '{"scene":"vivid 1-2 sentence description (max 100 chars)","choices":["a","b","c"],"art":"dungeon|forest|town|combat|tavern|mystery"}. ' ..
    'Each choice max 28 chars.',
    char.alignment, char.race.." "..char.class, char.alignment
  )
  local user_msg = action
  if history ~= "" then
    user_msg = "Story: "..history.." | Action: "..action
  end
  local function esc(s)
    return s:gsub('\\','\\\\'):gsub('"','\\"'):gsub('\n','\\n')
  end
  return string.format(
    '{"model":"%s","max_tokens":250,"system":"%s","messages":[{"role":"user","content":"%s"}]}',
    MODEL, esc(system_prompt), esc(user_msg)
  )
end

local function parse_response(body)
  local text = body:match('"text"%s*:%s*"(.-[^\\])"') or body:match('"text"%s*:%s*""')
  if not text then return nil end
  text = text:gsub('\\"','"'):gsub('\\n',' '):gsub('\\\\','\\')

  local scene = text:match('"scene"%s*:%s*"(.-[^\\])"') or "The adventure continues..."
  local art   = text:match('"art"%s*:%s*"(%a+)"') or "dungeon"

  local choices = {}
  for c in text:gmatch('"([^"\\][^"]*)"') do
    if #choices < 3 and c ~= scene and c ~= art and #c > 3 and #c < 60 then
      choices[#choices + 1] = c
    end
  end
  if #choices == 0 then choices = { "Press on", "Look around", "Make camp" } end

  return { scene = scene, choices = choices, art = art }
end

-- ─── Scene Drawing ────────────────────────────────────────────────────────────
-- Layout (176px height):
--   y=13-65  : art (5 lines, 13px step)
--   y=72     : separator line
--   y=86,101 : scene text (2 lines, 15px step)
--   y=109    : separator line
--   y=124,139,154 : choices (15px step)
--   y=170    : nav hint

local function draw_scene(art_key, scene_text, choices, cursor)
  local art = SCENE_ART[art_key] or SCENE_ART.dungeon

  -- Word-wrap scene into up to 2 lines of 36 chars
  local wrapped = wrap(scene_text, 36)
  local s1 = wrapped[1] or ""
  local s2 = wrapped[2] or ""

  onion.display_begin_batch()
  onion.clear_display()

  -- Outer border
  onion.display_rect(1, 1, W - 2, H - 2, { fill = false, clear = false })

  -- Art section (5 lines, 13px spacing, starting at y=14)
  for i, line in ipairs(art) do
    onion.display_text(line, 4, 14 + (i-1)*13, { font = "regular", clear = false })
  end

  -- Divider
  onion.display_line(2, 72, W - 2, 72, { clear = false })

  -- Scene text
  onion.display_text(s1, 4, 86,  { font = "small", clear = false })
  onion.display_text(s2, 4, 101, { font = "small", clear = false })

  -- Divider
  onion.display_line(2, 109, W - 2, 109, { clear = false })

  -- Choices
  for i, ch in ipairs(choices) do
    local y      = 124 + (i - 1) * 15
    local prefix = (i == cursor) and "> " or "  "
    onion.display_text(prefix..i..". "..ch:sub(1, 28), 4, y, { font = "small", clear = false })
  end

  -- Nav hint
  onion.display_text("UP/DOWN  SELECT choose  CAN exit", 4, 170, { font = "small", clear = false })

  onion.display_end_batch()
end

-- ─── Choice Wait ──────────────────────────────────────────────────────────────

local function wait_for_choice(scene_art, scene_text, choices)
  local cursor = 1
  draw_scene(scene_art, scene_text, choices, cursor)
  wait_release()

  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then wait_release(); return cursor end
    if b.up and cursor > 1 then
      cursor = cursor - 1
      draw_scene(scene_art, scene_text, choices, cursor)
      wait_release()
    end
    if b.down and cursor < #choices then
      cursor = cursor + 1
      draw_scene(scene_art, scene_text, choices, cursor)
      wait_release()
    end
    onion.sleep(50)
  end
end

-- ─── Loading Screen ───────────────────────────────────────────────────────────

local function show_loading(msg)
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_rect(1, 1, W - 2, H - 2, { fill = false, clear = false })
  onion.display_line(2, 40, W - 2, 40,  { clear = false })
  onion.display_line(2, 136, W - 2, 136, { clear = false })
  onion.display_text("~ DUNGEON MASTER ~", 42, 28, { font = "bold",    clear = false })
  onion.display_text(msg or "Consulting the fates...", 10, 82,  { font = "small",   clear = false })
  onion.display_text("Please wait...", 78, 116, { font = "small",   clear = false })
  onion.display_end_batch()
end

-- ─── Main ─────────────────────────────────────────────────────────────────────

local char = character_creation()
if not char then
  onion.release_display()
  return
end

show_loading("Conjuring your fate...")

local history = ""
local action  = string.format(
  "Start an adventure for a %s %s with %s alignment. Set the opening scene.",
  char.alignment, char.race.." "..char.class, char.alignment
)

local req = build_request(char, "", action)
local res = onion.http_post(API_URL, req)

if not (res and res.status == 200) then
  local code = res and res.status or -1
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_text("API error (status "..code..")", 4, 60, { font = "small", clear = false })
  onion.display_text("Set key via serial:", 4, 80, { font = "small", clear = false })
  onion.display_text("ai-key sk-ant-...", 4, 96, { font = "small", clear = false })
  onion.display_end_batch()
  onion.sleep(4000)
  onion.release_display()
  return
end

local scene = parse_response(res.body)
if not scene then
  scene = {
    scene   = "Your adventure begins in a dark dungeon...",
    choices = { "Look around", "Draw weapon", "Listen carefully" },
    art     = "dungeon",
  }
end

local function update_history(h, action_taken, new_scene)
  local combined = h.." "..action_taken..". "..new_scene
  if #combined > 160 then
    combined = combined:sub(#combined - 159):match("^%S*(.*)")
  end
  return combined
end

-- Adventure loop
while true do
  local choice_idx = wait_for_choice(scene.art, scene.scene, scene.choices)
  if not choice_idx then break end

  local chosen = scene.choices[choice_idx]
  history = update_history(history, chosen, scene.scene)

  show_loading("The DM ponders your fate...")

  local next_res = onion.http_post(API_URL, build_request(char, history, chosen))

  if next_res and next_res.status == 200 then
    local parsed = parse_response(next_res.body)
    if parsed then
      scene = parsed
    else
      scene.scene = "The mists obscure your path... (parse err)"
    end
  else
    local code = next_res and next_res.status or -1
    scene.scene   = "The oracle falls silent. (err "..code..")"
    scene.choices = { "Try again", "Rest here", "Turn back" }
  end
end

onion.release_display()
