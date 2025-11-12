-- turing.lua

function init() 
    max = tonumber('11111', 2) -- maximum binary value is all 1's (matches bits table size)
    bits = {0,0,0,0,0} -- initialize bits (size matches max string length above)

    range = 4.0 -- output range in volts

    output[1].scale = {0,2,4,5,7,9,11} -- configure output quant to major scale
    output[2].action = ar(0,0.4) -- configure output 2 as an attack/release envelope ( attack = 0s, release = 0.4s ) 

    bb.pulsein[1]{ mode = 'change', direction = 'rising'} -- configure pulse input 1 as trigger
end

bb.pulsein[1].change = function()
    first = table.remove(bits,1)  -- remove first bit, add it at the end (rotates the table of bits)
    table.insert(bits, first)

    -- compare noise against main knob and flip the first bit if noise >= knob
    -- Tiny scaling to ensure that bit always flips when knob is at 0.0, never flips when knob is at 1.0
    if math.random() * 0.999 >= bb.knob.main then
        bits[1] = 1 - bits[1]
    end

    bits_string = table.concat(bits) -- string version like '00111'
    bits_int = tonumber(bits_string,2) -- integer 0 to max
    bits_float = bits_int / max -- float 0.0 to 1.0

    output[1].volts = bits_float * range -- send the scaled output voltage to output 1 (where it will be quantized)
    output[2]() -- trigger the envelope on output 2
end