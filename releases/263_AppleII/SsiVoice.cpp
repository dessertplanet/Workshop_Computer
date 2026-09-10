// SsiVoice.cpp — see SsiVoice.h. Register/timing model mirrors the reference
// Ssi263; the synthesis is the vocoder restructure with LUTs (no per-sample
// transcendentals).

#include "SsiVoice.h"

#include <cmath>
#include <numbers>
#include <algorithm>

namespace {

// Synthesis constants (identical to the reference engine).
constexpr float  kVoicedGain   = 966.00f;
constexpr float  kNoiseGain    = 0.165f;
constexpr float  kOutputGain   = 5.00f;
constexpr float  kNoiseLpCoef  = 0.07f;
constexpr float  kFricLpCoef   = 0.28f;
constexpr float  kOutputLpCoef = 0.32f;
constexpr double kSourceBreakHz = 50.0;
constexpr double kBandwidthHz[3] = { 60.0, 90.0, 120.0 };
constexpr double kFricBandwidthHz = 450.0;
constexpr double kLevelTauSec   = 0.0025;
constexpr double kAttackTauSec  = 0.002;
constexpr double kReleaseTauSec = 0.004;

float OnePoleCoef(double tauSec, double fs)
{
	return static_cast<float>(1.0 - std::exp(-1.0 / (tauSec * fs)));
}

} // namespace

SsiVoice::SsiVoice(double xckHz)
{
	m_xckHz = (xckHz > 0.0) ? xckHz : kDefaultXckHz;
	Reset();
	RecomputeScale();
}

void SsiVoice::SetSampleRate(uint32_t sampleRate)
{
	m_sampleRate = sampleRate;
	if (m_sampleRate == 0)
		return;

	BuildTables();

	// Voice increments depend on the sample rate.
	for (auto &v : m_voices)
		if (v.active)
			v.inc = v.hz / static_cast<double>(m_sampleRate);
}

void SsiVoice::BuildTables()
{
	double fs = static_cast<double>(m_sampleRate);

	m_sourcePole = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * kSourceBreakHz / fs));

	for (int a = 0; a < 8; a++)
		m_articCoef[a] = OnePoleCoef((8.0 - a) * 0.010, fs);

	m_levelCoef   = OnePoleCoef(kLevelTauSec, fs);
	m_attackCoef  = OnePoleCoef(kAttackTauSec, fs);
	m_releaseCoef = OnePoleCoef(kReleaseTauSec, fs);

	for (int s = 0; s < 3; s++)
	{
		double r = std::exp(-std::numbers::pi * kBandwidthHz[s] / fs);
		m_r[s]    = static_cast<float>(r);
		m_rr[s]   = static_cast<float>(-(r * r));
		m_twoR[s] = static_cast<float>(2.0 * r);
	}

	double fr = std::exp(-std::numbers::pi * kFricBandwidthHz / fs);
	m_fricR    = static_cast<float>(fr);
	m_fricRR   = static_cast<float>(-(fr * fr));
	m_fricTwoR = static_cast<float>(2.0 * fr);

	m_radScale = static_cast<float>(fs / 44100.0);

	// cosLut_[i] = cos(pi * i / N); a hz maps to index round(2*fc/fs * N).
	for (int i = 0; i <= kCosLutSize; i++)
		m_cosLut[i] = static_cast<float>(std::cos(std::numbers::pi * i / kCosLutSize));
}

float SsiVoice::CosForHz(double hz) const
{
	double x = 2.0 * hz / static_cast<double>(m_sampleRate); // in [0,1)
	double f = x * kCosLutSize;
	int idx = static_cast<int>(f);
	if (idx < 0) { idx = 0; f = 0.0; }
	if (idx >= kCosLutSize) { idx = kCosLutSize - 1; f = idx + 1.0; }
	float frac = static_cast<float>(f - idx);
	// Linear interpolation between adjacent LUT entries: keeps the high-Q
	// resonators from amplifying quantization error into audible detuning.
	return m_cosLut[idx] + frac * (m_cosLut[idx + 1] - m_cosLut[idx]);
}

void SsiVoice::RecomputeScale()
{
	double divisor = 2.0 * (256.0 - static_cast<double>(m_reg[kRegFilterFreq]));
	double ff = (divisor > 0.0) ? (m_xckHz / divisor) : 0.0;
	double s = ff / kNominalFilterHz;
	m_scale = std::clamp(s, 0.5, 2.0);
}

void SsiVoice::SetXckClock(double xckHz)
{
	double previousXck  = m_xckHz;
	double previousTick = GetTickClockHz();
	if (xckHz > 0.0)
	{
		m_xckHz = xckHz;
		m_phonemeCycles *= (previousXck / m_xckHz) * (GetTickClockHz() / previousTick);
		RecomputeScale();
	}
}

void SsiVoice::SetTickClock(double tickClockHz)
{
	double previous = GetTickClockHz();
	if (tickClockHz > 0.0)
	{
		m_tickClockHz = tickClockHz;
		m_phonemeCycles *= m_tickClockHz / previous;
	}
}

// ---- Vocoder voices --------------------------------------------------------

