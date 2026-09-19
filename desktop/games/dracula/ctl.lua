-- ctl.lua - drive MAME from a shell, for desktop/games/dracula/run.sh.
--
-- MAME runs this as -autoboot_script. Once a frame it looks for a file named
-- cmd.txt in $MAME_CTL, runs the one line in it, and deletes it:
--
--   key <text>    type into the emulated X68000 keyboard. Coded keys are in
--                 braces: {ENTER} {ESC} {SPACE} {UP} {DOWN} {F1} ...
--   reset         soft reset, back to the boot disk's music menu
--   snap          write a screenshot to -snapshot_directory
--
-- With MAME_SNAP_S set to a number of seconds, it also takes a screenshot
-- that often. That is how the game was driven from a terminal while the
-- counters were being read; it needs no OS-level input permission because the
-- keystrokes go straight into the emulated machine.
--
-- SPDX-License-Identifier: 0BSD

local dir    = os.getenv("MAME_CTL") or "."
local snap_s = tonumber(os.getenv("MAME_SNAP_S") or "0") or 0
local last   = 0

local function do_cmd(line)
  local m = manager.machine
  if line == "reset" then m:soft_reset()
  elseif line == "snap" then m.video:snapshot()
  elseif line:sub(1, 4) == "key " then
    m.natkeyboard.in_use = true
    m.natkeyboard:post_coded(line:sub(5))
  end
end

local function tick()
  local ok, err = pcall(function()
    if snap_s > 0 then
      local now = os.time()
      if now - last >= snap_s then last = now; manager.machine.video:snapshot() end
    end
    local f = io.open(dir .. "/cmd.txt", "r")
    if f then
      local line = f:read("*l"); f:close(); os.remove(dir .. "/cmd.txt")
      if line and #line > 0 then do_cmd(line) end
    end
  end)
  if not ok then io.stderr:write("ctl.lua: " .. tostring(err) .. "\n") end
end

emu.register_frame_done(tick)
