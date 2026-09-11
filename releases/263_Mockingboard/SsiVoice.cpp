// SsiVoice.cpp — see SsiVoice.h. Register/timing model mirrors the reference
// Ssi263; the synthesis is the vocoder restructure with LUTs (no per-sample
// transcendentals).

#include "SsiVoice.h"

#include <cmath>
#include <numbers>
#include <algorithm>

// Optional signal-range instrumentation for the fixed-point migration. Zero
// cost unless SSIVOICE_PROBE is defined (host tooling only).
#ifdef SSIVOICE_PROBE
#include "tools/ssi_probe.h"
#define PROBE(idx, val) SsiProbeUpdate((idx), (double)(val))
#else
#define PROBE(idx, val) ((void)0)
#endif

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
constexpr double kCassoTuningRate = 44100.0;

float OnePoleCoef(double tauSec, double fs)
{
	return static_cast<float>(1.0 - std::exp(-1.0 / (tauSec * fs)));
}

float RetargetOnePole(float coefficient, double fs)
{
	return static_cast<float>(1.0 - std::pow(1.0 - coefficient,
	                                        kCassoTuningRate / fs));
}

// Q8.24 fixed-point: +-128 range, 24 fractional bits. The recursive resonators
// run here; products accumulate in int64 and narrow once (a single 3-tap term
// can momentarily exceed the +-128 range, but the settled output cannot).
constexpr int kFxShift = 24;
inline int32_t FxFromF(float v) { return static_cast<int32_t>(std::lrintf(v * 16777216.0f)); }
inline float   FxToF(int32_t v) { return static_cast<float>(v) * (1.0f / 16777216.0f); }

// One two-pole resonator step in fixed-point. State is Q8.24; b, c, a0 are
// converted here through the fast float->int path.
inline int32_t FxResonate(int32_t &y1, int32_t &y2, int32_t xin,
	                     int32_t a0Q, int32_t bQ, int32_t cQ)
{
	int64_t acc = static_cast<int64_t>(a0Q) * xin
	            + static_cast<int64_t>(bQ) * y1
	            + static_cast<int64_t>(cQ) * y2;
	int32_t y = static_cast<int32_t>(acc >> kFxShift);
	y2 = y1;
	y1 = y;
	return y;
}

inline int32_t FxMul(int32_t a, int32_t b)
{
	return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> kFxShift);
}

// One-pole: state += coef * (in - state), all Q8.24 (coef < 1).
inline void FxOnePole(int32_t &state, int32_t in, int32_t coefQ)
{
	state += static_cast<int32_t>((static_cast<int64_t>(coefQ) * (in - state)) >> kFxShift);
}

constexpr int32_t kFxOne = 1 << kFxShift;

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
			v.inc = static_cast<uint32_t>(v.hz * 4294967296.0 /
			                              static_cast<double>(m_sampleRate));
}

void SsiVoice::BuildTables()
{
	double fs = static_cast<double>(m_sampleRate);

	m_sourcePole = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * kSourceBreakHz / fs));

	for (int a = 0; a < 8; a++)
	{
		m_articCoef[a] = OnePoleCoef((8.0 - a) * 0.010, fs);
		m_articControlCoef[a] = 1.0f - std::pow(1.0f - m_articCoef[a],
		                                             kControlDivider);
	}

	m_levelCoef   = OnePoleCoef(kLevelTauSec, fs);
	m_levelControlCoef = 1.0f - std::pow(1.0f - m_levelCoef, kControlDivider);
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
	m_cosIndexScale = static_cast<float>(2.0 * kCosLutSize / fs);
	m_noiseRateComp = static_cast<float>(std::sqrt(fs / kCassoTuningRate));
	for (int s = 0; s < 3; s++)
		m_resCQ[s] = FxFromF(m_rr[s]);
	m_fricCQ = FxFromF(m_fricRR);

	// Fixed-point (Q8.24) copies of the audio-rate one-pole coefficients.
	m_sourcePoleQ = FxFromF(m_sourcePole);
	m_noiseLpQ    = FxFromF(RetargetOnePole(kNoiseLpCoef, fs));
	m_fricLpQ     = FxFromF(RetargetOnePole(kFricLpCoef, fs));
	m_outLpQ      = FxFromF(kOutputLpCoef);
	m_attackQ     = FxFromF(m_attackCoef);
	m_releaseQ    = FxFromF(m_releaseCoef);
	m_radScaleQ   = FxFromF(m_radScale);

	// cosLut_[i] = cos(pi * i / N); a hz maps to index round(2*fc/fs * N).
	for (int i = 0; i <= kCosLutSize; i++)
		m_cosLut[i] = static_cast<float>(std::cos(std::numbers::pi * i / kCosLutSize));
	// Peak-compressor gain curve over input magnitudes 0..4. It is exactly
	// unity below 0.75, then follows a tanh knee toward full scale.
	for (int i = 0; i <= kCompressorLutSize; i++)
	{
		float magnitude = static_cast<float>(i) / 64.0f;
		float gain = 1.0f;
		if (magnitude > 0.75f)
		{
			float limited = 0.75f + 0.25f * std::tanh((magnitude - 0.75f) / 0.25f);
			gain = limited / magnitude;
		}
		m_compressorGainLut[i] = FxFromF(gain);
	}
	m_compressorReleaseQ = FxFromF(OnePoleCoef(0.005, fs));
}

