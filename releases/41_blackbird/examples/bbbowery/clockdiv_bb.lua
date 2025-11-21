--- clock divider
-- in1: clock input
-- main knob: division selector
-- in2: division selector (see divs) when patched
-- when patched x becomes attenuator for in2, main becomes offset
-- out1-4: divided outputs

function tableindex(v, c) -- given a value between 0 and 1 selects an item from 1 to c
  if v < 0 then v = 0 end
  if v > 1 then v = 1 end
  local idx = (v * (c - 1)) // 1 + 1
  return idx
end

function newdiv()
  print("new div window selected: "..win_ix)
  for n = 1, 4 do
    output[n].clock_div = windows[win_ix].v[n] -- changed line
  end
end

-- choose your clock divisions
windows = {
  public({ win1 = { 5, 7, 11, 13 } }):action(newdiv),
  public({ win2 = { 3, 5, 7, 11 } }):action(newdiv),
  public({ win3 = { 2, 3, 5, 7 } }):action(newdiv),
  public({ win4 = { 2, 4, 8, 16 } }):action(newdiv),
  public({ win5 = { 4, 8, 16, 32 } }):action(newdiv),
}
win_ix = 3

function getdiv()
  local selector = 0
  if bb.connected.cv2 then
    selector = bb.knob.main + (input[2] / 5 * bb.knob.y) -- assumes -5 to 5 range
  else
    selector = bb.knob.main
  end
  newdiv(windows(tableindex(selector,windows#))) 

function init()
  input[1].mode("clock", 1 / 4)
  --input[2].mode("window", { -3, -1, 1, 3 })
  for n = 1, 4 do
    output[n]:clock(public.win3[n])
  end
  getdiv()
end

--input[2].window = function(win, dir)
--  win_ix = win
--  newdiv(windows[win])
--end
