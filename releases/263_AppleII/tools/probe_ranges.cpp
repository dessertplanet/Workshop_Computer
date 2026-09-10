// probe_ranges.cpp — measure worst-case internal signal ranges in SsiVoice,
// to choose fixed-point Q-formats with known headroom. Drives a full 8-voice
// chord (max excitation sum) through the same script the oracle uses.
//
// Build/run (host): compile SsiVoice.cpp with -DSSIVOICE_PROBE.
//   g++ -std=c++20 -O2 -I. -DSSIVOICE_PROBE tools/probe_ranges.cpp SsiVoice.cpp -o /tmp/probe
//   /tmp/probe

#include "SsiVoice.h"
#include "tools/ssi_probe.h"

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>

double g_ssiProbeMax[kSsiProbeCount] = {};

namespace {
constexpr uint32_t kSampleRate = 48000;

uint16_t InflectionForHz(double hz, double xck)
{
	double i = 4096.0 - xck / (8.0 * hz);
	return static_cast<uint16_t>(std::clamp(i, 0.0, 4095.0) + 0.5);
}
void SetInflection(SsiVoice &chip, uint16_t inflection, uint8_t rate)
{
	uint8_t reg1 = static_cast<uint8_t>((inflection >> 3) & 0xFF);
	uint8_t i11  = static_cast<uint8_t>((inflection & 0x800) ? 0x08 : 0);
	uint8_t ilow = static_cast<uint8_t>(inflection & 0x07);
	chip.WriteRegister(SsiVoice::kRegInflection, reg1);
	chip.WriteRegister(SsiVoice::kRegRateInflection, static_cast<uint8_t>((rate << 4) | i11 | ilow));
}
const uint8_t kScript[] = { 0x01,0x08,0x0E,0x11,0x16,0x1C,0x20,0x00,0x01,0x30,0x2F,0x08 };
constexpr int kNSeg = static_cast<int>(sizeof(kScript));
const char *kName[kSsiProbeCount] = {
	"impulseSum","excLp2","exc","res0_y","res1_y","res2_y","fric_y","fricLp3",
	"post_diff","final","a0","b","-","-","-","-"
};
} // namespace

int main()
{
	const double xck = SsiVoice::kDefaultXckHz;
	const uint8_t rate = 0, artic = 4, durBits = SsiVoice::kModePhonemeImmediate;

	SsiVoice chip;
	chip.SetSampleRate(kSampleRate);
	chip.SetTickClock(kSampleRate);
	SetInflection(chip, InflectionForHz(120.0, xck), rate);

	// Worst case: a full chord of detuned voices across the musical range.
	const double pitches[SsiVoice::kMaxVoices] = { 55, 82.4, 110, 146.8, 196, 261.6, 329.6, 392 };
	for (int v = 0; v < SsiVoice::kMaxVoices; v++)
		chip.SetVoicePitch(v, pitches[v]);

	chip.WriteRegister(0, static_cast<uint8_t>((durBits << 6) | kScript[0]));
	chip.WriteRegister(3, static_cast<uint8_t>((artic << 4) | 0x0F));

	int seg = 1, guard = kSampleRate * 6;
	while (guard-- > 0)
	{
		chip.GenerateSample();
		chip.Tick(1);
		if (chip.IsRequesting())
		{
			if (seg < kNSeg)
				chip.WriteRegister(0, static_cast<uint8_t>((durBits << 6) | kScript[seg++]));
			else
				break;
		}
	}

	std::printf("Worst-case |signal| over 8-voice chord:\n");
	for (int i = 0; i < kSsiProbeCount; i++)
	{
		if (kName[i][0] == '-') continue;
		double m = g_ssiProbeMax[i];
		int intBits = (m > 0) ? (int)std::ceil(std::log2(m + 1e-9)) : 0;
		std::printf("  %-10s max=%10.4f  (needs %d integer bits + sign)\n", kName[i], m, intBits < 0 ? 0 : intBits);
	}
	return 0;
}
