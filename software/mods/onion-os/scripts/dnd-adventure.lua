-- D&D Forever Adventure v2
-- Save/load, XP, leveling, dice rolling, clean text UI
-- Set AI key via serial: ai-key sk-ant-...

local API_URL = "https://api.anthropic.com/v1/messages"
local MODEL   = "claude-haiku-4-5-20251001"
local W, H    = 264, 176

-- XP needed to reach the next level (index = current level)
local XP_NEXT = { 100, 300, 600, 1000, 1500, 2100, 2800, 3600, 4500, 99999 }

local RACES   = { "Human", "Elf", "Dwarf", "Orc", "Halfling", "Tiefling" }
local CLASSES = { "Warrior", "Mage", "Rogue", "Ranger", "Paladin", "Bard" }
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

local function wrap(text, w)
  local lines = {}
  text = tostring(text or "")
  while #text > w do
    local cut = w
    while cut > 1 and text:sub(cut, cut) ~= " " do cut = cut - 1 end
    if cut == 1 then cut = w end
    lines[#lines + 1] = text:sub(1, cut):match("^(.-)%s*$")
    text = text:sub(cut + 1):match("^%s*(.*)")
  end
  if #text > 0 then lines[#lines + 1] = text end
  return lines
end

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

-- ─── Persistent Save / Load ───────────────────────────────────────────────────

local function save(char)
  onion.nvs_set("race",   char.race)
  onion.nvs_set("class",  char.class)
  onion.nvs_set("align",  char.alignment)
  onion.nvs_set("level",  tostring(char.level))
  onion.nvs_set("xp",     tostring(char.xp))
  onion.nvs_set("hp",     tostring(char.hp))
  onion.nvs_set("maxhp",  tostring(char.maxhp))
  onion.nvs_set("hist",   char.history)
end

local function load_save()
  local race = onion.nvs_get("race", "")
  if race == "" then return nil end
  return {
    race      = race,
    class     = onion.nvs_get("class",  "Warrior"),
    alignment = onion.nvs_get("align",  "True Neutral"),
    level     = tonumber(onion.nvs_get("level", "1"))  or 1,
    xp        = tonumber(onion.nvs_get("xp",    "0"))  or 0,
    hp        = tonumber(onion.nvs_get("hp",    "10")) or 10,
    maxhp     = tonumber(onion.nvs_get("maxhp", "10")) or 10,
    history   = onion.nvs_get("hist", ""),
  }
end

-- ─── Dice Rolling ─────────────────────────────────────────────────────────────

local function roll(sides)
  return math.random(1, sides)
end

local function animate_roll(sides)
  local result = 0
  for i = 1, 18 do
    result = roll(sides)
    onion.display_begin_batch()
    onion.clear_display()
    onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
    onion.display_line(2, 36, W-2, 36, { clear=false })
    onion.display_line(2, 140, W-2, 140, { clear=false })
    onion.display_text("  ~ FATE DECIDES ~", 30, 24, { font="bold",  clear=false })
    onion.display_text("d" .. sides, 20, 110, { font="large", clear=false })
    onion.display_text(tostring(result), 100, 110, { font="large", clear=false })
    local bar = string.rep("=", clamp(i * 2, 1, 36))
    onion.display_text(bar, 14, 158, { font="small", clear=false })
    onion.display_end_batch()
    onion.sleep(30 + i * 9)
  end
  return result
end

-- ─── XP / Levelling ───────────────────────────────────────────────────────────

local function gain_xp(char, amount)
  char.xp = char.xp + amount
  local needed = XP_NEXT[clamp(char.level, 1, #XP_NEXT)] or 99999
  if char.xp >= needed and char.level < 10 then
    char.level  = char.level + 1
    local bonus = math.random(3, 6)
    char.maxhp  = char.maxhp + bonus
    char.hp     = char.maxhp
    return true, bonus
  end
  return false
end

local function show_level_up(char, bonus)
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
  onion.display_line(2, 28, W-2, 28, { clear=false })
  onion.display_line(2, 146, W-2, 146, { clear=false })
  onion.display_text("  *** LEVEL UP! ***", 20, 18, { font="bold",    clear=false })
  onion.display_text("You are now Level " .. char.level, 10, 56,  { font="regular", clear=false })
  onion.display_text("+" .. bonus .. " HP  (max " .. char.maxhp .. ")",  10, 78,  { font="regular", clear=false })
  onion.display_text("Fully restored!", 10, 100, { font="regular", clear=false })
  local next = XP_NEXT[clamp(char.level, 1, #XP_NEXT)] or 99999
  onion.display_text("Next level at " .. next .. " XP", 10, 122, { font="small",   clear=false })
  onion.display_text("Press SELECT to continue", 16, 160, { font="small",   clear=false })
  onion.display_end_batch()
  wait_release()
  while true do
    local b = onion.buttons()
    if b.select or b.cancel then wait_release(); break end
    onion.sleep(50)
  end
end

-- ─── UI ───────────────────────────────────────────────────────────────────────
-- Layout (176px height):
--   y=14          header (small): race class lv hp xp
--   y=20          H-line
--   y=34,49,64,79 story text (4 lines, 15px)
--   y=88          H-line
--   y=103,118,133 choices (3 lines, 15px)
--   y=141         H-line
--   y=155         hint

local function hdr(char)
  local nxt = XP_NEXT[clamp(char.level, 1, #XP_NEXT)] or 99999
  return string.format("%s %s Lv.%d  HP:%d/%d  XP:%d/%d",
    char.race:sub(1,3), char.class:sub(1,3),
    char.level, char.hp, char.maxhp, char.xp, nxt)
end

local function draw_scene(char, scene_text, choices, cursor, roll_info)
  local lines = wrap(scene_text, 36)
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
  onion.display_text(hdr(char), 4, 14, { font="small", clear=false })
  onion.display_line(2, 20, W-2, 20, { clear=false })
  for i = 1, math.min(4, #lines) do
    onion.display_text(lines[i], 4, 19 + i*15, { font="small", clear=false })
  end
  if roll_info then
    onion.display_text(roll_info, 4, 79, { font="small", clear=false })
  end
  onion.display_line(2, 88, W-2, 88, { clear=false })
  for i, ch in ipairs(choices) do
    local y   = 88 + i * 15
    local pre = (i == cursor) and "> " or "  "
    onion.display_text(pre .. i .. ". " .. ch:sub(1, 28), 4, y, { font="small", clear=false })
  end
  onion.display_line(2, 141, W-2, 141, { clear=false })
  onion.display_text("UP/DN  SEL choose  CAN exit", 4, 155, { font="small", clear=false })
  onion.display_end_batch()
end

local function show_loading(line1, line2)
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
  onion.display_line(2, 28, W-2, 28, { clear=false })
  onion.display_text("  ~ DUNGEON MASTER ~", 24, 18, { font="bold",  clear=false })
  onion.display_text(line1 or "Consulting the fates...", 8, 72, { font="small", clear=false })
  if line2 then
    onion.display_text(line2, 8, 90, { font="small", clear=false })
  end
  onion.display_end_batch()
end

-- ─── Selection Screen ─────────────────────────────────────────────────────────

local function select_screen(title, options)
  local idx, n = 1, #options
  local function draw()
    onion.display_begin_batch()
    onion.clear_display()
    onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
    onion.display_text(title, 6, 16, { font="bold", clear=false })
    onion.display_line(2, 22, W-2, 22, { clear=false })
    for i, opt in ipairs(options) do
      local y   = 22 + i * 16
      local pre = (i == idx) and "> " or "  "
      onion.display_text(pre .. opt, 6, y, { font="small", clear=false })
    end
    onion.display_line(2, 154, W-2, 154, { clear=false })
    onion.display_text("UP/DN  SEL confirm  " .. idx .. "/" .. n, 6, 168, { font="small", clear=false })
    onion.display_end_batch()
  end
  draw(); wait_release()
  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then wait_release(); return options[idx] end
    if b.up   then idx = (idx - 2) % n + 1; draw(); wait_release() end
    if b.down then idx = idx % n + 1;        draw(); wait_release() end
    onion.sleep(50)
  end
end

-- ─── Alignment Screen ─────────────────────────────────────────────────────────

local function alignment_screen()
  local idx = 5
  local function draw()
    onion.display_begin_batch()
    onion.clear_display()
    onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
    onion.display_text("ALIGNMENT", 6, 16, { font="bold", clear=false })
    onion.display_line(2, 22, W-2, 22, { clear=false })
    for i, al in ipairs(ALIGNMENTS) do
      local col = (i-1) % 3
      local row = math.floor((i-1) / 3)
      local x, y = 4 + col*87, 44 + row*34
      if i == idx then
        onion.display_rect(x-2, y-12, 86, 18, { fill=true,  clear=false })
        onion.display_text(al, x, y, { font="small", color="white", clear=false })
      else
        onion.display_rect(x-2, y-12, 86, 18, { fill=false, clear=false })
        onion.display_text(al, x, y, { font="small", clear=false })
      end
    end
    onion.display_line(2, 148, W-2, 148, { clear=false })
    onion.display_text("L/R/U/D move   SEL confirm", 6, 163, { font="small", clear=false })
    onion.display_end_batch()
  end
  draw(); wait_release()
  while true do
    local b = onion.buttons()
    if b.cancel then return nil end
    if b.select then wait_release(); return ALIGNMENTS[idx] end
    if b.up    then idx = (idx-4)%9+1;                          draw(); wait_release() end
    if b.down  then idx = idx%9+1;                              draw(); wait_release() end
    if b.left  then if (idx-1)%3>0 then idx=idx-1 end;         draw(); wait_release() end
    if b.right then if (idx-1)%3<2 then idx=idx+1 end;         draw(); wait_release() end
    onion.sleep(50)
  end
end

-- ─── Character Creation ───────────────────────────────────────────────────────

local function new_character()
  local race = select_screen("CHOOSE YOUR RACE", RACES)
  if not race then return nil end
  local class = select_screen("CHOOSE YOUR CLASS", CLASSES)
  if not class then return nil end
  local alignment = alignment_screen()
  if not alignment then return nil end
  local hp = 10 + math.random(0, 4)
  return {
    race=race, class=class, alignment=alignment,
    level=1, xp=0, hp=hp, maxhp=hp, history="",
  }
end

-- ─── Start Screen ─────────────────────────────────────────────────────────────

local function start_screen(saved)
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
  onion.display_line(2, 28, W-2, 28, { clear=false })
  onion.display_text("  D&D FOREVER QUEST", 20, 18, { font="bold",    clear=false })
  if saved then
    onion.display_text(saved.race.." "..saved.class.." - Level "..saved.level, 8, 52, { font="regular", clear=false })
    onion.display_text("HP:"..saved.hp.."/"..saved.maxhp.."  XP:"..saved.xp, 8, 70, { font="small",   clear=false })
    onion.display_line(2, 90, W-2, 90, { clear=false })
    onion.display_text("> Continue", 8, 110, { font="bold",  clear=false })
    onion.display_text("  New Game", 8, 128, { font="small", clear=false })
  else
    onion.display_text("An AI-powered infinite quest.", 8, 60, { font="small", clear=false })
    onion.display_text("Every run is unique.", 8, 76, { font="small", clear=false })
    onion.display_line(2, 100, W-2, 100, { clear=false })
    onion.display_text("Press SELECT to begin", 20, 124, { font="small", clear=false })
  end
  onion.display_line(2, 148, W-2, 148, { clear=false })
  if saved then
    onion.display_text("SEL continue  UP/DN switch  CAN quit", 4, 162, { font="small", clear=false })
  else
    onion.display_text("SEL create character  CAN quit", 4, 162, { font="small", clear=false })
  end
  onion.display_end_batch()
end

-- ─── AI ───────────────────────────────────────────────────────────────────────

local function esc(s)
  return tostring(s):gsub('\\','\\\\'):gsub('"','\\"'):gsub('\n','\\n')
end

local function build_request(char, action, d20)
  local sys = string.format(
    'You are a terse D&D DM. Player: Lv.%d %s %s %s, HP %d/%d. ' ..
    'They rolled d20=%d this turn (high=good). ' ..
    'Return ONLY valid JSON: {"scene":"vivid 1-2 sentences max 110 chars",' ..
    '"choices":["a","b","c"],"xp":%d} ' ..
    'xp 10-40 based on danger. Each choice max 26 chars. No text outside JSON.',
    char.level, char.alignment, char.race, char.class,
    char.hp, char.maxhp, d20,
    math.random(10, 30)
  )
  local ctx = action
  if char.history ~= "" then
    ctx = "Story: " .. char.history .. " | Action: " .. action
  end
  return string.format(
    '{"model":"%s","max_tokens":220,"system":"%s","messages":[{"role":"user","content":"%s"}]}',
    MODEL, esc(sys), esc(ctx)
  )
end

local function parse_response(body)
  local text = body:match('"text"%s*:%s*"(.-[^\\])"')
  if not text then return nil end
  text = text:gsub('\\"','"'):gsub('\\n',' '):gsub('\\\\','\\')
  local scene   = text:match('"scene"%s*:%s*"(.-[^\\])"') or "The adventure continues..."
  local xp_gain = tonumber(text:match('"xp"%s*:%s*(%d+)')) or 15
  local choices = {}
  for c in text:gmatch('"([^"\\][^"]*)"') do
    if #choices < 3 and c ~= scene and #c > 3 and #c < 60 then
      choices[#choices+1] = c
    end
  end
  if #choices == 0 then choices = { "Press on", "Look around", "Rest" } end
  return { scene=scene, xp=xp_gain, choices=choices }
end

-- ─── History ──────────────────────────────────────────────────────────────────

local function update_history(h, action, scene)
  local s = h .. " " .. action .. ". " .. scene
  if #s > 160 then s = s:sub(#s-159):match("^%S*(.*)") end
  return s
end

-- ─── Main ─────────────────────────────────────────────────────────────────────

math.randomseed(os.time() + math.floor(os.clock() * 10000))

local saved = load_save()
local char

-- Start screen
start_screen(saved)
wait_release()

local choice = nil
if saved then
  -- navigate: SELECT=continue, DOWN=new game, CANCEL=quit
  local sel = 1  -- 1=continue, 2=new
  while true do
    local b = onion.buttons()
    if b.cancel then onion.release_display(); return end
    if b.select then choice = sel; wait_release(); break end
    if (b.up or b.down) and saved then
      sel = (sel == 1) and 2 or 1
      onion.display_begin_batch()
      onion.clear_display()
      onion.display_rect(1, 1, W-2, H-2, { fill=false, clear=false })
      onion.display_line(2, 28, W-2, 28, { clear=false })
      onion.display_text("  D&D FOREVER QUEST", 20, 18, { font="bold", clear=false })
      onion.display_text(saved.race.." "..saved.class.." - Level "..saved.level, 8, 52, { font="regular", clear=false })
      onion.display_text("HP:"..saved.hp.."/"..saved.maxhp.."  XP:"..saved.xp, 8, 70, { font="small", clear=false })
      onion.display_line(2, 90, W-2, 90, { clear=false })
      onion.display_text((sel==1 and "> " or "  ") .. "Continue", 8, 110, { font=(sel==1 and "bold" or "small"), clear=false })
      onion.display_text((sel==2 and "> " or "  ") .. "New Game", 8, 128, { font=(sel==2 and "bold" or "small"), clear=false })
      onion.display_line(2, 148, W-2, 148, { clear=false })
      onion.display_text("SEL continue  UP/DN switch  CAN quit", 4, 162, { font="small", clear=false })
      onion.display_end_batch()
      wait_release()
    end
    onion.sleep(50)
  end
else
  while true do
    local b = onion.buttons()
    if b.cancel then onion.release_display(); return end
    if b.select then choice = 2; wait_release(); break end
    onion.sleep(50)
  end
end

if choice == 1 then
  char = saved
else
  char = new_character()
  if not char then onion.release_display(); return end
  save(char)
end

-- Initial scene
show_loading("Conjuring your fate...")
local d20 = animate_roll(20)
local intro = string.format(
  "Begin a new adventure for a Lv.%d %s %s %s. d20=%d. Set opening scene.",
  char.level, char.alignment, char.race, char.class, d20
)
local res = onion.http_post(API_URL, build_request(char, intro, d20))

if not (res and res.status == 200) then
  local code = res and res.status or -1
  onion.display_begin_batch()
  onion.clear_display()
  onion.display_text("API error (status "..code..")", 4, 60, { font="small", clear=false })
  onion.display_text("Set key via serial:", 4, 80, { font="small", clear=false })
  onion.display_text("ai-key sk-ant-...", 4, 96, { font="small", clear=false })
  onion.display_end_batch()
  onion.sleep(4000)
  onion.release_display()
  return
end

local scene = parse_response(res.body)
if not scene then
  scene = { scene="Your adventure begins in darkness...", choices={"Look around","Draw weapon","Listen"}, xp=10 }
end

-- Adventure loop
local roll_info = nil

while true do
  -- Draw scene and wait for choice
  draw_scene(char, scene.scene, scene.choices, 1, roll_info)
  roll_info = nil
  wait_release()

  local cursor = 1
  local chosen = nil
  while true do
    local b = onion.buttons()
    if b.cancel then chosen = "cancel"; break end
    if b.select then wait_release(); chosen = scene.choices[cursor]; break end
    if b.up   and cursor > 1          then cursor = cursor - 1; draw_scene(char, scene.scene, scene.choices, cursor, nil); wait_release() end
    if b.down and cursor < #scene.choices then cursor = cursor + 1; draw_scene(char, scene.scene, scene.choices, cursor, nil); wait_release() end
    onion.sleep(50)
  end
  if chosen == "cancel" then break end

  -- Roll dice and show loading
  local turn_d20 = animate_roll(20)
  roll_info = "You rolled d20: " .. turn_d20 .. (turn_d20 >= 15 and "  (Great!)" or turn_d20 >= 10 and "  (OK)" or "  (Rough...)")
  show_loading("The DM considers...", roll_info)

  -- XP from previous scene
  local leveled, bonus = gain_xp(char, scene.xp or 15)
  save(char)
  if leveled then show_level_up(char, bonus) end

  -- Fetch next scene
  char.history = update_history(char.history, chosen, scene.scene)
  local next_res = onion.http_post(API_URL, build_request(char, chosen, turn_d20))

  if next_res and next_res.status == 200 then
    local parsed = parse_response(next_res.body)
    if parsed then
      scene = parsed
    else
      scene.scene = "The mists obscure the path... (parse err)"
    end
  else
    local code = next_res and next_res.status or -1
    scene.scene   = "The oracle falls silent. (err "..code..")"
    scene.choices = { "Try again", "Rest here", "Turn back" }
    scene.xp      = 0
  end
end

save(char)
onion.release_display()
