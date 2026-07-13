#ifndef NOISE_GATE_H
#define NOISE_GATE_H

typedef struct
{
	uint32_t count;
	int32_t lpf_value;
} noise_gate;

void noise_gate_init(noise_gate *ng)
{
	ng->count = 0;
	ng->lpf_value = 0;
}

int32_t __not_in_flash_func(noise_gate_tick)(noise_gate *ng, int32_t in)
{

	// Noise gate.
	//  If input < threshold for long enough, then turn off input
	//  If input > thresholdh, turn input back on

	// ADC is 12-bit, 4096 values, from -2048 to 2047
	// Here, "in" is set to (left+right)<<2, giving "in" of ±16384
	// We pass in<<6 into high pass filter, giving ±1048576 in the filter
	// This is multiplied by 100, giving ±104,857,600, within 28 bits.

	// Output returns 11Hz highpassed version of input, to avoid 'thumps' when
	// the gate switches on and off.
	int32_t threshold = 9;
	int32_t thresholdh = 17;
	uint32_t countMax = 5000;


	// 11hz HPF to remove DC offset
	in <<= 6;
	ng->lpf_value += ((in - ng->lpf_value) * 100) >> 16;
	int32_t hpf = in - ng->lpf_value;

	// debug("%d %d %d\n",in, hpf>>6, ng->lpf_value>>6);

	int32_t ahpf = hpf >> 9; // bring back down by 2^9, to same magnitude as ADC left/right
	if (ahpf < 0) ahpf = -ahpf;

	if (ahpf < threshold)
	{
		ng->count++;
	}
	if (ahpf > thresholdh)
	{
		ng->count = 0;
	}

	if (ng->count > countMax)
		return 0; // return zero if signal should be silenced
	else
		return hpf >> 6; // return highpassed signal otherwise
}



#endif