void SsiVoice::SetVoicePitch(int i, double hz)
{
	if (i < 0 || i >= kMaxVoices)
		return;
	if (hz <= 0.0)
	{
		m_voices[i].active = false;
		return;
	}
	m_voices[i].active = true;
	m_voices[i].hz = hz;
	if (m_sampleRate > 0)
		m_voices[i].inc = hz / static_cast<double>(m_sampleRate);
}

void SsiVoice::SetVoiceActive(int i, bool on)
{
	if (i < 0 || i >= kMaxVoices)
		return;
	m_voices[i].active = on;
}

int SsiVoice::ActiveVoiceCount() const
{
	int n = 0;
	for (const auto &v : m_voices)
		if (v.active)
			n++;
	return n;
}

// ---- Register / timing (mirrors Ssi263) ------------------------------------

uint8_t SsiVoice::SelectRegister(uint8_t address)
{
	uint8_t sel = static_cast<uint8_t>(address & kAddressMask);
	return (sel >= kRegFilterFreq) ? kRegFilterFreq : sel;
}

void SsiVoice::WriteRegister(uint8_t reg, uint8_t value)
{
	uint8_t sel = SelectRegister(reg);
	bool wasDown = IsPoweredDown();
	m_reg[sel] = value;

	if (sel == kRegCtlArtAmp)
	{
		if (wasDown && !IsPoweredDown())
			LatchMode();
		else if (!wasDown && IsPoweredDown())
		{
			m_sounding = false;
			m_phonemeCycles = 0.0;
			m_request = false;
		}
	}
	else if (sel == kRegDurationPhoneme && !IsPoweredDown())
	{
		BeginPhoneme();
	}
	else if (sel == kRegFilterFreq)
	{
		RecomputeScale();
	}
}

void SsiVoice::Reset()
{
	for (int i = 0; i < kRegCount; i++)
		m_reg[i] = 0;
	m_reg[kRegCtlArtAmp] = kCtl;

	m_mode = kModeArDisabled;
	m_request = false;
	m_phonemeCycles = 0.0;
	m_sounding = false;

	for (int i = 0; i < 3; i++)
	{
		m_fCur[i] = 0.0;
		m_resY1[i] = 0.0f;
		m_resY2[i] = 0.0f;
	}
	m_envLevel = 0.0f;
	m_radPrev = 0.0f;
	m_outLp = m_outLp2 = 0.0f;
	m_excLp1 = m_excLp2 = 0.0f;
	m_noiseLp = 0.0f;
	m_vaCur = m_faCur = 0.0f;
	m_fricLp = m_fricLp2 = m_fricLp3 = 0.0f;
	m_fricY1 = m_fricY2 = 0.0f;
	m_lfsr = 0xACE1u;
}

void SsiVoice::Tick(uint32_t cycles)
{
	if (!m_sounding || IsPoweredDown())
		return;

	m_phonemeCycles -= static_cast<double>(cycles);
	if (m_phonemeCycles <= 0.0)
	{
		m_phonemeCycles = 0.0;
		m_sounding = false;
		if (m_mode != kModeArDisabled)
			m_request = true;
	}
}

bool SsiVoice::IsSilent() const
{
	bool quiet = IsPoweredDown() || (GetAmplitude() == 0) || !m_sounding;
	return quiet && (m_envLevel < 0.001f);
}

uint16_t SsiVoice::GetInflectionValue() const
{
	uint16_t high = static_cast<uint16_t>((m_reg[kRegRateInflection] & kInflect11) != 0 ? 0x800 : 0);
	uint16_t mid  = static_cast<uint16_t>(static_cast<uint16_t>(m_reg[kRegInflection]) << 3);
	uint16_t low  = static_cast<uint16_t>(m_reg[kRegRateInflection] & kInflectLowMask);
	return static_cast<uint16_t>(high | mid | low);
}

double SsiVoice::GetFrameDurationSec() const
{
	return (4096.0 * (16.0 - static_cast<double>(GetRateSel()))) / m_xckHz;
}

double SsiVoice::GetPhonemeDurationSec() const
{
	double frame = GetFrameDurationSec();
	if (m_mode == kModeFrameImmediate)
		return frame;
	return frame * (4.0 - static_cast<double>(GetDurationSel()));
}

double SsiVoice::GetFilterFrequencyHz() const
{
	double divisor = 2.0 * (256.0 - static_cast<double>(m_reg[kRegFilterFreq]));
	return (divisor > 0.0) ? (m_xckHz / divisor) : 0.0;
}

double SsiVoice::GetInflectionFrequencyHz() const
{
	double divisor = 8.0 * (4096.0 - static_cast<double>(GetInflectionValue()));
	return (divisor > 0.0) ? (m_xckHz / divisor) : 0.0;
}

const PhonemeSpec &SsiVoice::GetActiveSpec() const
{
	return kPhonemeRom[GetPhoneme()];
}

bool SsiVoice::HasAnySource(const PhonemeSpec &spec)
{
	return (spec.voicedLevel > 0.0f) || (spec.fricLevel > 0.0f);
}

