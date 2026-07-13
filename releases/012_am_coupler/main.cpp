#include "WavFile.h"
#include "ComputerCard.h"

#include "pico/stdlib.h" 
#include <cstdio>
#include <cstring>
#include <vector>
#include "pico/multicore.h"
#include <algorithm>

volatile int32_t amValue=0, fqValue=1024000;

class WindowNR
{
public:
	WindowNR(uint32_t size_)
	{
		size = (int32_t)size_;
		current = 0;
	}

	int32_t operator()(int32_t in)
	{
		if ((in > current + size) || (in < current - size)) current = in;
		return current;
	}
private:
	int32_t size, current;

};

class AMCoupler : public ComputerCard
{
public:	
	AMCoupler() : mainKnobNR(5)
	{
		currentFile = 0;
		numFiles = 0;
		LoadWAVsFromFlash();

		startupTimer = 48000;
		
		// Set up UART TX pin as RF output
		SetupDebugPinAsPWM();

		// Ground the UART_RX pin
		gpio_init(DEBUG_2);
		gpio_set_dir(DEBUG_2, GPIO_OUT);
		gpio_put(DEBUG_2, false);	
	}

	static void __not_in_flash_func(OnRFPWMWrap)()
	{	
		static int32_t amError = 0, fqError = 0;
		int32_t amv = amValue, fqv = fqValue;
		//sio_hw->gpio_set = 2; // turn on DEBUG_2 LED
		uint32_t amTruncated = (amv - amError) & 0xFFFFF000;
		uint32_t fqTruncated = (fqv - fqError) & 0xFFFFF000;
		
	   	amError += amTruncated - amv;
		fqError += fqTruncated - fqv;
		pwm_hw->slice[0].cc = amTruncated >> 12;
		pwm_hw->slice[0].top = fqTruncated >> 12;
		//sio_hw->gpio_clr = 2; // turn off DEBUG_2 LED
		
		pwm_hw->intr = 1; // clear interrupt flag for slice 0
	}

	
	void SetupDebugPinAsPWM()
	{
		gpio_set_function(DEBUG_1, GPIO_FUNC_PWM);
		// now create PWM config struct
		pwm_config config = pwm_get_default_config();
		pwm_config_set_wrap(&config, 220);

		// now set this PWM config to apply to the two outputs
		pwm_init(pwm_gpio_to_slice_num(DEBUG_1), &config, true);
		
		// set initial level 
		pwm_set_gpio_level(DEBUG_1, 0);

		
		// Turn on IRQ for CV output PWM
		uint slice_num = pwm_gpio_to_slice_num(DEBUG_1);
		pwm_clear_irq(slice_num);
		pwm_set_irq_enabled(slice_num, true);
		
		irq_set_exclusive_handler(PWM_IRQ_WRAP, AMCoupler::OnRFPWMWrap);
		irq_set_priority(PWM_IRQ_WRAP, PICO_HIGHEST_IRQ_PRIORITY);
		irq_set_enabled(PWM_IRQ_WRAP, true);	
	}
	
	// Load WAV files uploaded to the RP2040 using a UF2 from the generate_sample_uf2.html page
	int LoadWAVsFromFlash()
	{
		// Look in last 256-byte block of flash to get start address and number of WAV files
		uint32_t *wavStart = ((uint32_t *)(XIP_BASE+ PICO_FLASH_SIZE_BYTES - 256 )); 
		uint32_t wavStartAddress = *wavStart;
		wavStart++; // advance to next 4-byte region
		numFiles = *wavStart; // get number of WAV files

		// If an invalid number of files, probably no valid sample UF2 was uploaded
		if (numFiles == 0 || numFiles > 256)
		{
			numFiles = 0;
			return -1;
		}
		
		// If start address is not in the right range, probably no valid sample UF2 was uploaded
		if (wavStartAddress < XIP_BASE || wavStartAddress >= XIP_BASE + PICO_FLASH_SIZE_BYTES)
		{
			numFiles = 0;
			return -1;
		}

		// If above tests pass, create list of wav files
		wavfiles = std::vector<WAVFile>(numFiles);

		// Loop through wav files, processing each in turn
		uint8_t *wavptr = ((uint8_t *)(wavStartAddress));
		for (unsigned i=0; i<numFiles; i++)
		{
			int res = wavfiles[i].Load(wavptr);
			if (res) // if this load failed, give up here
			{
				numFiles = i;
				return -2;
			}
			else
			{
				// Increment file start pointer to next wav file
				wavptr += wavfiles[i].FileSize();
			}
		}

		return 0; // success
	}