float SsiVoice::CosForHz(float hz) const
{
	float f = hz * m_cosIndexScale; // = 2*hz/fs * kCosLutSize
	int idx = static_cast<int>(f);
	if (idx < 0) { idx = 0; f = 0.0f; }
	if (idx >= kCosLutSize) { idx = kCosLutSize - 1; f = static_cast<float>(idx + 1); }
	float frac = f - idx;
	// Linear interpolation between adjacent LUT entries: keeps the high-Q
	// resonators from amplifying quantization error into audible detuning.
	return m_cosLut[idx] + frac * (m_cosLut[idx + 1] - m_cosLut[idx]);
}

void SsiVoice::RecomputeScale()
{
	double divisor = 2.0 * (256.0 - static_cast<double>(m_reg[kRegFilterFreq]));
	double ff = (divisor > 0.0) ? (m_xckHz / divisor) : 0.0;
	double s = ff / kNominalFilterHz;
	m_scale = static_cast<float>(std::clamp(s, 0.5, 2.0));
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
		SetVoicePhaseIncrement(i, 0);
		return;
	}
	m_voices[i].hz = hz;
	if (m_sampleRate > 0)
		SetVoicePhaseIncrement(i, static_cast<uint32_t>(
			hz * 4294967296.0 / static_cast<double>(m_sampleRate)));
}

void SsiVoice::SetVoicePhaseIncrement(int i, uint32_t increment)
{
	if (i < 0 || i >= kMaxVoices)
		return;
	bool active = increment != 0;
	if (m_voices[i].active != active)
	{
		m_activeVoiceCount += active ? 1 : -1;
		UpdateVoiceNormalizationTarget();
	}
	m_voices[i].active = active;
	m_voices[i].inc = increment;
}

void SsiVoice::SetVoiceActive(int i, bool on)
{
	if (i < 0 || i >= kMaxVoices)
		return;
	if (m_voices[i].active != on)
	{
		m_activeVoiceCount += on ? 1 : -1;
		UpdateVoiceNormalizationTarget();
	}
	m_voices[i].active = on;
}

int SsiVoice::ActiveVoiceCount() const
{
	return m_activeVoiceCount;
}

