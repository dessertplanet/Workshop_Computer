---krahenlied
--a poetry sequencer for crow, just friends, and w/tape
--input 1: clock
--outputs 1 & 3: v/8
--outputs 2 & 4: AR envelopes
--begin by giving your poem a title in druid using the title function (i.e., typing title followed by your title in quotes), like so:
--title "Christabel"
--this will start the clocks running and create an initial sequence
--continue by updating the sequence in druid by typing text followed by a new line of poetry in quotes — e.g.,
--text "'Tis the middle of night by the castle clock,"
--note: it's probably best to reset crow after using this script and before using another one or using this one again. Not doing so can lead to crashes
  text_string = "aaaaaa"
  function remap(ascii)
    ascii = ascii % 32 + 1
    return ascii
  end
  function processString(s)
    local tempScalar = {}
    for i = 1, #s do
      table.insert(tempScalar,remap(s:byte(i)))
    end
    return tempScalar
  end
  function set()
    s:settable(processString(text_string))
  end
  function title(str)
    text_string = str
    set()
    coro_id = clock.run(notes_event)
              clock.run(other_event)   
  end
  function text(str)
    text_string = str
    set()
  end
  function init()
    input[1].mode('clock')
    bpm = clock.tempo  
  end
  function notes_event()
    while true do
      clock.sync(s()/s:step(2)())
      output[1].volts = s:step(3)()/12
      output[1].slew = s:step(4)()/300
      output[2].action = ar(s:step(5)()/20, s:step(6)()/20, j:step(7)(), 'linear')
      output[2]()
    end
  end
  function other_event()
    while true do
      clock.sync(s:step(8)()/s:step(9)())
      output[3].volts = s:step(10)()/12
      output[3].slew = s:step(11)()/300
      output[4].action = ar(s:step(12)()/20, s:step(13)()/20, j:step(14)(), 'linear')
      output[4]()
    end
  end