void SsiVoice::LatchMode()
{
	m_mode = GetDurationSel();
	if (m_mode == kModeArDisabled)
	{
		m_request = false;
		m_sounding = false;
		m_phonemeCycles = 0.0;
		return;
	}
	BeginPhoneme();
}

void SsiVoice::BeginPhoneme()
{
	double seconds = GetPhonemeDurationSec();
	m_phonemeCycles = seconds * GetTickClockHz();
	m_sounding = (m_phonemeCycles > 0.0);
	m_request = false;
}

void SsiVoice::GlideFormants()
{
	const PhonemeSpec &spec = GetActiveSpec();
	if (spec.f1 == 0)
		return; // pause/closure: hold position

	double target[3] = { double(spec.f1), double(spec.f2), double(spec.f3) };

	if (m_fCur[0] <= 0.0)
	{
		for (int i = 0; i < 3; i++)
			m_fCur[i] = target[i];
		return;
	}

	float coef = m_articCoef[GetArticulation()];
	for (int i = 0; i < 3; i++)
		m_fCur[i] += coef * (target[i] - m_fCur[i]);
}

void SsiVoice::GlideLevels()
{
	const PhonemeSpec &spec = GetActiveSpec();
	m_vaCur += m_levelCoef * (spec.voicedLevel - m_vaCur);
	m_faCur += m_levelCoef * (spec.fricLevel - m_faCur);
}

// ---- Synthesis: 3-phase vocoder --------------------------------------------

float SsiVoice::GenerateSample()
{
	if (IsSilent() || m_sampleRate == 0)
		return 0.0f;

	// Phase A: shared per-sample update.
	GlideFormants();
	GlideLevels();

	// Phase B: per-voice excitation, summed.
	float impulseSum = 0.0f;
	for (int v = 0; v < kMaxVoices; v++)
	{
		if (!m_voices[v].active)
			continue;
		m_voices[v].phase += m_voices[v].inc;
		if (m_voices[v].phase >= 1.0)
		{
			m_voices[v].phase -= 1.0;
			impulseSum += 1.0f;
		}
	}
	// Excitation smoothing + voiced gain are linear/shared: smooth the summed
	// impulse train once rather than per voice.
	m_excLp1 += m_sourcePole * (impulseSum - m_excLp1);
	m_excLp2 += m_sourcePole * (m_excLp1 - m_excLp2);
	float exc = m_excLp2 * kVoicedGain * m_vaCur;
	exc *= static_cast<float>(731.0 / std::max(m_fCur[0], 170.0));

	// Phase C: shared tract cascade.
	double fs = static_cast<double>(m_sampleRate);
	float sample = exc;
	for (int s = 0; s < 3; s++)
	{
		double fc = std::clamp(m_fCur[s] * m_scale, 50.0, fs * 0.45);
		float b = m_twoR[s] * CosForHz(fc);
		float c = m_rr[s];
		float a0 = 1.0f - b - c;
		float y = a0 * sample + b * m_resY1[s] + c * m_resY2[s];
		m_resY2[s] = m_resY1[s];
		m_resY1[s] = y;
		sample = y;
	}

	// Parallel fricative branch.
	if (GetActiveSpec().fricative || m_faCur > 0.0001f)
	{
		// LFSR noise, one-pole smoothed.
		uint8_t bit = static_cast<uint8_t>(m_lfsr & 1u);
		m_lfsr >>= 1;
		if (bit != 0)
			m_lfsr ^= 0xB400u;
		m_noiseLp += kNoiseLpCoef * (((bit != 0) ? 1.0f : -1.0f) - m_noiseLp);

		double fc = std::clamp(m_fCur[1] * m_scale, 50.0, fs * 0.45);
		float b = m_fricTwoR * CosForHz(fc);
		float c = m_fricRR;
		float a0 = 1.0f - b - c;
		float fric = a0 * m_noiseLp + b * m_fricY1 + c * m_fricY2;
		m_fricY2 = m_fricY1;
		m_fricY1 = fric;

		m_fricLp  += kFricLpCoef * (fric - m_fricLp);
		m_fricLp2 += kFricLpCoef * (m_fricLp - m_fricLp2);
		m_fricLp3 += kFricLpCoef * (m_fricLp2 - m_fricLp3);

		sample += m_fricLp3 * kNoiseGain * m_faCur;
	}

	// Radiation differentiator (+6 dB/oct), host-rate normalized.
	float diffed = (sample - m_radPrev) * m_radScale;
	m_radPrev = sample;
	sample = diffed;

	// Output low-pass x2.
	m_outLp  += kOutputLpCoef * (sample - m_outLp);
	m_outLp2 += kOutputLpCoef * (m_outLp - m_outLp2);
	sample = m_outLp2;

	// Amplitude envelope.
	float target = m_sounding
	                   ? (HasAnySource(GetActiveSpec()) ? 1.0f : 0.0f) *
	                         (static_cast<float>(GetAmplitude()) / 15.0f)
	                   : 0.0f;
	float coef = (target > m_envLevel) ? m_attackCoef : m_releaseCoef;
	m_envLevel += coef * (target - m_envLevel);
	sample *= m_envLevel * kOutputGain;

	return std::clamp(sample, -1.0f, 1.0f);
}
