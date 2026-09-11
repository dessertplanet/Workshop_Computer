// render_ref.cpp — host-only reference renderer for the float SSI-263 engine.
//
// This is the numerical oracle for the fixed-point port: it drives the pristine
// Ssi263 float engine through a scripted phoneme program exactly as the card's
// MIDI layer will (register writes + Tick + A/R pacing) and writes a 16-bit
// mono WAV plus summary stats. The fixed-point engine is validated by rendering
// the same script and comparing against this output.
//
// Build/run (host, not the RP2040):
//   g++ -std=c++20 -O2 -I.. tools/render_ref.cpp ../Ssi263.cpp -o /tmp/render_ref
//   /tmp/render_ref /tmp/ref.wav

#include "Ssi263.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

namespace {

constexpr uint32_t kSampleRate = 48000;

// Inflection register value I (12-bit) for a target glottal frequency, given
// XCK. Datasheet: freq = XCK / (8 * (4096 - I)).
uint16_t InflectionForHz(double hz, double xck)
{
	double i = 4096.0 - xck / (8.0 * hz);
	if (i < 0) i = 0;
	if (i > 4095) i = 4095;
	return static_cast<uint16_t>(i + 0.5);
}

// Split a 12-bit inflection value across registers 1 and 2, OR the rate nibble
// R into register 2's high bits.
void SetInflection(Ssi263 &chip, uint16_t inflection, uint8_t rate)
{
	uint8_t reg1 = static_cast<uint8_t>((inflection >> 3) & 0xFF);         // I10-I3
	uint8_t i11  = static_cast<uint8_t>((inflection & 0x800) ? 0x08 : 0);  // I11 -> bit3
	uint8_t ilow = static_cast<uint8_t>(inflection & 0x07);               // I2-I0
	uint8_t reg2 = static_cast<uint8_t>((rate << 4) | i11 | ilow);
	chip.WriteRegister(Ssi263::kRegInflection, reg1);
	chip.WriteRegister(Ssi263::kRegRateInflection, reg2);
}

void WriteWavMono16(const char *path, const std::vector<int16_t> &pcm, uint32_t rate)
{
	FILE *f = std::fopen(path, "wb");
	if (!f) { std::perror("fopen"); return; }

	uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
	uint32_t riff = 36 + dataBytes;
	uint16_t chans = 1, bits = 16;
	uint32_t byteRate = rate * chans * (bits / 8);
	uint16_t blockAlign = static_cast<uint16_t>(chans * (bits / 8));
	uint32_t fmtLen = 16;
	uint16_t pcmFmt = 1;

	std::fwrite("RIFF", 1, 4, f);
	std::fwrite(&riff, 4, 1, f);
	std::fwrite("WAVE", 1, 4, f);
	std::fwrite("fmt ", 1, 4, f);
	std::fwrite(&fmtLen, 4, 1, f);
	std::fwrite(&pcmFmt, 2, 1, f);
	std::fwrite(&chans, 2, 1, f);
	std::fwrite(&rate, 4, 1, f);
	std::fwrite(&byteRate, 4, 1, f);
	std::fwrite(&blockAlign, 2, 1, f);
	std::fwrite(&bits, 2, 1, f);
	std::fwrite("data", 1, 4, f);
	std::fwrite(&dataBytes, 4, 1, f);
	std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
	std::fclose(f);
}

} // namespace

int main(int argc, char **argv)
{
	const char *outPath = (argc > 1) ? argv[1] : "/tmp/ref.wav";

	Ssi263 chip;
	chip.SetSampleRate(kSampleRate);
	chip.SetTickClock(static_cast<double>(kSampleRate)); // 1 tick == 1 sample

	const double xck = Ssi263::kDefaultXckHz;
	const uint8_t rate = 0;       // R=0: longest frames
	const uint8_t artic = 4;      // mid articulation

	// A short scripted utterance: a run of voiced phonemes from the ROM table,
	// with pauses between, all at a fixed ~120 Hz glottal pitch.
	const uint8_t script[] = {
		0x01, // E   meet
		0x08, // A   made
		0x0E, // AH  got
		0x11, // O   store
		0x16, // U   tune
		0x1C, // ER  bird
		0x20, // L   lift
		0x00, // PA  pause
		0x01, // E
	};
	const int nSeg = static_cast<int>(sizeof(script));

	SetInflection(chip, InflectionForHz(120.0, xck), rate);

	// reg0: DUR bits select mode at the CTL 1->0 transition; use mode 2
	// (phoneme immediate, A/R active). Load the first phoneme.
	const uint8_t durBits = Ssi263::kModePhonemeImmediate; // 2
	chip.WriteRegister(Ssi263::kRegDurationPhoneme,
	                   static_cast<uint8_t>((durBits << 6) | script[0]));

	// reg3: CTL low, articulation, full amplitude -> latches mode + starts phoneme.
	uint8_t reg3 = static_cast<uint8_t>((artic << Ssi263::kArticShift) | 0x0F);
	chip.WriteRegister(Ssi263::kRegCtlArtAmp, reg3);

	std::vector<int16_t> pcm;
	pcm.reserve(kSampleRate * 4);

	int seg = 1;                     // next phoneme index to feed on A/R
	int guardSamples = kSampleRate * 5; // safety cap
	double peak = 0.0, sumSq = 0.0;
	long nonZero = 0;

	while (guardSamples-- > 0)
	{
		float s = static_cast<float>(chip.GenerateSample()) * (1.0f / 16777216.0f);
		chip.Tick(1);

		double v = s;
		if (std::fabs(v) > peak) peak = std::fabs(v);
		sumSq += v * v;
		if (std::fabs(v) > 1e-4) nonZero++;

		int iv = static_cast<int>(std::lround(v * 32767.0));
		if (iv > 32767) iv = 32767;
		if (iv < -32768) iv = -32768;
		pcm.push_back(static_cast<int16_t>(iv));

		// Advance the script when the chip requests the next phoneme.
		if (chip.IsRequesting())
		{
			if (seg < nSeg)
			{
				chip.WriteRegister(Ssi263::kRegDurationPhoneme,
				                   static_cast<uint8_t>((durBits << 6) | script[seg]));
				seg++;
			}
			else
			{
				break; // script exhausted
			}
		}
	}

	// Render a short release tail so the final envelope decay is captured.
	for (int i = 0; i < static_cast<int>(kSampleRate * 0.2); i++)
	{
		float s = static_cast<float>(chip.GenerateSample()) * (1.0f / 16777216.0f);
		chip.Tick(1);
		int iv = static_cast<int>(std::lround(s * 32767.0));
		if (iv > 32767) iv = 32767;
		if (iv < -32768) iv = -32768;
		pcm.push_back(static_cast<int16_t>(iv));
	}

	WriteWavMono16(outPath, pcm, kSampleRate);

	double rms = pcm.empty() ? 0.0 : std::sqrt(sumSq / pcm.size());
	std::printf("wrote %s\n", outPath);
	std::printf("samples=%zu  dur=%.2fs  peak=%.4f  rms=%.4f  nonzero=%ld (%.1f%%)\n",
	            pcm.size(), pcm.size() / double(kSampleRate), peak, rms,
	            nonZero, 100.0 * nonZero / std::max<size_t>(1, pcm.size()));
	return 0;
}