void SsiVoice::UpdateVoiceNormalizationTarget()
{
	static constexpr int32_t kNormQ16[kMaxVoices + 1] = {
		65536, 65536, 32768, 21845, 16384, 13107, 10923, 9362, 8192
	};
	m_voiceNormTargetQ16 = kNormQ16[m_activeVoiceCount];
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
		m_amplitudeQ = (static_cast<int32_t>(GetAmplitude()) * kFxOne) / 15;
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
	m_amplitudeQ = 0;

	m_mode = kModeArDisabled;
	m_request = false;
	m_phonemeCycles = 0.0;
	m_sounding = false;

	for (int i = 0; i < 3; i++)
	{
		m_fCur[i] = 0.0;
		m_resY1[i] = 0;
		m_resY2[i] = 0;
	}
	m_envLevel = 0;
	m_radPrev = 0;
	m_outLp = m_outLp2 = 0;
	m_excLp1 = m_excLp2 = 0;
	m_noiseLp = 0;
	m_vaCur = m_faCur = 0.0f;
	m_fricLp = m_fricLp2 = m_fricLp3 = 0;
	m_fricY1 = m_fricY2 = 0;
	m_lfsr = 0xACE1u;
	m_controlCounter = 0;
	m_activeVoiceCount = 0;
	m_voiceNormQ16 = 65536;
	m_voiceNormTargetQ16 = 65536;
	m_compressorEnvelope = 0;
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
	bool quiet = IsPoweredDown() || (GetAmplitude() == 0) || !m_sounding || !m_outputGate;
	return quiet && (m_envLevel < 16777); // 0.001 in Q8.24
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

	float target[3] = { float(spec.f1), float(spec.f2), float(spec.f3) };

	if (m_fCur[0] <= 0.0f)
	{
		for (int i = 0; i < 3; i++)
			m_fCur[i] = target[i];
		return;
	}

	float coef = m_articControlCoef[GetArticulation()];
	for (int i = 0; i < 3; i++)
		m_fCur[i] += coef * (target[i] - m_fCur[i]);
}

void SsiVoice::GlideLevels()
{
	const PhonemeSpec &spec = GetActiveSpec();
	m_vaCur += m_levelControlCoef * (spec.voicedLevel - m_vaCur);
	m_faCur += m_levelControlCoef * (spec.fricLevel - m_faCur);
}

void SsiVoice::UpdateControlState(uint8_t phase)
{
	float fcMax = static_cast<float>(m_sampleRate) * 0.45f;
	if (phase == 0)
	{
		GlideFormants();
		GlideLevels();
		m_fricativeActive = GetActiveSpec().fricative || m_faCur > 0.0001f;
		m_hasSource = HasAnySource(GetActiveSpec());
	}
	else if (phase == 1)
	{
		float voicedGain = kVoicedGain * m_vaCur;
		voicedGain *= 731.0f / std::max(m_fCur[0], 170.0f);
		m_voiceNormQ16 += (m_voiceNormTargetQ16 - m_voiceNormQ16) >> 3;
		int32_t baseGainQ16 = static_cast<int32_t>(std::lrintf(voicedGain * 65536.0f));
		m_voicedGainQ16 = static_cast<int32_t>(
			(static_cast<int64_t>(baseGainQ16) * m_voiceNormQ16) >> 16);
		m_fricGainQ = FxFromF(kNoiseGain * m_noiseRateComp * m_faCur);
	}
	else if (phase < 5)
	{
		int stage = phase - 2;
		float fc = std::clamp(m_fCur[stage] * m_scale, 50.0f, fcMax);
		float b = m_twoR[stage] * CosForHz(fc);
		m_resBQ[stage] = FxFromF(b);
		m_resA0Q[stage] = kFxOne - m_resBQ[stage] - m_resCQ[stage];
	}
	else if (phase == 5)
	{
		float fc = std::clamp(m_fCur[1] * m_scale, 50.0f, fcMax);
		float b = m_fricTwoR * CosForHz(fc);
		m_fricBQ = FxFromF(b);
		m_fricA0Q = kFxOne - m_fricBQ - m_fricCQ;
	}
}

// ---- Synthesis: 3-phase vocoder --------------------------------------------

