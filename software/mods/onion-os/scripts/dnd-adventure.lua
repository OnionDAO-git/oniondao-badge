-- D&D Forever Adventure
-- AI-powered infinite adventure for Onion OS badge
-- Configure AI key via serial: ai-key sk-ant-...

local API_URL = "https://api.anthropic.com/v1/messages"
local MODEL   = "claude-haiku-4-5-20251001"
local W, H    = 264, 176

-- ─── ASCII Art ────────────────────────────────────────────────────────────────

local RACE_ART = {
  Human = {
    "   \\O/   ",
    "    |    ",
    "   / \\   ",
    " HUMAN   ",
  },
  Elf = {
    "  \\^O^/  ",
    "   )||(  ",
    "  / || \\ ",
    "   ELF   ",
  },
  Dwarf = {
    "  (O_O)  ",
    " [|###|] ",
    "  d | b  ",
    "  DWARF  ",
  },
  Orc = {
    "  {>O<}  ",
    "  /|||\\  ",
    "  d | b  ",
    "   ORC   ",
  },
  Halfling = {
    "   (o)   ",
    "   /|\\   ",
    "  _/ \\_  ",
    "HALFLING ",
  },
  Tiefling = {
    "  /\\O/\\  ",
    "   )||(  ",
    "  / || \\ ",
    "TIEFLING ",
  },
}

local CLASS_ART = {
  Warrior = {
    "  [###]  ",
    "  |/ \\|  ",
    "  |\\=/|  ",
    " WARRIOR ",
  },
  Mage = {
    "  _/\\_   ",
    " (*   *) ",
    "  |~ ~|  ",
    "  MAGE   ",
  },
  Rogue = {
    "  .-\"-.  ",
    " /|_o_|\\ ",
    "  \\ | /  ",
    "  ROGUE  ",
  },
  Ranger = {
    "  )||(   ",
    " /|  |\\ ",
    "  |->-|  ",
    " RANGER  ",
  },
  Paladin = {
    "  (+++)  ",
    " /|###|\\ ",
    "  |___/  ",
    " PALADIN ",
  },
  Bard = {
    "  o_O    ",
    " /|~~|\\  ",
    "  | J |  ",
    "  BARD   ",
  },
}

