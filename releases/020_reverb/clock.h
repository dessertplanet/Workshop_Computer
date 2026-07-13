#ifndef CLOCK_H
#define CLOCK_H

////////////////////////////////////////
// Free-running clock, incremented 48kHz


typedef struct
{
	uint32_t count, increment;
} clock;

void clock_init(clock *c)
{
	c->count = 0;
	c->increment = 0;
}


uint32_t __not_in_flash_func(clock_get_incr_from_hz)(clock *c, float f)
{
	(void)c;
	return (uint32_t)(89478.48533f * f);
}

void __not_in_flash_func(clock_set_freq_hz)(clock *c, float f)
{
	// tick called at 48kHz
	// wraps at 2^32 = 4,294,967,296
	// increment is linear in Hz, with 89478.485333 per Hz
	c->increment = (uint32_t)(89478.48533f * f);
}

void __not_in_flash_func(clock_set_freq_incr)(clock *c, uint32_t incr)
{
	// tick called at 48kHz
	// wraps at 2^32 = 4,294,967,296
	// increment is linear in Hz, with 89478.485333 per Hz
	c->increment = incr;
}

// Call at 48kHz
// Returns true on both rising and falling edges
bool __not_in_flash_func(clock_tick)(clock *c)
{
	uint32_t nextCount = c->count + c->increment;

	// Internal clock pulse, both rising and falling edges
	bool edge = (c->count ^ nextCount) & 0x80000000;
	c->count = nextCount;
	return edge;
}

bool __not_in_flash_func(clock_state)(clock *c)
{
	return c->count & 0x80000000;
}


#endif
