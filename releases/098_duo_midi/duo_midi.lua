--- duo midi

-- Disclaimer: This script uses APIs that only work on an unreleased version of Blackbird firmware 
-- (that firmware is included in the UF2 here so this works but other general Blackbird things are untested)

-- Dune's custom MIDI card. Device or Host mode both work (switching is automatic but requires a WS power cycle)
-- Switch up: Duophonic (dynamically assigned channels)
-- Switch middle: Monophonic (both channels match)

-- Main knob: Velocity sensitivity for envelopes
-- X knob: Envelope attack
-- Y knob: Envelope release

-- CV ouputs: Pitch 
-- Audio outputs: ASR envelopes
-- Pulse outputs: 10ms trigger whenever a new voice is assigned

-- constants
BEND_RANGE = 2
ENV_SHAPE = 'linear'

-- globals
switch_mode = nil
voice_data = {nil,nil}
num_active_voices = 0
bend_volts = 0.0
gate_mode = false

-- Avoid per-note allocations from constructing a new pulse() object.
PULSE_10MS = pulse(0.01)

-- defined my own ASR envelope instructions using crows ASL language
function custom_asr(a,s,r,shape)
  local low = -1
  a = a or 0.1
  s = s or 6.0
  r = r or 0.5
  shape = shape or ENV_SHAPE
  return {
    held{ to(s,a,shape) },
    to(low,r,shape)
  }
end

-- init sets up actions
function init()
  for i=3,4 do
    output[i].volts = -1
    output[i].action = custom_asr(
    dyn{attack=0.1},
    dyn{sustain=6},
    dyn{release=0.5}
  )
  end
  
  bb.switch.mode = 'change'
  bb.switch.change(bb.switch.position)
  for i = 1,2 do
    bb.pulsein[i]{mode = 'change',  direction = 'both'}
    bb.pulsein[i].change = function(state)
      update_params(i,127)
      output[i+2]( state )
    end
  end
end

-- When a new key is pressed, decide which voice the note-on should go to based on
-- proximity to other live voices & how many voices are currently active
function voice_dispatch(note)
  local nearby = false
  if  num_active_voices == 2 then
    if math.abs(note - voice_data[1]) < math.abs(note - voice_data[2]) then
      return 1
    else
      return 2
    end
  elseif  num_active_voices == 1 then 
    if voice_data[2] then 
      nearby = math.abs(voice_data[2] - note) < 5
      if nearby then return 2 else return 1 end
    else
      nearby = math.abs(voice_data[1] - note) < 5
      if nearby then return 1 else return 2 end
    end
  elseif  num_active_voices == 0 then
    if note%40 < 16 then
      return 1
    else
      return 2
    end
  else
    print('voice dispatch error: num_active_voices= ', num_active_voices)
    return -1
  end
end

-- Helper function to update envelope parameters
function update_params(chan, vel)
  if vel then output[chan+2].dyn.sustain = 6 * ((1-bb.knob.main) + (bb.knob.main * vel / 127)) end
  output[chan+2].dyn.attack = bb.knob.x * 2
  output[chan+2].dyn.release = bb.knob.y * 2
end

-- Midi note callback
bb.midi.rx.note = function(type,num,vel)
  if type == 'on' then
    if switch_mode == 'duo' then
      -- duo note on
      local chan = voice_dispatch(num)
      output[chan].volts = ( num - 41 ) / 12 + bend_volts
      update_params(chan,vel)
      if not voice_data[chan] then 
        num_active_voices = num_active_voices + 1
        output[chan+2]( true ) 
        if gate_mode then
          bb.pulseout[chan]:high()
        else
          bb.pulseout[chan]( PULSE_10MS )
        end
      end
      voice_data[chan] = num
    elseif switch_mode == 'mono' then
      -- mono note on
      for i = 1,2 do
        output[i].volts = ( num - 41 ) / 12 + bend_volts
        update_params(i,vel)
        output[i+2]( true )
        if gate_mode then
          bb.pulseout[i]:high()
        else
          bb.pulseout[i]( PULSE_10MS )
        end
        if not voice_data[i] then num_active_voices = num_active_voices + 1 end
        voice_data[i] = num
      end
    end
  else
    -- note off (same for both modes)
    for i,v in pairs(voice_data) do
      if v == num then
          output[i+2]( false )
          update_params(i)
          voice_data[i] = nil
          num_active_voices = num_active_voices - 1
          if gate_mode then
            bb.pulseout[i]:low()
          end
      end
    end
  end
end

-- midi pitchbend callback
bb.midi.rx.bend = function(b)
  bend_volts = b * BEND_RANGE / 12 -- two semitone range
  for i,v in pairs(voice_data) do
    if v then
      output[i].volts = ( v - 41 ) / 12 + bend_volts
    end
  end
end

-- filter incoming CCs and use mod-wheel to configure pitch slew
bb.midi.rx.cc = function(num,val)
  if num == 1 then
    for i = 1,2 do
      output[i].slew = val / 127 -- mod wheel controls up to 1 second slew
    end
  end
end

-- change mode based on switch position
bb.switch.change = function(new_state)
  if new_state ~= 'down' then
    if new_state == 'up' then
      switch_mode = 'duo'
    elseif switch_mode ~= 'mono' then
      switch_mode = 'mono'
    end
  else 
    -- switch is down
    gate_mode = not gate_mode
    bb.pulseout[1]:low()
    bb.pulseout[2]:low()
  end
end
