---alphabet sequencer
--start playback for outputs with start_playing() 
--stop playback on outputs with stop_playing()
--each channel is vertical - pitch on cv out, envelope on audio out, trigger on pulse out
--tempo is set by main knob
--decay on envelopes is set by knobs x and y
--try updating the sequins! add your own sequins to do other stuff!
--see comments below for which sequins do what

s = sequins
a = s{4, 6, 4, s{6, 8, 1, 11}} -- voice 1 pitch
b = s{1, 1, 1, 1, 1, 1} -- voice 1 timing
c = s{4, 1, 6, 1, 6} -- voice 2 pitch
d = s{2, 2, 2, 2, 2, 2} -- voice 2 timing

function init()
  --input[1].mode('clock') --this is how you would clock from input 1. this does not play well with knob control! 
  start_playing()
end

function check_clock()
  clock.tempo = (bb.knob.main * 200) + 40
end

function start_playing()
  check_clock()
  coro_1 = clock.run(notes_event)
  coro_2 = clock.run(other_event)
end

function stop_playing()
  clock.cancel(coro_1)
  clock.cancel(coro_2)
end

function notes_event()
  while true do
    clock.sync(b())
    output[1].volts = a()/12
    output[1].slew = .1
    output[3].action = ar(0.01, bb.knob.x, 5, 'lin') -- try 'log' shapes for snappier envelopes
    output[3]()
    bb.pulseout[1](pulse())
    check_clock()      
  end
end

function other_event()
  while true do
    clock.sync(d())
    output[2].volts = c()/12
    output[2].slew = .1
    output[4].action = ar(0.01, bb.knob.x, 5, 'lin')
    output[4]()
    bb.pulseout[2](pulse())
    check_clock()
  end

end
