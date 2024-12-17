# ComputerCard

ComputerCard is a header-only C++ library, providing a framework that
manages the hardware aspects of the [Music Thing Modular Workshop
System Computer](https://www.musicthing.co.uk/workshopsystem/).

It aims to present a very simple C++ interface for card programmers 
to use the jacks, knobs, switch and LEDs, for programs running at
a fixed 48kHz audio sample rate.

Behind the scenes, the ComputerCard class:
- Manages the ADC and external multiplexer to collect analogue samples from audio inputs, CV inputs, knobs and switch.
- Applies smoothing, some simple hysteresis, and range extension to knobs and CV, to provide relatively low-noise values that span the whole range (-2048 to 2047). Knob/CV values are updated every audio frame.
- If enabled, determines whether jacks are connected to the six input sockets by driving the 'normalisation probe' pin and detecting its signal on the inputs.
- Manages PWM signals for CV output and brightness control of the six LEDs.
- Sends audio outputs to external DAC


## Usage:
Basic usage is intended to be extremely simple: for example, here is complete code for a Sample and Hold card:

```{.cpp}
#include "ComputerCard.h"

// Derive a new class from ComputerCard
class SampleAndHold : public ComputerCard
{
public:
	virtual void ProcessSample() // executed every sample at 48kHz
	{
    	// Update audio out 1 with value from audio in 1,
        // only if rising edge seen on pulse 1 input
		if (PulseIn1RisingEdge()) AudioOut1(AudioIn1());
	}
};

int main()
{
    // Create and run our new Sample and Hold
	SampleAndHold sh;
	sh.Run();
}
```
  
More generally, the process is:
1. Add `#include "ComputerCard.h"` to each cpp file that uses it. Make sure to `#define COMPUTERCARD_NOIMPL` before this include in all but one translation unit, so that the implementation is only included once.

2. Derive a new class from the (abstract) ComputerCard class, representing the particular card being written.

3. Do any card-specific setup in the constructor of the derived class

4. Override the pure virtual `ComputerCard::ProcessSample()` method to implement the per-sample functionality required for the particular card. `ComputerCard::ProcessSample()` is called at a fixed 48kHz sample rate.

5. Create an instance of the new derived class

6. Call the `ComputerCard::EnableNormalisationProbe()` method on it, if the normalisation probe is used.

7. Call the `ComputerCard::Run()` method on it to start audio processing. `ComputerCard::Run()` is blocking (never returns).


### Notes
- Make sure execution of `ComputerCard::ProcessSample` always runs quickly enough that it has returned before the next execution begins (~20us).
- It is anticipated that only one instance of a ComputerCard will be created.

### Limitations
As of initial version, notable omissions are:
- no use of EEPROM calibration
- no way to configure CV/knob smoothing filters
