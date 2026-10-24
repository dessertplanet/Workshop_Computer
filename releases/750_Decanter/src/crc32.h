// CRC-32 (IEEE 802.3, as zlib), bitwise: small, and fast enough for a few
// hundred kB once per upload or flash.
#pragma once

#include <stddef.h>
#include <stdint.h>

inline uint32_t Crc32Update(uint32_t crc, const uint8_t *data, size_t len)
{
	crc = ~crc;
	for (size_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (int b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

inline uint32_t Crc32(const uint8_t *data, size_t len)
{
	return Crc32Update(0, data, len);
}
