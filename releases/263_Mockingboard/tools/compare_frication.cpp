// compare_frication.cpp — compare fricative level and spectrum across the
// Casso reference rate, the card rate, and the fixed-point port.
//
// Build/run (host):
//   g++ -std=c++20 -O2 -I. tools/compare_frication.cpp Ssi263.cpp SsiVoice.cpp -o /tmp/compare_frication
//   /tmp/compare_frication

#include "Ssi263.h"
#include "SsiVoice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

constexpr double kXckHz = 1022727.0;
constexpr uint8_t kFilter = 0xE6;
constexpr uint8_t kArticulation = 5;
constexpr uint8_t kAmplitude = 0x0C;
constexpr double kPitchHz = 112.0;

uint16_t InflectionForHz(double hz)
{
	double value = 4096.0 - kXckHz / (8.0 * hz);
	return static_cast<uint16_t>(std::clamp(value, 0.0, 4095.0) + 0.5);
}

template <class Chip>
void Configure(Chip &chip, uint32_t sampleRate, uint8_t phoneme)
{
	chip.SetSampleRate(sampleRate);
	chip.SetTickClock(sampleRate);
	uint16_t inflection = InflectionForHz(kPitchHz);
	chip.WriteRegister(Chip::kRegInflection, static_cast<uint8_t>(inflection >> 3));
	chip.WriteRegister(Chip::kRegRateInflection,
	                   static_cast<uint8_t>(((inflection & 0x800) ? 0x08 : 0) |
	                                        (inflection & 0x07)));
	chip.WriteRegister(Chip::kRegDurationPhoneme,
	                   static_cast<uint8_t>((Chip::kModePhonemeTransitioned << 6) | phoneme));
	chip.WriteRegister(Chip::kRegCtlArtAmp,
	                   static_cast<uint8_t>((kArticulation << 4) | kAmplitude));
	chip.WriteRegister(Chip::kRegFilterFreq, kFilter);
	chip.WriteRegister(Chip::kRegDurationPhoneme, phoneme);
}

double BandPower(const std::vector<float> &samples, uint32_t sampleRate,
                 double lowHz, double highHz)
{
	double power = 0.0;
	int bins = 0;
	for (double frequency = lowHz; frequency < highHz; frequency += 100.0)
	{
		double omega = 2.0 * std::numbers::pi * frequency / sampleRate;
		double coeff = 2.0 * std::cos(omega);
		double s1 = 0.0, s2 = 0.0;
		for (float sample : samples)
		{
			double current = sample + coeff * s1 - s2;
			s2 = s1;
			s1 = current;
		}
		power += s1 * s1 + s2 * s2 - coeff * s1 * s2;
		bins++;
	}
	return power / std::max(bins, 1);
}

void Report(const char *engine, uint8_t phoneme, uint32_t sampleRate,
            const std::vector<float> &samples)
{
	double sumSq = 0.0;
	for (float sample : samples)
		sumSq += static_cast<double>(sample) * sample;
	double rms = std::sqrt(sumSq / samples.size());
	double low = BandPower(samples, sampleRate, 500.0, 1000.0);
	double mid = BandPower(samples, sampleRate, 1000.0, 2000.0);
	double high = BandPower(samples, sampleRate, 2000.0, 4000.0);
	double air = BandPower(samples, sampleRate, 4000.0,
	                      std::min(8000.0, sampleRate * 0.45));
	double total = low + mid + high + air;
	auto pct = [total](double value) { return total > 0.0 ? 100.0 * value / total : 0.0; };
	std::printf("%-8s ph=%02X fs=%5u rms=%.6f bands%% 0.5-1k=%5.1f 1-2k=%5.1f 2-4k=%5.1f 4-8k=%5.1f\n",
	            engine, phoneme, sampleRate, rms, pct(low), pct(mid), pct(high), pct(air));
}

void RenderReference(uint8_t phoneme, uint32_t sampleRate)
{
	Ssi263 chip(kXckHz);
	Configure(chip, sampleRate, phoneme);
	for (uint32_t i = 0; i < sampleRate / 5; i++)
		chip.GenerateSample();
	std::vector<float> samples(sampleRate / 2);
	for (float &sample : samples)
		sample = chip.GenerateSample();
	Report("Casso", phoneme, sampleRate, samples);
}

void RenderPort(uint8_t phoneme, uint32_t sampleRate)
{
	SsiVoice chip(kXckHz);
	Configure(chip, sampleRate, phoneme);
	chip.SetVoicePitch(0, kPitchHz);
	for (uint32_t i = 0; i < sampleRate / 5; i++)
		chip.GenerateSample();
	std::vector<float> samples(sampleRate / 2);
	for (float &sample : samples)
		sample = static_cast<float>(chip.GenerateSample()) * (1.0f / 16777216.0f);
	Report("Port", phoneme, sampleRate, samples);
}

} // namespace

int main()
{
	for (uint8_t phoneme : {uint8_t{0x30}, uint8_t{0x2F}, uint8_t{0x33}})
	{
		RenderReference(phoneme, 44100);
		RenderReference(phoneme, 24000);
		RenderPort(phoneme, 24000);
		std::puts("");
	}
}
