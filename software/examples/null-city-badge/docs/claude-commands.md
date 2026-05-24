# Claude Code — Badge Development Workflows

How to use Claude Code effectively when working on the NULL City Badge.
Claude has full context of this codebase and retains memory across sessions.

---

## Common Workflows

### Build and flash

```
compile and flash the firmware
```

Runs `pio run --target upload` from `firmware/` and reports any errors.

### Change badge behavior

Describe what you want in plain English:

```
When I press SELECT on the menu, add a new screen that shows the
current uptime in seconds
```

Claude finds the right place in `main.cpp`, makes the edit, compiles,
and flashes.

### Debug a display issue

```
The text on the RNG screen is clipping off the right edge — fix it
```

Claude knows the font metrics (FreeMono9pt7b: 11 px per character,
18 px line height, 264 px wide display) and trims offending strings.

### Send an image to the badge

Open the art tool, import or draw an image, and click **Send to Badge**.
The badge saves it to flash (NVS) and uses it as the home screen
permanently. No Claude involvement needed for this step.

### Understand a piece of firmware

```
Explain how rising-edge button detection works in handle_buttons()
```

Claude will explain the `pressed = btns & ~g_last_btns` pattern in
plain English.

### Update or add guide content

The on-badge Hardware Guide and Software Guide are `const` arrays in
`main.cpp` (around line 96 and 209). Each screen is a `GuideScreen`
struct with a title and up to 6 lines of 23 characters each.

```
Add a new page to the Hardware Guide about the CC1101 radio module
```

---

## Project Memory

Claude maintains persistent memory for this project in:

```
~/.claude/projects/-home-hoot-null-city-badge-main/memory/
```

This includes notes about the correct display driver
(`GxEPD2_270_GDEY027T91`), font metrics, and anything else worth
remembering across sessions. You don't need to manage this manually.

---

## Creating a Slash Command

If you want a reusable automated workflow for a badge task (e.g. always
run a full compile-flash-monitor cycle), create a `.md` file in
`~/.claude/commands/`. The filename becomes the slash command. The file
should describe the goal, context, and step-by-step instructions for
Claude to follow.
