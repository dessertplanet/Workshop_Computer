/**
 * ulaw.h — G.711 u-law codec (header-only).
 *
 * 8-bit companded audio, ~2:1 compression of 16-bit PCM.
 */

#ifndef ULAW_H
#define ULAW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ULAW_BIAS 0x84
#define ULAW_CLIP 32635

static inline uint8_t ulaw_find_segment(uint16_t v)
{
	uint8_t seg = 0;
	for (uint16_t mask = 0x4000; (v & mask) == 0 && seg < 8; mask >>= 1)
		seg++;
	return (uint8_t)(7 - seg);
}

static inline uint8_t ulaw_encode(int16_t sample)
{
	uint8_t sign = 0;
	int32_t pcm = sample;
	if (pcm < 0) {
		sign = 0x80;
		pcm = -pcm;
		if (pcm > 32767) pcm = 32767;
	}
	if (pcm > ULAW_CLIP) pcm = ULAW_CLIP;
	pcm += ULAW_BIAS;

	uint8_t segment = ulaw_find_segment((uint16_t)pcm);
	uint8_t mantissa = (uint8_t)((pcm >> (segment + 3)) & 0x0F);
	return (uint8_t)(~(sign | (segment << 4) | mantissa));
}

static inline int16_t ulaw_decode(uint8_t code)
{
	code = (uint8_t)~code;
	int32_t mantissa = ((int32_t)(code & 0x0F) << 3) + ULAW_BIAS;
	mantissa <<= (code >> 4) & 0x07;
	return (int16_t)((code & 0x80) ? (ULAW_BIAS - mantissa) : (mantissa - ULAW_BIAS));
}

#ifdef __cplusplus
}
#endif

#endif /* ULAW_H */