int32_t SsiVoice::GenerateSample()
{
	if (IsSilent() || m_sampleRate == 0)
		return 0;

	// Phase A: update one coefficient set per sample. Every set runs at 6 kHz,
	// but the expensive work is spread out instead of causing a periodic spike.
	UpdateControlState(m_controlCounter);
	m_controlCounter = static_cast<uint8_t>((m_controlCounter + 1) % kControlDivider);

	// Phase B: per-voice excitation, summed.
	int impulseCount = 0;
	for (int v = 0; v < kMaxVoices; v++)
	{
		if (!m_voices[v].active)
			continue;
		uint32_t oldPhase = m_voices[v].phase;
		m_voices[v].phase += m_voices[v].inc;
		if (m_voices[v].phase < oldPhase)
		{
			impulseCount++;
		}
	}
	// Excitation smoothing is linear/shared: smooth the summed impulse train
	// once (fixed-point), then apply the voiced gain + F1 correction (the 966
	// gain and per-sample divide stay float, isolated to this one spot).
	FxOnePole(m_excLp1, impulseCount * kFxOne, m_sourcePoleQ);
	FxOnePole(m_excLp2, m_excLp1, m_sourcePoleQ);
	int32_t sig = static_cast<int32_t>((static_cast<int64_t>(m_excLp2) *
	                                   m_voicedGainQ16) >> 16);
	PROBE(0, impulseCount); PROBE(1, FxToF(m_excLp2)); PROBE(2, FxToF(sig));

	// Phase C: shared tract cascade (fixed-point resonators, int64 accumulate).
	for (int s = 0; s < 3; s++)
	{
		sig = FxResonate(m_resY1[s], m_resY2[s], sig,
		                 m_resA0Q[s], m_resBQ[s], m_resCQ[s]);
		PROBE(3 + s, FxToF(sig));
	}
	int32_t sampleQ = sig;

	// Parallel fricative branch.
	if (m_fricativeActive)
	{
		// LFSR noise, one-pole smoothed (fixed-point).
		uint8_t bit = static_cast<uint8_t>(m_lfsr & 1u);
		m_lfsr >>= 1;
		if (bit != 0)
			m_lfsr ^= 0xB400u;
		FxOnePole(m_noiseLp, (bit != 0) ? kFxOne : -kFxOne, m_noiseLpQ);

		int32_t fricQ = FxResonate(m_fricY1, m_fricY2, m_noiseLp,
		                               m_fricA0Q, m_fricBQ, m_fricCQ);

		FxOnePole(m_fricLp, fricQ, m_fricLpQ);
		FxOnePole(m_fricLp2, m_fricLp, m_fricLpQ);
		FxOnePole(m_fricLp3, m_fricLp2, m_fricLpQ);

		// noiseGain * faCur is control-rate; convert once and mix in.
		sampleQ += FxMul(m_fricLp3, m_fricGainQ);
		PROBE(6, FxToF(fricQ)); PROBE(7, FxToF(m_fricLp3));
	}

	// Radiation differentiator (+6 dB/oct), host-rate normalized.
	int32_t diffed = FxMul(m_radScaleQ, sampleQ - m_radPrev);
	m_radPrev = sampleQ;
	sampleQ = diffed;
	PROBE(8, FxToF(sampleQ));

	// Output low-pass x2.
	FxOnePole(m_outLp, sampleQ, m_outLpQ);
	FxOnePole(m_outLp2, m_outLp, m_outLpQ);
	sampleQ = m_outLp2;

	// Amplitude envelope.
	int32_t targetQ = (m_sounding && m_hasSource && m_outputGate) ? m_amplitudeQ : 0;
	FxOnePole(m_envLevel, targetQ, (targetQ > m_envLevel) ? m_attackQ : m_releaseQ);
	int32_t envGainQ = m_envLevel * static_cast<int32_t>(kOutputGain);
	sampleQ = FxMul(sampleQ, envGainQ);
	PROBE(9, FxToF(sampleQ));

	return Compress(sampleQ);
}

int32_t SsiVoice::Compress(int32_t sample)
{
	uint32_t magnitude = sample < 0
	                       ? static_cast<uint32_t>(-static_cast<int64_t>(sample))
	                       : static_cast<uint32_t>(sample);
	if (magnitude > static_cast<uint32_t>(m_compressorEnvelope))
		m_compressorEnvelope = static_cast<int32_t>(std::min<uint32_t>(magnitude, INT32_MAX));
	else
		FxOnePole(m_compressorEnvelope, static_cast<int32_t>(magnitude), m_compressorReleaseQ);

	uint32_t index = static_cast<uint32_t>(m_compressorEnvelope) >> 18;
	uint32_t fraction = static_cast<uint32_t>(m_compressorEnvelope) & 0x3FFFFu;
	int32_t gainQ;
	if (index >= kCompressorLutSize)
	{
		gainQ = m_compressorGainLut[kCompressorLutSize];
	}
	else
	{
		int32_t delta = m_compressorGainLut[index + 1] - m_compressorGainLut[index];
		gainQ = m_compressorGainLut[index] + static_cast<int32_t>(
			(static_cast<int64_t>(delta) * fraction) >> 18);
	}

	return std::clamp(FxMul(sample, gainQ), -kFxOne, kFxOne);
}
