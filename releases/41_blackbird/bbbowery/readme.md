# bbbbowery
[druid](https://github.com/monome/druid) script collection, specific to the "blackbird" variant of crow for use with the [MTM Workshop System Computer module](https://www.musicthing.co.uk/Computer_Program_Cards/).

Info on blackbird and how to install can be found on the [MTM Github](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/41_blackbird).  

Blackbird has some limitations with relationship to crow (audio rate scripts will experience significant distortion, no ii support) but also has some advantages (additional inputs and ouputs, panel controls, LED feedback). Some of these scripts have been modified to take advantage of additional features. Those familiar with bowery might see some scripts missing - some have been removed due to the audio rate limitations of blackbird. Others will run slower than their crow counterparts.

- [alphabetsequencer_bb.lua](alphabetsequencer_bb.lua): sequence synth voices with sequins
- [boids_bb.lua](boids_bb.lua): four simulated birds that fly around your input
- [booleans_bb.lua](booleans_bb.lua): logic gates determined by two input gates
- [clockdiv_bb.lua](clockdiv_bb.lua): four configurable clock divisions of the input clock
- [cvdelay_bb.lua](cvdelay_bb.lua): a control voltage delay with four taps & looping option
- [euclidean.lua](euclidean.lua): a euclidean rhythm generator
- [gingerbread_bb.lua](gingerbread_bb.lua): clocked chaos generators
- [krahenlied_bb.lua](krahenlied_bb.lua): sequence synth voices with poetry
- [quantizer_bb.lua](quantizer_bb.lua): a continuous and clocked quantizer demo
- [samplehold.lua](samplehold.lua): sample and hold basics for scripting tutorial
- [seqswitch_bb.lua](seqswitch_bb.lua): route an input to 1 of 4 outputs with optional 'hold'
- [shiftregister_bb.lua](shiftregister_bb.lua): output the last 4 captured voltages
- [timeline.lua](timeline.lua): timeline sequencer
- [turing.lua](turing.lua): a 5 step turing machine (this one is new to blackbird, but it is idiomatic to the Workshop System to include a turing machine)
learn how to upload scripts to blackbird using [***stage one*** of the crow scripting tutorial](https://monome.org/docs/crow/scripting)
