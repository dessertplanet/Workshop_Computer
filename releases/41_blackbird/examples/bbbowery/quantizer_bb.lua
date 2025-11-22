--- quantizer
-- sam wolk 2019.10.15
-- updated by whimsicalraps 2021
-- adapted by qben for bb 2025
-- cv in 1: clock
-- cv in 2: voltage to quantize
-- cv out 1: in2 quantized to scale1 on clock pulses, 
-- cv out 2: in2 quantized to scale2 on clock pulses
-- audio out 1: linear envelope at clock pulses
-- audio out 2: log envelope at clock pulses
-- knobs x and y select scale
-- main knob is envelope decay

-- add your scales here!
scales =
{ {0}               -- octaves
, {}                -- chromatic
, {0,2,4,5,7,9,11}  -- major
, {0,2,3,5,7,8,10}  -- harmonic minor
, {0,2,3,5,7,9,10}  -- dorian
, {0,4,7}           -- major triad
, {0,4,7,10}        -- dominant 7th
, {0,2,4,6,8,10}    -- whole tone
}
names = {
    "octaves",
    "chromatic",
    "major",
    "harmonic minor",
    "dorian",
    "major triad",
    "dominant 7th",
    "whole tone",
}
scale1 = 1
scale2 = 1
scale3 = 1
-- update clocked outputs
input[1].change = function(state)
  output[1].volts = input[2].volts
  output[2].volts = input[2].volts
  setScales()
  output[3](ar(0.01,bb.knob.main,5,'lin'))
  output[4](ar(0.01,bb.knob.main,5,'log'))
end

-- update continuous quantizer (this is removed in favor of an additional envelope, here for reference)
--input[2].scale = function(sv)
  --output[3].volts = sv.volts
--end

function init()
  input[1].mode('change',1,0.1,'rising')
  input[2].mode('scale')
  setScales()
end

function tableIndex(v, c)
  if v < 0 then v = 0 end
  if v > 1 then v = 1 end
  local idx = (v * (c - 1)) // 1 + 1
  return idx
end

function setScales()
  local count = #scales
  scale1_old = scale1
  scale2_old = scale2
  scale3_old = scale3
  scale1 = tableIndex(bb.knob.x,count)
  scale2 = tableIndex(bb.knob.y,count)
  output[1].scale(scales[scale1])
  if scale1_old ~= scale1 then
    print("scale 1: "..names[scale1])
  end 
  output[2].scale(scales[scale2])
  if scale2_old ~= scale2 then
    print("scale 2: "..names[scale2])
  end
  output[3].scale(scales[scale3])
  if scale3_old ~= scale3 then
    print("scale 3: "..names[scale3])
  end
end
