-- recon.lua: log every I/O register access, per-second activity and game frame cadence.
-- env: RECON_OUT (prefix for output files), RECON_SNAP (seconds between snapshots, 0=off),
--      RECON_INPUT (script of timed presses "t:port:field:dur,..." in emulated seconds)
local out = os.getenv("RECON_OUT") or "recon"
local snap_every = tonumber(os.getenv("RECON_SNAP") or "10")
local input_spec = os.getenv("RECON_INPUT") or ""

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]

local regs = {}      -- [addr] = {w=count, r=count, first=t, vals={}, nv=0, last=v}
local sec_w = {}     -- per second block counters
local sec_r = {}     -- per second reads per register (polling detector)
-- RECON_MASH="port|field|period|start|dur;..." presses a button every period seconds
local mash = {}
for item in string.gmatch(os.getenv("RECON_MASH") or "", "[^;]+") do
  local port, field, period, start, dur = string.match(item, "([^|]+)|([^|]+)|([%d%.]+)|([%d%.]+)|([%d%.]+)")
  if port then mash[#mash + 1] = {port = port, field = field, period = tonumber(period), start = tonumber(start), dur = tonumber(dur)} end
end
local function block(a) return a & 0xffff0000 end

local function note(addr, kind, data)
  local r = regs[addr]
  if not r then r = {w = 0, r = 0, first = emu.time(), vals = {}, nv = 0, last = 0}; regs[addr] = r end
  if kind == "w" then
    r.w = r.w + 1; r.last = data
    if r.nv < 12 and not r.vals[data] then r.vals[data] = true; r.nv = r.nv + 1 end
    local b = block(addr); sec_w[b] = (sec_w[b] or 0) + 1
  else
    r.r = r.r + 1
    sec_r[addr] = (sec_r[addr] or 0) + 1
  end
end

-- flips: TV start address / buffer select / PPU registers that look like frame commits
local flips = 0
local flip_addr = {[0x08090020] = true, [0x08070000] = true, [0x08070004] = true, [0x08070008] = true}

_G.wtap = space:install_write_tap(0x08000000, 0x0824ffff, "reconw", function(offset, data, mask)
  local a = offset & ~3
  note(a, "w", data)
  if flip_addr[a] then flips = flips + 1 end
end)
-- RFID gate pulse widths (bit 1 of P_CSI_GPIO_SETUP), in 13.56 MHz carrier cycles, bucketed by 50
local gate_hist, gate_level, gate_t = {}, 0, 0
_G.gtap = space:install_write_tap(0x08200024, 0x08200027, "recong", function(offset, data, mask)
  if mask & 0xff == 0 then return end
  local lvl = (data >> 1) & 1
  if lvl ~= gate_level then
    local now = emu.time()
    if gate_level == 1 then
      local clk = math.floor((now - gate_t) * 13560000)
      local b = (clk // 50) * 50
      gate_hist[b] = (gate_hist[b] or 0) + 1
    end
    gate_level, gate_t = lvl, now
  end
end)
local uart = io.open(out .. "_uart.txt", "wb")
_G.utap = space:install_write_tap(0x08150000, 0x08150003, "reconu", function(offset, data, mask)
  uart:write(string.char(data & 0xff))
end)
_G.rtap = space:install_read_tap(0x08000000, 0x0824ffff, "reconr", function(offset, data, mask)
  note(offset & ~3, "r", data)
end)

-- timed input
local presses = {}
for item in string.gmatch(input_spec, "[^,]+") do
  local t, port, field, dur = string.match(item, "([%d%.]+):([^:]+):([^:]+):([%d%.]+)")
  if t then presses[#presses + 1] = {t = tonumber(t), port = port, field = field, dur = tonumber(dur)} end
end

local log = io.open(out .. "_sec.txt", "w")
local wall0 = os.clock()
local last_t, last_wall, last_flips = 0, 0, 0
local next_snap = snap_every

local function dump()
  local f = io.open(out .. "_regs.txt", "w")
  local keys = {}
  for a in pairs(regs) do keys[#keys + 1] = a end
  table.sort(keys)
  for _, a in ipairs(keys) do
    local r = regs[a]
    local vs = {}
    for v in pairs(r.vals) do vs[#vs + 1] = string.format("%x", v) end
    table.sort(vs)
    f:write(string.format("%08x w=%d r=%d first=%.2f last=%x vals=%s%s\n", a, r.w, r.r, r.first, r.last,
      table.concat(vs, ","), r.nv >= 12 and ",..." or ""))
  end
  f:close()
end

_G.fsub = emu.add_machine_frame_notifier(function()
  local ok, err = pcall(function()
    local t = emu.time()
    for _, p in ipairs(presses) do
      local fld = manager.machine.ioport.ports[p.port] and manager.machine.ioport.ports[p.port].fields[p.field]
      if fld then
        if t >= p.t and t < p.t + p.dur then fld:set_value(1) elseif t >= p.t + p.dur and not p.done then fld:set_value(0); p.done = true end
      end
    end
    for _, m in ipairs(mash) do
      local port = manager.machine.ioport.ports[m.port]
      local fld = port and port.fields[m.field]
      if fld and t >= m.start then
        local ph = (t - m.start) % m.period
        fld:set_value(ph < m.dur and 1 or 0)
      end
    end
    if t - last_t >= 1.0 then
      local wall = os.clock() - wall0
      local parts = {}
      for b, n in pairs(sec_w) do parts[#parts + 1] = string.format("%04x:%d", b >> 16, n) end
      table.sort(parts)
      local rs = {}
      for a, n in pairs(sec_r) do rs[#rs + 1] = {a, n} end
      table.sort(rs, function(x, y) return x[2] > y[2] end)
      local rparts = {}
      for i = 1, math.min(3, #rs) do rparts[#rparts + 1] = string.format("%08x:%d", rs[i][1], rs[i][2]) end
      log:write(string.format("t=%.0f host=%.0f%% flips=%d w[%s] r[%s]\n", t, 100 * (t - last_t) / math.max(wall - last_wall, 1e-6),
        flips - last_flips, table.concat(parts, " "), table.concat(rparts, " ")))
      log:flush()
      sec_w = {}; sec_r = {}
      last_t, last_wall, last_flips = t, wall, flips
    end
    if snap_every > 0 and t >= next_snap then
      manager.machine.video:snapshot()
      next_snap = next_snap + snap_every
      dump()
    end
  end)
  if not ok then io.stderr:write("recon.lua: " .. tostring(err) .. "\n") end
end)

local function dump_ram(name, a0, a1)
  local f = io.open(out .. "_" .. name .. ".bin", "wb")
  local chunk = {}
  for a = a0, a1 - 1, 4 do
    chunk[#chunk + 1] = string.pack("<I4", space:read_u32(a))
    if #chunk == 4096 then f:write(table.concat(chunk)); chunk = {} end
  end
  f:write(table.concat(chunk)); f:close()
end

_G.stopsub = emu.add_machine_stop_notifier(function()
  dump(); log:close(); uart:close()
  local gf = io.open(out .. "_gate.txt", "w")
  local ks = {}
  for k in pairs(gate_hist) do ks[#ks + 1] = k end
  table.sort(ks)
  for _, k in ipairs(ks) do gf:write(string.format("%5d-%5d clk %7d\n", k, k + 49, gate_hist[k])) end
  gf:close()
  dump_ram("low", 0xa0000000, 0xa0090000)    -- BIOS-resident image incl. the 0xa0000800 table
end)

-- list input ports once, for building RECON_INPUT
local pf = io.open(out .. "_ports.txt", "w")
for tag, port in pairs(manager.machine.ioport.ports) do
  for name, _ in pairs(port.fields) do pf:write(tag .. " : " .. name .. "\n") end
end
pf:close()