	int32_t PlayWav()
	{
		uint32_t speed = 1024;

		// If there are no files, upload nothing.
		if (numFiles == 0) return 0;
		
		sampleIndex += (speed*wavfiles[currentFile].SampleRate())/(48000<<2);

		// If we go past the end of the file...
		if (sampleIndex >= (wavfiles[currentFile].NumSamples()<<8))
		{
			// Wrap the sample index
			sampleIndex -= wavfiles[currentFile].NumSamples()<<8;
		}

		// Look up interpolated sample
		int16_t sample = wavfiles[currentFile].Interp8(sampleIndex);
		return sample >>= 4; // convert from 16-bit WAV to 12-bit DAC output
	}
	
	virtual void ProcessSample()
	{
		if (startupTimer)
		{
			if (startupTimer > 24000)
			for (int i=0; i<6; i++)
			{
				LedBrightness(i, (startupTimer-24000)>>3);
			}
			else
			{
				for (int i=0; i<6; i++)
				{
					LedOff(i);
				}
			}
			startupTimer--;
			return;
		}
		
		// Switch in middle position turns RF output off
		bool rfOn = SwitchVal() != Middle;

		// If a jack is connected to pulse in 1, pulse input turns RF on and off 
		if (Connected(Pulse1)) rfOn = rfOn && PulseIn1();
		
		LedOn(0, rfOn);

		
		// Modulation signal is sum of the two audio inputs
		int32_t signal = 0;
		if (Connected(Audio1)) signal += AudioIn1();
		if (Connected(Audio2)) signal += AudioIn2();

		// Also, if a WAV file has been uploaded using the sample_upload interface:
		// - play that out of Audio Out 1
		// - automatically feed that into modulation signal, if no Audio Inputs connected.
		int32_t sampledAudio = PlayWav();
		if (Disconnected(Audio1) && Disconnected(Audio2)) signal += sampledAudio;
		AudioOut1(sampledAudio);

		// Use knob Y to set overall volume
		int32_t vol = std::max(KnobVal(Y)-30, 0l);
		vol = (vol*vol)>>12;
		signal = (signal*vol)>>12;

		if (signal>2047) signal=2047;
		if (signal<-2047) signal=-2047;

		// Output signal that will be broadcast on Audio output 2
		AudioOut2(signal);
		
		int ledSignal = std::abs(signal*2); // 0-4094
		int led1 = (ledSignal<1024)?0:(((ledSignal-1024)*5461)>>12);
		int led3 = (ledSignal<256)?0:((ledSignal>=1024)?4095:(((ledSignal-256)*21845)>>12));
		int led5 = (ledSignal>=256)?4095:(ledSignal<<4);
		if (!rfOn) // dim VU meter if not transmitting
		{
			led1 >>= 2;
			led3 >>= 2;
			led5 >>= 2;
		}
		LedBrightness(1, led1);
		LedBrightness(3, led3);
		LedBrightness(5, led5);



		// RF frequency is 220MHz * 4096 / (fqValue+1)
		// Main knob controls frequency over MW band, between 530 and 1600kHz approximately
		// Knob X adds fine tune
	   	fqValue = 563200 + (4095-mainKnobNR(KnobVal(Main)))*280 - KnobVal(X)*6 - CVIn1()*6;

		// If RF output is off, set duty cycle to 0.
		// Otherwise, duty cycle is (0.25 + the audio), ranging from 0 to 0.5 at most.
		// This sets the amplitude
		amValue = rfOn?((signal+2048)*(fqValue>>13)):0;
	}

	
private:
	uint32_t sampleIndex;
	WindowNR mainKnobNR;
	
	unsigned numFiles, currentFile;
	std::vector<WAVFile> wavfiles;

	unsigned startupTimer;
};


AMCoupler *amp;
void core1()
{
	amp->Run();
}

int main()
{
	// Mild overclock, required to fit even worse-case jitter of PWM IRQ into the 625ns available
	set_sys_clock_khz(220000, true);

	AMCoupler am;
	amp = &am;
	am.EnableNormalisationProbe();

	// Run ComputerCard audio on core 1
	multicore_launch_core1(core1);

	// This core just does RF IRQ
	while (1)
	{
		__wfi();
	}
}