local SCENE_ART = {
  dungeon = {
    " ___________ ",
    "|  ___ ___  |",
    "| |   |   | |",
    "| |___|___| |",
    "|_____V_____|",
    "    |   |    ",
  },
  forest = {
    "  /\\  /\\  /\\ ",
    " /  \\/  \\/  \\",
    "/    \\  /    \\",
    "|    |  |    |",
    "|____|  |____|",
  },
  town = {
    "  /\\  [__] /\\ ",
    " /  \\ |  |/  \\",
    "|    ||  ||   |",
    "|    ||  ||   |",
    "|____|'--'|___|",
  },
  combat = {
    " \\O/  *** /O\\ ",
    "  |  *   *  | ",
    " /|\\ * ! * /|\\ ",
    "  |  *   *  | ",
    " / \\  ***  / \\ ",
  },
  tavern = {
    " ___________  ",
    "|  THE INN  | ",
    "|_[_]___[_]_| ",
    "| |_______|  | ",
    "|_|_______|__| ",
  },
  mystery = {
    "  ?  ???  ?   ",
    " ??? \\|/ ???  ",
    "  ?  -+-  ?   ",
    " ??? /|\\ ???  ",
    "  ?  ???  ?   ",
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
    if not (b.up or b.down or b.select or b.cancel) then return end
    onion.sleep(40)
  end
end

local function draw_art(lines, y)
  for i, line in ipairs(lines) do
    onion.display_text(line, 10, y + (i - 1) * 12, { font = "regular", clear = false })
  end
end

-- ─── Selection Screen ─────────────────────────────────────────────────────────
-- Returns chosen string or nil on cancel.

local function select_screen(title, options, art_table)
  local idx = 1
  local n   = #options

  local function draw()
    local name = options[idx]
    local art  = art_table and art_table[name]
    onion.clear_display()
    onion.display_text(title, 10, 14, { font = "bold", clear = false })
    onion.display_line(0, 20, W, 20, { clear = false })
    if art then draw_art(art, 30) end
    onion.display_text(name, 10, 90, { font = "large", clear = false })
    onion.display_text("^ UP  v DOWN  [SEL] confirm", 6, 160, { font = "small", clear = false })
    onion.display_text(idx .. "/" .. n, W - 30, 160, { font = "small", clear = false })
  end

  draw()
  wait_release()

  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then
      wait_release()
      return options[idx]
    end
    if b.up then
      idx = (idx - 2) % n + 1
      draw()
      wait_release()
    end
    if b.down then
      idx = idx % n + 1
      draw()
      wait_release()
    end
    onion.sleep(60)
  end
end

-- ─── Alignment Screen (3x3 grid) ─────────────────────────────────────────────

local function alignment_screen()
  local idx = 5  -- default True Neutral

  local function draw()
    onion.clear_display()
    onion.display_text("CHOOSE ALIGNMENT", 10, 14, { font = "bold", clear = false })
    onion.display_line(0, 20, W, 20, { clear = false })
    for i, al in ipairs(ALIGNMENTS) do
      local col  = (i - 1) % 3
      local row  = math.floor((i - 1) / 3)
      local x    = 8 + col * 86
      local y    = 35 + row * 36
      if i == idx then
        onion.display_rect(x - 2, y - 12, 84, 28, { fill = true, clear = false })
        onion.display_text(al, x, y, { font = "small", color = "white", clear = false })
      else
        onion.display_text(al, x, y, { font = "small", clear = false })
      end
    end
    onion.display_text("[SEL] confirm  [CAN] back", 6, 165, { font = "small", clear = false })
  end

  draw()
  wait_release()

  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then
      wait_release()
      return ALIGNMENTS[idx]
    end
    if b.up    then idx = (idx - 4) % 9 + 1; draw(); wait_release() end
    if b.down  then idx = idx % 9 + 1;        draw(); wait_release() end
    if b.left  then
      local col = (idx - 1) % 3
      if col > 0 then idx = idx - 1 end
      draw(); wait_release()
    end
    if b.right then
      local col = (idx - 1) % 3
      if col < 2 then idx = idx + 1 end
      draw(); wait_release()
    end
    onion.sleep(60)
  end
end

-- ─── Character Creation ───────────────────────────────────────────────────────

local function character_creation()
  -- Title screen
  onion.clear_display()
  onion.display_lines({
    " ___  _  _  ___",
    "|   \\| \\| ||   \\",
    "| |) |  ` || |) |",
    "|___/|_|\\_||___/",
    "",
    "FOREVER ADVENTURE",
    "",
    "Press SELECT to begin",
  }, 20, 18, 16, { font = "regular", clear = true })

  wait_release()
  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then wait_release(); break end
    onion.sleep(60)
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
    'You are a D&D dungeon master. The player is a %s %s with %s alignment. ' ..
    'Given their action, return ONLY valid JSON with no extra text: ' ..
    '{"scene":"2-3 sentence vivid scene description","choices":["action1","action2","action3"],"art":"dungeon|forest|town|combat|tavern|mystery"}. ' ..
    'Keep scene under 120 chars. Keep choices under 30 chars each.',
    char.alignment, char.race .. " " .. char.class, char.alignment
  )

  local user_msg = action
  if history ~= "" then
    user_msg = "Story so far: " .. history .. " | Player action: " .. action
  end

  -- Escape double quotes and backslashes in strings for JSON
  local function json_str(s)
    s = s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n')
    return '"' .. s .. '"'
  end

  return string.format(
    '{"model":%s,"max_tokens":300,"system":%s,"messages":[{"role":"user","content":%s}]}',
    json_str(MODEL),
    json_str(system_prompt),
    json_str(user_msg)
  )
end

local function parse_response(body)
  -- Extract the text content from Anthropic response
  local text = body:match('"text"%s*:%s*"(.-[^\\])"')
  if not text then
    text = body:match('"text"%s*:%s*"()"')
  end
  if not text then return nil end

  -- Unescape
  text = text:gsub('\\"', '"'):gsub('\\n', ' '):gsub('\\\\', '\\')

  -- Parse scene, choices, art from the JSON the model returned
  local scene = text:match('"scene"%s*:%s*"(.-[^\\])"') or
                text:match('"scene"%s*:%s*"()"') or
                "The adventure continues..."

  local art = text:match('"art"%s*:%s*"(%a+)"') or "dungeon"

  local choices = {}
  for c in text:gmatch('"([^"\\][^"]*)"') do
    if #choices < 3 and c ~= scene and c ~= art and #c > 3 then
      choices[#choices + 1] = c
    end
  end
  if #choices == 0 then
    choices = { "Press on", "Look around", "Make camp" }
  end

  return { scene = scene, choices = choices, art = art }
end

-- ─── Scene Drawing ────────────────────────────────────────────────────────────

local function draw_scene(art_key, scene, choices, cursor)
  local art = SCENE_ART[art_key] or SCENE_ART.dungeon
  onion.clear_display()

  -- Art panel (top)
  draw_art(art, 4)
  onion.display_line(0, 72, W, 72, { clear = false })

  -- Scene text (up to two lines of ~38 chars each)
  onion.display_text(scene:sub(1, 38), 4, 82, { font = "small", clear = false })
  if #scene > 38 then
    onion.display_text(scene:sub(39, 76), 4, 93, { font = "small", clear = false })
  end

  onion.display_line(0, 106, W, 106, { clear = false })

  -- Choices
  for i, ch in ipairs(choices) do
    local y = 116 + (i - 1) * 18
    local prefix = (i == cursor) and "> " or "  "
    onion.display_text(prefix .. i .. ". " .. ch:sub(1, 30), 4, y, { font = "small", clear = false })
  end
end

-- ─── Choice Wait ──────────────────────────────────────────────────────────────

local function wait_for_choice(n, scene_art, scene_text, choices)
  local cursor = 1
  draw_scene(scene_art, scene_text, choices, cursor)
  wait_release()

  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then
      wait_release()
      return cursor
    end
    if b.up and cursor > 1 then
      cursor = cursor - 1
      draw_scene(scene_art, scene_text, choices, cursor)
      wait_release()
    end
    if b.down and cursor < n then
      cursor = cursor + 1
      draw_scene(scene_art, scene_text, choices, cursor)
      wait_release()
    end
    onion.sleep(60)
  end
end

-- ─── Loading Screen ───────────────────────────────────────────────────────────

local function show_loading(msg)
  onion.clear_display()
  onion.display_lines({
    "  ~ ~ ~ ~ ~ ~ ~  ",
    "",
    msg or "Consulting the fates...",
    "",
    "  ~ ~ ~ ~ ~ ~ ~  ",
  }, 10, 60, 18, { font = "regular", clear = true })
end

-- ─── Main ─────────────────────────────────────────────────────────────────────

local char = character_creation()
if not char then
  onion.release_display()
  return
end

show_loading("Conjuring your fate...")
onion.sleep(200)

local history  = ""
local action   = string.format("Begin my journey as a %s %s (%s). Set the opening scene.", char.alignment, char.race .. " " .. char.class, char.alignment)
local scene    = { scene = "Your adventure begins...", choices = { "Look around", "Draw weapon", "Listen carefully" }, art = "dungeon" }

local function update_history(prev_history, chosen_action, new_scene)
  local combined = prev_history .. " " .. chosen_action .. ". " .. new_scene
  if #combined > 150 then
    combined = combined:sub(#combined - 149)
    combined = combined:match("^%S*(.*)")  -- trim partial word
  end
  return combined
end

-- First scene
local req = build_request(char, "", action)
local res = onion.http_post(API_URL, req)

if res and res.status == 200 then
  local parsed = parse_response(res.body)
  if parsed then scene = parsed end
else
  onion.display_text("No AI key set. Use serial: ai-key <key>", 4, 80, { font = "small", clear = true })
  onion.sleep(3000)
  onion.release_display()
  return
end

-- Adventure loop
while true do
  local choice_idx = wait_for_choice(#scene.choices, scene.art, scene.scene, scene.choices)

  if not choice_idx then break end

  local chosen = scene.choices[choice_idx]
  history = update_history(history, chosen, scene.scene)

  show_loading("The Dungeon Master ponders...")

  local next_req = build_request(char, history, chosen)
  local next_res = onion.http_post(API_URL, next_req)

  if next_res and next_res.status == 200 then
    local parsed = parse_response(next_res.body)
    if parsed then
      scene = parsed
    else
      scene.scene = "The mists swirl... (parse error)"
    end
  else
    local code = next_res and next_res.status or -1
    scene.scene = "The oracle is silent. (err " .. code .. ")"
    scene.choices = { "Try again", "Rest here", "Turn back" }
  end
end

onion.release_display()
