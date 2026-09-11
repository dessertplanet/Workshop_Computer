// compare_ref.cpp — validate the SsiVoice port against the float Ssi263 oracle.
//
// Drives both engines with an identical scripted phoneme program and identical
// glottal pitch (the port runs a single voice at the reference's inflection
// frequency), rendering both sample-for-sample. Reports peak/RMS error so the
// vocoder restructure + LUTs can be checked before quantization or hardware.
//
// Build/run (host):
//   g++ -std=c++20 -O2 -I. tools/compare_ref.cpp Ssi263.cpp SsiVoice.cpp -o /tmp/compare_ref
//   /tmp/compare_ref

#include "Ssi263.h"
#include "SsiVoice.h"

#include <cstdint>
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace {

constexpr uint32_t kSampleRate = 48000;

uint16_t InflectionForHz(double hz, double xck)
{
	double i = 4096.0 - xck / (8.0 * hz);
	return static_cast<uint16_t>(std::clamp(i, 0.0, 4095.0) + 0.5);
}

template <class Chip>
void SetInflection(Chip &chip, uint16_t inflection, uint8_t rate)
{
	uint8_t reg1 = static_cast<uint8_t>((inflection >> 3) & 0xFF);
	uint8_t i11  = static_cast<uint8_t>((inflection & 0x800) ? 0x08 : 0);
	uint8_t ilow = static_cast<uint8_t>(inflection & 0x07);
	uint8_t reg2 = static_cast<uint8_t>((rate << 4) | i11 | ilow);
	chip.WriteRegister(Chip::kRegInflection, reg1);
	chip.WriteRegister(Chip::kRegRateInflection, reg2);
}

const uint8_t kScript[] = {
	0x01, 0x08, 0x0E, 0x11, 0x16, 0x1C, 0x20, 0x00, 0x01,
		0x30, 0x2F, 0x08, // add S, Z, A to exercise the fricative branch
};
constexpr int kNSeg = static_cast<int>(sizeof(kScript));

} // namespace

int main()
{
	const double xck = Ssi263::kDefaultXckHz;
	const uint8_t rate = 0, artic = 4;
	const uint8_t durBits = Ssi263::kModePhonemeImmediate;

	Ssi263 ref;
	ref.SetSampleRate(kSampleRate);
	ref.SetTickClock(kSampleRate);

	SsiVoice port;
	port.SetSampleRate(kSampleRate);
	port.SetTickClock(kSampleRate);

	SetInflection(ref, InflectionForHz(120.0, xck), rate);
	SetInflection(port, InflectionForHz(120.0, xck), rate);

	// Port: single glottal voice at the reference's clamped inflection pitch.
	double pitch = std::clamp(ref.GetInflectionFrequencyHz(), 30.0, 400.0);
	port.SetVoicePitch(0, pitch);

	auto start = [&](auto &chip) {
		chip.WriteRegister(0, static_cast<uint8_t>((durBits << 6) | kScript[0]));
		chip.WriteRegister(3, static_cast<uint8_t>((artic << 4) | 0x0F));
	};
	start(ref);
	start(port);

	int segR = 1, segP = 1;
	int guard = kSampleRate * 6;
	double peakErr = 0.0, sumSqErr = 0.0, sumSqRef = 0.0;
	double peakRef = 0.0, peakPort = 0.0;
	long n = 0;

	while (guard-- > 0)
	{
		float a = ref.GenerateSample();
		ref.Tick(1);
				float b = port.GenerateSample();
		port.Tick(1);

		double e = double(a) - double(b);
		if (std::fabs(e) > peakErr) peakErr = std::fabs(e);
		sumSqErr += e * e;
		sumSqRef += double(a) * double(a);
		if (std::fabs(a) > peakRef) peakRef = std::fabs(a);
		if (std::fabs(b) > peakPort) peakPort = std::fabs(b);
		n++;

		bool rq = ref.IsRequesting();
		if (rq)
		{
			if (segR < kNSeg)
				ref.WriteRegister(0, static_cast<uint8_t>((durBits << 6) | kScript[segR++]));
			else
				break;
		}
		if (port.IsRequesting())
		{
			if (segP < kNSeg)
				port.WriteRegister(0, static_cast<uint8_t>((durBits << 6) | kScript[segP++]));
		}
	}

	double rmsErr = std::sqrt(sumSqErr / std::max(1L, n));
	double rmsRef = std::sqrt(sumSqRef / std::max(1L, n));
	double relPct = (rmsRef > 0) ? 100.0 * rmsErr / rmsRef : 0.0;

	std::printf("samples=%ld  segR=%d segP=%d\n", n, segR, segP);
	std::printf("peakRef=%.4f peakPort=%.4f\n", peakRef, peakPort);
	std::printf("peakErr=%.5f  rmsErr=%.6f  rmsRef=%.6f  relErr=%.3f%%\n",
	            peakErr, rmsErr, rmsRef, relPct);
	return (relPct < 2.0) ? 0 : 1;
}
