#include <cstdint>

////////////////////////////////////////
// A very simple WAV file parsing class.
//
// Takes a WAV file mapped into memory (e.g. in flash)
// and gives an interface to the samples and basic format info
// Currently hard-coded to 16-bit mono PCM files

class WAVFile
{
private:

	// Read 16- and 32-bit little-endian values from a buffer
	// (Can't just cast the 32-bit values, as they are not
	//  always aligned to 4-byte boundary)
	static uint16_t GetU16(uint8_t *&ptr)
	{
		uint16_t a1=*ptr++;
		uint16_t a2=*ptr++;
		return (a2<<8)+a1;
	}

	static uint32_t GetU32(uint8_t *&ptr)
	{
		uint32_t a1=*ptr++;
		uint32_t a2=*ptr++;
		uint32_t a3=*ptr++;
		uint32_t a4=*ptr++;
		return (a4<<24)+(a3<<16)+(a2<<8)+a1;
	}

public:
	WAVFile()
	{
		dataptr = nullptr;
		numSamples = 0;
		sampleRate = 0;
		fileSize = 0;
	}

	// Get raw sample
	int16_t operator[](uint32_t index)
	{
		if (!dataptr || index >= numSamples)
		{
			return 0;
		}
		
		return dataptr[index];
	}

	// Get sample value at fractional position index/256, linearly interpolated.
	// Interpolation at endpoint assumes that sample is looping
	int16_t Interp8(uint32_t index)
	{
		if (!dataptr || index >= (numSamples<<8))
		{
			return 0;
		}

		int32_t r = index & 0xFF;
		index >>= 8;

		uint32_t nextIndex = index+1;
		if (nextIndex > numSamples) nextIndex -= numSamples;
		
		return (dataptr[index]*(256-r) + dataptr[nextIndex]*r)>>8;
	}

	uint32_t SampleRate() {return sampleRate;}
	uint32_t FileSize() {return fileSize;}
	uint32_t NumSamples() {return numSamples;}
	uint16_t NumChannels() {return numChannels;}
	
	int Load(uint8_t *startptr)
	{
		uint8_t *ptr = startptr;
		uint32_t riffMarker = GetU32(ptr);
		if (riffMarker != 0x46464952) return 1;
	
		fileSize = GetU32(ptr) + 8;
		
		uint32_t waveMarker = GetU32(ptr);
		if (waveMarker != 0x45564157) return 2;

		uint32_t fmtMarker = GetU32(ptr);
		if (fmtMarker != 0x20746d66) return 3;

		uint32_t subChunkSize = GetU32(ptr);

		uint16_t audioFormat = GetU16(ptr);
		if (audioFormat != 1) return 4; // must be PCM format

		numChannels = GetU16(ptr);
		if (numChannels != 1) return 5; // must be mono

		sampleRate = GetU32(ptr);

		ptr += 6; // skip 'byte rate' and 'block align' fields
	
		uint16_t bitsPerSample  = GetU16(ptr);
		if (bitsPerSample != 16) return 6; // must be 16-bit

		// skip any 'extra params' which may or may not be in the format chunk
		if (subChunkSize>16) {ptr += subChunkSize-16;}
	
		uint32_t dataMarker = GetU32(ptr);
		if (dataMarker != 0x61746164) return 7;

		numSamples = GetU32(ptr) / numChannels / (bitsPerSample/8);

		
		// WAV file format specifies that all data is an even number
		// of bytes long, so 16-bit samples are aligned, so we can just cast here
		// (WAV and Arm Cortex M0+ both little endian too)
		dataptr = (int16_t*)ptr;

		return 0;
	}
	
	int16_t *dataptr;
	uint32_t numSamples;
	uint16_t numChannels;
	uint32_t sampleRate;
	uint32_t fileSize;
};



