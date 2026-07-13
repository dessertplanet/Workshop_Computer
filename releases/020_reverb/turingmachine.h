#ifndef TURINGMACHINE_H
#define TURINGMACHINE_H


typedef struct
{
	uint32_t bits;
	uint8_t length;
} turing_machine;

uint32_t __not_in_flash_func(turing_machine_rand)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed;
}



void turing_machine_init(turing_machine *tm)
{
	tm->length = 8;
	tm->bits = 0x53;
}

uint32_t __not_in_flash_func(turing_machine_step)(turing_machine *tm, int32_t p)
{
	if (p < 10) p = 0;
	if (p > 4085) p = 4095;
	
	uint32_t prob = 4095 - p;
	prob <<= 20;
	if (prob == 0xFFF00000) prob = 0xFFFFFFFF;
	
	uint32_t chosenBit = (tm->bits) & (1 << ((tm->length) - 1)) ? 1 : 0;
	chosenBit = (turing_machine_rand() < prob) ? (1 - chosenBit) : chosenBit;
	tm->bits = ((tm->bits) << 1) | chosenBit;

	return chosenBit;
}

uint32_t __not_in_flash_func(turing_machine_volt)(turing_machine *tm)
{
	return (tm->bits) & 0xFF;
}

void __not_in_flash_func(turing_machine_set_length)(turing_machine *tm, uint8_t length)
{
	tm->length = length;
}


#endif
