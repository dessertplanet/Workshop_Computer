--- gingerbread man chaotic attractor
-- input1: clock
-- main knob: offset (-5 to 5)
-- input2: add to offset, attenuate with y
-- knob x : slew for outputs
-- output1: X1
-- output2: Y1
-- output3: X2
-- output4: Y2

-- TODO view in x-y mode
function init()
  input[1]{ mode      = 'change'
          , direction = 'rising'
          }
end

-- two instances
gs = { {x=0,y=0}
     , {x=0,y=0}
     }

function make_bread(g, xoff, yoff)
  local x1 = g.x
  g.x = 0.5 - g.y + math.abs(x1) + xoff
  g.y = x1 + yoff
  if g.x > 5 then g.x = 5.0 end
  if g.y > 5 then g.y = 5.0 end
end

input[1].change = function()
  local off = 0.0
  local knoboff = (bb.knob.main * 10) - 5 -- -5 to 5 range
  if bb.connected.cv2 then
    off = knoboff + (input[2].volts * bb.knob.y)
  else
    off = knoboff
  end
  make_bread(gs[1], off, 0)
  make_bread(gs[2], 0, off)
  for n = 1,4 do output[n].slew = bb.knob.x * 0.5 end
  for n=1,2 do
    output[n*2-1].volts = gs[n].x
    output[n*2].volts = gs[n].y
  end
end
