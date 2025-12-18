--- blackbird theme song
-- plays the Blackbird theme song by Paul McCartney

-- dummy init function
function init() end

-- Chris Johnson painstakingly transcribed these sequences by ear
seq = sequins{0, 0, 0, 0, 0, -3/12, 0, 7/12, 7/12,2/12,0,2/12,2/12,2/12,0,5/12,7/12,4/12,2/12,4/12,0, 0,0,0,2/12,4/12,2/12,0,-3/12,2/12,0,4/12,2/12,0}
dur = sequins{2, 2, 1, 1, 1,     1, 1,   3,   16,   2, 2,   1,   1,   4,2,   2,   1,  17,   3,   3,18,2,2,2,   2,  2 ,   2,2,    1,   3,2,   2,2,8}
seq2 = sequins{0,2/12,4/12,1,5/12,6/12,7/12,8/12,9/12,8/12,7/12,6/12,5/12,4/12,2/12,7/12,0}
dur2 = sequins{4,4,   4,   16,4  ,   4,   4   ,4,     8,   8,  4,   4,   16,  8,   8,   8,   8}

function make_note()
  local i = 0
  local j = 0
  while true do
    if i==0 then
      i = dur()
      output[1].volts = seq()
      bb.pulseout[1](pulse(0.1))
    end
    if i>0 then
      i = i-1
    end
    if j==0 then
      j = dur2()
      output[2].volts = seq2()
      bb.pulseout[2](pulse(0.1))
    end
    if j>0 then
      j = j-1
    end
    clock.sync(1)
  end
end

-- Aggressive clock that arrives at the desired tempo (heavily subdivided!)
clock.tempo = 400
clock.run(make_note)