## Hyprland (`thefunbranch`)

Based on upstream [Hyprland](https://github.com/hyprwm/Hyprland) (BSD-3-Clause).

### Persistent Lua State

Module-scoped locals survive reloads. Useful for tracking state across re-evals:

```lua
local rec_pid = nil

function M.wfrecord(output, opts)
    return function()
        if rec_pid then
            print("Already recording")
            return
        end

        opts = opts or {}
        if opts.audio == nil then opts.audio = true end

        local rec_file = (opts.dir or v.record_dir) .. "/" .. os.date("%d-%m-%y_%H-%M-%S") .. ".mp4"
        local cmd      = string.format("gpu-screen-recorder -w %s -o '%s'", output, rec_file)
        local pid      = hl.exec_raw(cmd)

        if pid and pid > 0 then
            rec_pid = pid
            print("Recording started with PID:", rec_pid)
        end
    end
end
```

### Hot Reload (Module-level)

Only the changed module + dependents get re-evaluated, not everything:

```lua
-- hyprland.lua
require("my-theme")
require("bindings")
```

Editing `my-theme.lua` reloads just that module and modules that depend on it.

### Scrolling layout

Scrolling layout has been refactored and simplified. column width toggle and maximize behavior all work properly now.

### Sync/Async dispatchers

`hl.exec_cmd()` and `hl.exec_raw()` return a PID. `hl.async_exec_cmd()` returns a handle for waiting:

```lua
hl.bind("SUPER + Q", function()
  local h = hl.async_exec_cmd("echo $HOME")
  local res = h()
  if res and res.ok then
    print("Home dir:", res.out)
  end
end)

hl.bind("SUPER + W", function()
  local h = hl.async_exec_cmd("kitty")
  local res = h()
  if res and res.ok then
    print("Kitty closed. :(")
  end
end)

local pid = nil
hl.bind("SUPER + E", function()
  if pid then
    print("Already running")
    return
  end
  pid = hl.exec_raw("kitty")
end)
```

### Printing objects

Objects like monitors, windows, and workspaces expose their fields via `pairs`:

```lua
local m = hl.get_active_monitor()
if m then
    for k, v in pairs(m) do
        print(string.format("%-20s:%s", k, v))
    end
end
```

### Coroutines

Coroutines persist across reloads. Create one to run on load, yield, and resume later:

```lua
local co = coroutine.create(function()
    print("first run")
    coroutine.yield()
    print("resumed after reload")
end)
coroutine.resume(co)

if not _G.saved_co then
    _G.saved_co = co
end

if _G.saved_co and coroutine.status(_G.saved_co) == "suspended" then
    coroutine.resume(_G.saved_co)
end
```

### Rollinglog

`hyprctl rollinglog` now supports level filtering with `-l`:

```
hyprctl rollinglog -f -l lua
hyprctl rollinglog -f -l warn
hyprctl rollinglog -f -l error
hyprctl rollinglog -f -l trace
```

Log output is now colored.
