--- control voltage delay
-- input1: CV to delay
-- knob x: slew amount
-- knob main: crossfade dry/wet (when input2 not patched)
-- knob main attenuates input2 when patched
-- knob y offsets input2 when patched
-- output1-4: delay equaly spaced delay taps


LENGTH = 100 -- max loop time. MUST BE CONSTANT

public{tap1 = 25}:range(1,LENGTH):type'slider'
public{tap2 = 50}:range(1,LENGTH):type'slider'
public{tap3 = 75}:range(1,LENGTH):type'slider'
public{tap4 = 100}:range(1,LENGTH):type'slider'

bucket = {}
write = 1
cv_mode = 0

function init()
    input[1].mode('stream', 0.001) -- 1kHz
    for n=1,4 do output[n].slew = 0.002 end -- smoothing at nyquist
    for n=1,LENGTH do bucket[n] = 0 end -- clear the buffer
end

function peek(tap)
    local ix = (math.floor(write - tap - 1) % LENGTH) + 1
    return bucket[ix]
end

function poke(v, ix)
    --local c = (input[2].volts / 4.5) + public.loop
    if bb.connected.cv2 then
        c = (input[2].volts / 5) * bb.knob.y + bb.knob.main
    else
        c = bb.knob.main
    end
    local c = bb.knob.main
    c = (c < 0) and 0 or c
    c = (c > 1) and 1 or c
    bucket[ix] = v * c + (bucket[ix]*(1 - c))
end

input[1].stream = function(v)
    for n=1,4 do
        output[n].slew = 0.002 + (bb.knob.x * 0.1) -- smoothing at nyquist plus knob y
    end
    output[1].volts = peek(public.tap1)
    output[2].volts = peek(public.tap2)
    output[3].volts = peek(public.tap3)
    output[4].volts = peek(public.tap4)
    poke(v, write)
    write = (write % LENGTH) + 1
end
