-- Desktop mock of the Onion OS `onion` Lua API, for testing generated book
-- scripts without a badge. Mirrors the surface used by reader_template.lua.
--
--   local stub = dofile("onion_stub.lua")
--   local state = stub.install({ presses = { {mask=8, times=1}, ... }, kv = {} })
--   dofile("../out/alice-p1.lua")
--   -- inspect state.events / state.kv / state.released
--
-- Button mask bits match the firmware: left=1 down=2 up=4 right=8
-- select=16 cancel=32. Each press entry is returned from onion.buttons()
-- `times` times; an exhausted queue reads as all-released.

local M = {}

local BITS = { left = 1, down = 2, up = 4, right = 8, select = 16, cancel = 32 }

function M.install(opts)
    local state = {
        presses = opts.presses or {},
        kv = opts.kv or {},
        events = {},
        logs = {},
        millis = 0,
        released = false,
    }

    local function record(kind, text)
        state.events[#state.events + 1] = { kind = kind, text = text }
    end

    local function next_mask()
        local entry = state.presses[1]
        if not entry then return 0 end
        entry.times = (entry.times or 1) - 1
        if entry.times <= 0 then table.remove(state.presses, 1) end
        return entry.mask or 0
    end

    onion = {
        log = function(msg) state.logs[#state.logs + 1] = tostring(msg) end,
        millis = function() return state.millis end,
        sleep = function(ms) state.millis = state.millis + (ms or 0) end,
        display_size = function() return { width = 264, height = 176 } end,

        buttons = function()
            local mask = next_mask()
            local t = { mask = mask }
            for name, bit in pairs(BITS) do
                t[name] = mask % (bit * 2) >= bit
            end
            return t
        end,

        display_begin = function() record("begin") end,
        display_commit = function() record("commit") end,
        display_flush = function() record("flush") end,
        display_rect = function() end,
        display_line = function() end,
        clear_display = function() record("clear") end,
        release_display = function() state.released = true end,

        display_lines = function(lines, _x, _y, _lh, _opts)
            if type(lines) == "table" then
                record("lines", table.concat(lines, "\n"))
            else
                record("lines", tostring(lines))
            end
        end,
        display_text = function(text, _x, _y, _opts)
            record("text", tostring(text))
        end,

        kv_get = function(key) return state.kv[key] end,
        kv_set = function(key, value)
            if value == nil then
                state.kv[key] = nil
            else
                state.kv[key] = tostring(value)
            end
            return true
        end,
        kv_del = function(key)
            state.kv[key] = nil
            return true
        end,
    }

    return state
end

return M
