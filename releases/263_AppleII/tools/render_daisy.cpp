// render_daisy.cpp — host pre-flight for the Daisy Bell demo. Drives SsiVoice
// with the same daisy_demo.h score and re-trigger logic as the card, writes a
// WAV, and prints stats. Lets us hear/verify the demo before flashing.
//
//   g++ -std=c++20 -O2 -I. tools/render_daisy.cpp SsiVoice.cpp -o /tmp/render_daisy
//   /tmp/render_daisy /tmp/daisy.wav

#include "SsiVoice.h"
#include "daisy_demo.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace {
constexpr uint32_t kSampleRate = 24000; // matches the card's 24 kHz synthesis
constexpr uint8_t kModeBits = SsiVoice::kModePhonemeTransitioned;

float MidiToHz(uint8_t note) { return 440.0f * std::pow(2.0f, (int(note) - 69) / 12.0f); }

void WriteWav(const char *path, const std::vector<int16_t> &pcm, uint32_t rate)
{
	FILE *f = std::fopen(path, "wb");
	if (!f) return;
	uint32_t dataBytes = pcm.size() * 2, riff = 36 + dataBytes, byteRate = rate * 2;
	uint16_t chans = 1, bits = 16, blockAlign = 2, pcmFmt = 1;
	uint32_t fmtLen = 16;
	std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVE", 1, 4, f);
	std::fwrite("fmt ", 1, 4, f); std::fwrite(&fmtLen, 4, 1, f); std::fwrite(&pcmFmt, 2, 1, f);
	std::fwrite(&chans, 2, 1, f); std::fwrite(&rate, 4, 1, f); std::fwrite(&byteRate, 4, 1, f);
	std::fwrite(&blockAlign, 2, 1, f); std::fwrite(&bits, 2, 1, f);
	std::fwrite("data", 1, 4, f); std::fwrite(&dataBytes, 4, 1, f);
	std::fwrite(pcm.data(), 2, pcm.size(), f); std::fclose(f);
}
} // namespace

int main(int argc, char **argv)
{
	const char *out = (argc > 1) ? argv[1] : "/tmp/daisy.wav";
	int voiceCount = (argc > 2) ? std::atoi(argv[2]) : kDemoVoiceCount;
	voiceCount = std::clamp(voiceCount, 0, kDemoVoiceCount);

	SsiVoice eng(kDaisyXckHz);
	eng.SetSampleRate(kSampleRate);
	eng.SetTickClock(kSampleRate);
	eng.WriteRegister(SsiVoice::kRegDurationPhoneme, (kModeBits << 6) | 0x00);
	eng.WriteRegister(SsiVoice::kRegCtlArtAmp,
	                  (kDaisyArticulation << 4) | kDaisyAmplitude);
	eng.WriteRegister(SsiVoice::kRegFilterFreq, kDaisyFilter);

	std::vector<int16_t> pcm;
	int segIndex = 0, segLeft = 0;
	uint8_t curReg0 = 0;
	double peak = 0, sumSq = 0;
	uint32_t clippedSamples = 0;
	double segmentPeak[kDaisyLen] = {};
	double segmentMaxStep[kDaisyLen] = {};
	double segmentBoundaryStep[kDaisyLen] = {};
	double segmentAttackMaxStep[kDaisyLen] = {};
	uint32_t segmentClipped[kDaisyLen] = {};
	float previousSample = 0.0f;

	for (int loop = 0; loop < 2; loop++)          // two passes through the score
	{
		for (int i = 0; i < kDaisyLen; i++)
		{
			int currentSegment = segIndex;
			const DaisySeg &seg = kDaisy[segIndex];
			segIndex = (segIndex + 1) % kDaisyLen;
			for (int voice = 0; voice < voiceCount; voice++)
				eng.SetVoicePitch(voice, MidiToHz(static_cast<uint8_t>(
					seg.note + kDemoChordSemitones[voice])));
			curReg0 = seg.phoneme;
			eng.WriteRegister(SsiVoice::kRegDurationPhoneme, curReg0);
			segLeft = int(seg.units) * kDaisyUnitSamples;

			int sampleInSegment = 0;
			while (segLeft-- > 0)
			{
				if (eng.IsRequesting())
					eng.WriteRegister(SsiVoice::kRegDurationPhoneme, curReg0);
				int32_t sampleQ = eng.GenerateSample();
				float s = static_cast<float>(sampleQ) * (1.0f / 16777216.0f);
				eng.Tick(1);
				double v = s;
				if (std::fabs(v) > peak) peak = std::fabs(v);
				segmentPeak[currentSegment] = std::max(segmentPeak[currentSegment], std::fabs(v));
				double step = std::fabs(v - previousSample);
				segmentMaxStep[currentSegment] = std::max(segmentMaxStep[currentSegment], step);
				if (sampleInSegment == 0)
					segmentBoundaryStep[currentSegment] = std::max(
						segmentBoundaryStep[currentSegment], step);
				if (sampleInSegment < static_cast<int>(kSampleRate / 200))
					segmentAttackMaxStep[currentSegment] = std::max(
						segmentAttackMaxStep[currentSegment], step);
				previousSample = s;
				sampleInSegment++;
				if (std::fabs(v) >= 1.0f)
				{
					clippedSamples++;
					segmentClipped[currentSegment]++;
				}
				sumSq += v * v;
				int iv = std::lround(std::clamp(s * 2000.0f, -2047.0f, 2047.0f));
				pcm.push_back(static_cast<int16_t>(iv * 8)); // *8 -> ~full-scale WAV
			}
		}
	}

	WriteWav(out, pcm, kSampleRate);
	double rms = pcm.empty() ? 0 : std::sqrt(sumSq / pcm.size());
	std::printf("wrote %s : voices=%d, %zu samples (%.2fs), engine peak=%.3f rms=%.4f clipped=%u (%.4f%%)\n",
	            out, voiceCount, pcm.size(), pcm.size() / double(kSampleRate), peak, rms,
	            clippedSamples, 100.0 * clippedSamples / pcm.size());
	for (int i = 0; i < kDaisyLen; i++)
		std::printf("  %2d ph=%02X note=%u units=%u peak=%.3f boundary=%.4f attack=%.4f step=%.4f clipped=%u\n",
		            i, kDaisy[i].phoneme, kDaisy[i].note, kDaisy[i].units,
		            segmentPeak[i], segmentBoundaryStep[i], segmentAttackMaxStep[i],
		            segmentMaxStep[i], segmentClipped[i]);
	return 0;
}
