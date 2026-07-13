#ifndef BERNOULLIGATE_H
#define BERNOULLIGATE_H


typedef struct
{
	bool toggle;
	bool awi;
	bool state;
} bernoulli_gate;

// Discretised to 12-bit
// Max value 4095*2^20 - 1
uint32_t __not_in_flash_func(bernoulli_gate_rand)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed & 0xFFF00000;
}



void bernoulli_gate_init(bernoulli_gate *bg)
{
	bg->toggle = 0;
	bg->awi = 0;
	bg->state = 0;
}


void __not_in_flash_func(bernoulli_gate_set_toggle)(bernoulli_gate *bg, bool toggle)
{
	bg->toggle = toggle;
}

void __not_in_flash_func(bernoulli_gate_set_and_with_input)(bernoulli_gate *bg, bool awi)
{
	bg->awi = awi;
}


bool __not_in_flash_func(bernoulli_gate_step)(bernoulli_gate *bg, int32_t p, bool risingEdge)
{
	if (!risingEdge) return bg->state;
	
	uint32_t rand = bernoulli_gate_rand();
	if (p < 0) p = 0;
	if (p > 4095) p = 4095;
	uint32_t up = p;
	up <<= 20; // 12-bit -> 32-bit conversion

	if (rand <= up)
	{
		if (bg->toggle)
		{
			bg->state = 1 - bg->state;
		}
		else
		{
			bg->state = 1;
		}
	}
	else
	{
		if (bg->toggle)
		{
		}
		else
		{
			bg->state = 0;
		}
	}

	return bg->state;
}



#endif
