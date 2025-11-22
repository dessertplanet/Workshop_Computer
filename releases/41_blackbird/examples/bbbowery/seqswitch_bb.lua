--- sequential switch
-- in1: signal
-- in2: switch
-- out1-4: i1 sent through sequentially
-- knob x: slew time
-- knob y: cv offset
-- knob main: cv attenuator
-- switch: advance channel

-- user settings
public{mode = 'hold'}:options{'hold','zero'} -- when switch closes, should hold last value?

-- private
dest = 1

function init()
  input[1]{ mode = 'stream'
          , time = 0.05
          }
  input[2]{ mode = 'change'
          , direction = 'rising'
          }
  for n=1,4 do
    output[n].slew = 0.2 * bb.knob.x -- interpolate at the stream rate
  end
end

input[1].stream = function(v)
  output[dest].slew = 0.2 * bb.knob.x
  output[dest].volts = (v * bb.knob.main) + (bb.knob.y * 5)
end

bb.switch.change = function(p)
  if p == 'down' then
    if public.mode == 'zero' then output[dest].volts = 0 end
    dest = (dest % 4)+1 -- rotate channel
  end
end

input[2].change = function()
  if public.mode == 'zero' then output[dest].volts = 0 end
  dest = (dest % 4)+1 -- rotate channel
end
