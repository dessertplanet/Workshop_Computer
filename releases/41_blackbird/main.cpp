#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h" // for sleep_ms and printf
#include <cstdio>

/*

Output of data over a USB serial port, for e.g. debugging

This requires multicore processing, as USB is too slow to execute in ProcessSample.

To use, connect USB to a computer and run a serial terminal at 115200 baud
 */


class USBSerial : public ComputerCard
{
	// Variables for communication between the two cores
	volatile uint32_t v1, v2;
	
public:
	USBSerial()
	{
		// Start the second core
		multicore_launch_core1(core1);
	}

	// Boilerplate to call member function as second core
	static void core1()
	{
		((USBSerial *)ThisPtr())->SlowProcessingCore();
	}

	
	// Code for second RP2040 core, blocking
	void SlowProcessingCore()
	{
		// Display CV input values every ~10ms
		while (1)
		{
			printf("%ld\t%ld\n", v1, v2);
			sleep_ms(10);
		}
	}

	
	// 48kHz audio processing function
	virtual void ProcessSample()
	{
		// Copy the two CV inputs into v1 and v2, for transmission to printf
		v1 = CVIn1();
		v2 = CVIn2();

	}
};


int main()
{
	stdio_init_all();

	USBSerial usbs;
	usbs.Run();
}

  
