-- test_public.lua
-- Verification script for norns public parameter + event compatibility on RP2040 crow emulator

print("=== test_public.lua (public param compatibility) ===")

-- Expectation:
--   On upload you should see a sequence of:
--     ^^ready()
--     ^^pub('gain',0.5,{0,1,"float"}) ...
--     ^^pub('mode',{'a','b','c'},{"option",'a','b','c"})
--     ^^pub('depth',0.25,{0,1,"float"})
--     ^^pub('_end')
--   Then periodic ^^pupdate('gain',X) packets as metro advances.

-- Declare initial public parameters
public.add("gain", 0.5, {0,1,"float"})
public.add("mode", {"a","b","c"}, {"option","a","b","c"})
public.add("depth", 0.25, {0,1,"float"})

-- Internal state
local t = 0
local dir = 1

-- Simple metro-driven modulation pattern
function init()
  print("init(): starting public param update metro")
  if metro and metro[1] then
    metro[1].time(0.5)    -- 500ms
    metro[1].start()
  end
end

-- Metro handler (crow-style)
function metro_handler(id, stage)
  if id == 1 then
    -- Triangle LFO on gain param 0..1
    t = t + dir * 0.1
    if t >= 1.0 then t = 1.0 dir = -1 end
    if t <= 0.0 then t = 0.0 dir = 1 end
    local new_gain = t
    public.update("gain", new_gain)

    -- Every 5 steps rotate mode
    if math.floor(stage) % 5 == 0 then
      local idx = math.floor((stage / 5) % 3) + 1
      local modes = {"a","b","c"}
      -- For option update we just set entire value
      public.update("mode", modes[idx])
    end

    -- Slow sine-ish depth change
    local depth = 0.5 + 0.5 * math.sin(stage * 0.1)
    public.update("depth", depth)
  end
end

-- Optional: manual rediscover trigger after 5 seconds (helps host compare)
function step()
  -- Called at audio rate; throttle rediscover call
  -- (Commented out to avoid spamming)
  -- if (some condition) then public.discover() end
end

print("Public param test script loaded. Watch for ^^pub / ^^pupdate events.")
