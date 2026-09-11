// SsiVoice.h — polyphonic vocoder port of the SSI-263 formant synthesizer.
//
// Same register model and phoneme timing as the reference Ssi263, but the
// synthesis is restructured for the "N voices, one shared tract" case (see
// rp2040-voice-port-design.md):
//
//   * Shared per-sample update: glides, resonator coefficients (cos LUT),
//     fricative signal, envelope.
//   * Per-voice: glottal phase accumulators only; their impulses are summed.
//   * Shared back-end: one tract cascade over the summed excitation, plus the
//     shared fricative, radiation, output LP and envelope.
//
// The tract is linear, so summing excitations and running one tract equals
// running one tract per voice and summing — valid while all voices share the
// phoneme program, which they do. The final clamp is the only nonlinearity.
//
// This revision is float arithmetic with LUTs/precomputed coefficients (no
// per-sample transcendentals); the fixed-point conversion is a follow-up that
// keeps this structure.
#pragma once

#include <cstdint>
#include "phoneme_rom.h"

class SsiVoice
{
public:
	// Maximum simultaneous glottal voices (chord size).
	static constexpr int kMaxVoices = 8;

	// Register file (identical layout to the SSI-263).
	static constexpr uint8_t kRegCount    = 5;
	static constexpr uint8_t kAddressMask = 0x07;

	static constexpr uint8_t kRegDurationPhoneme = 0;
	static constexpr uint8_t kRegInflection      = 1;
	static constexpr uint8_t kRegRateInflection  = 2;
	static constexpr uint8_t kRegCtlArtAmp       = 3;
	static constexpr uint8_t kRegFilterFreq      = 4;

	static constexpr uint8_t kPhonemeMask   = 0x3F;
	static constexpr uint8_t kDurationShift = 6;

	static constexpr uint8_t kCtl           = 0x80;
	static constexpr uint8_t kArticMask     = 0x70;
	static constexpr uint8_t kArticShift    = 4;
	static constexpr uint8_t kAmplitudeMask = 0x0F;

	static constexpr uint8_t kRateMask       = 0xF0;
	static constexpr uint8_t kRateShift      = 4;
	static constexpr uint8_t kInflect11      = 0x08;
	static constexpr uint8_t kInflectLowMask = 0x07;

	static constexpr uint8_t kModePhonemeTransitioned = 3;
	static constexpr uint8_t kModePhonemeImmediate    = 2;
	static constexpr uint8_t kModeFrameImmediate      = 1;
	static constexpr uint8_t kModeArDisabled          = 0;

	static constexpr double  kDefaultXckHz    = 1789772.5;
	static constexpr double  kNominalFilterHz = 20000.0;

	explicit SsiVoice(double xckHz = kDefaultXckHz);

	void SetSampleRate(uint32_t sampleRate);
	void SetXckClock(double xckHz);
	void SetTickClock(double tickClockHz);

	void WriteRegister(uint8_t reg, uint8_t value);
	void Reset();
	void Tick(uint32_t cycles);

	// ---- Vocoder voices --------------------------------------------------
	// Each voice is a glottal phase accumulator at a fundamental frequency.
	// The shared phoneme program supplies everything else.
	void SetVoicePitch(int i, double hz);       // hz <= 0 deactivates
	void SetVoiceActive(int i, bool on);
	int  ActiveVoiceCount() const;

	// Render one mono sample summing all active voices through one tract.
	float GenerateSample();

	// ---- Status / getters ------------------------------------------------
	bool    IsRequesting()  const { return m_request; }
	bool    IsPoweredDown() const { return (m_reg[kRegCtlArtAmp] & kCtl) != 0; }
	bool    IsSilent()      const;

	uint8_t GetPhoneme()      const { return static_cast<uint8_t>(m_reg[kRegDurationPhoneme] & kPhonemeMask); }
	uint8_t GetDurationSel()  const { return static_cast<uint8_t>(m_reg[kRegDurationPhoneme] >> kDurationShift); }
	uint8_t GetRateSel()      const { return static_cast<uint8_t>((m_reg[kRegRateInflection] & kRateMask) >> kRateShift); }
	uint8_t GetAmplitude()    const { return static_cast<uint8_t>(m_reg[kRegCtlArtAmp] & kAmplitudeMask); }
	uint8_t GetArticulation() const { return static_cast<uint8_t>((m_reg[kRegCtlArtAmp] & kArticMask) >> kArticShift); }
	uint8_t GetActiveMode()   const { return m_mode; }

	uint16_t GetInflectionValue() const;
	double   GetFrameDurationSec() const;
	double   GetPhonemeDurationSec() const;
	double   GetFilterFrequencyHz() const;
	double   GetInflectionFrequencyHz() const;

	static uint8_t SelectRegister(uint8_t address);

private:
	double GetTickClockHz() const { return (m_tickClockHz > 0.0) ? m_tickClockHz : m_xckHz; }

	void LatchMode();
	void BeginPhoneme();
	void GlideFormants();
	void GlideLevels();
	void RecomputeScale();
	void BuildTables();

	const PhonemeSpec &GetActiveSpec() const;
	static bool HasAnySource(const PhonemeSpec &spec);

	// Cosine lookup: cosLut_[i] = cos(pi * i / kCosLutSize), i in [0, kCosLutSize].
	static constexpr int kCosLutSize = 4096;
	float  CosForHz(float hz) const;

	// ---- Register / timing state (shared, cheap) -------------------------
	double   m_xckHz       = kDefaultXckHz;
	double   m_tickClockHz = 0.0;
	uint32_t m_sampleRate  = 0;

	uint8_t  m_reg[kRegCount] = {};
	uint8_t  m_mode = kModeArDisabled;
	bool     m_request = false;
	double   m_phonemeCycles = 0.0;
	bool     m_sounding = false;

	// ---- Precomputed coefficients (rebuilt on rate change) ---------------
	float    m_sourcePole = 0.0065f;
	float    m_articCoef[8] = {};   // formant glide, by articulation 0..7
	float    m_levelCoef = 0.0f;    // source-amplitude glide
	float    m_attackCoef = 0.0f;
	float    m_releaseCoef = 0.0f;
	float    m_r[3] = {};           // resonator pole radius per stage
	float    m_rr[3] = {};          // -(r*r) per stage
	float    m_twoR[3] = {};        // 2*r per stage
	float    m_fricR = 0.0f;
	float    m_fricRR = 0.0f;
	float    m_fricTwoR = 0.0f;
	float    m_radScale = 1.0f;     // (fs / 44100)
	float    m_cosLut[kCosLutSize + 1] = {};
	float    m_cosIndexScale = 0.0f; // hz -> LUT index: 2*kCosLutSize/fs
	float    m_scale = 1.0f;        // filter-freq voice-type scale, from reg4

	// Fixed-point (Q8.24) coefficients for the audio-rate one-poles etc.
	int32_t  m_sourcePoleQ = 0;
	int32_t  m_noiseLpQ = 0;
	int32_t  m_fricLpQ = 0;
	int32_t  m_outLpQ = 0;
	int32_t  m_attackQ = 0;
	int32_t  m_releaseQ = 0;
	int32_t  m_radScaleQ = 0;
	// Excitation level scales with 1/fs; this normalizes it to the 48 kHz the
	// chip model was voiced at, so lower sample rates keep the same loudness.
	float    m_excRateComp = 1.0f;

	// ---- Glide state (shared) --------------------------------------------
	float    m_fCur[3] = { 0.0f, 0.0f, 0.0f };
	float    m_vaCur = 0.0f;
	float    m_faCur = 0.0f;

	// ---- Shared synthesis state ------------------------------------------
	// Audio-rate DSP state is Q8.24 fixed-point (int32).
	int32_t  m_resY1[3] = {};
	int32_t  m_resY2[3] = {};
	int32_t  m_excLp1 = 0;
	int32_t  m_excLp2 = 0;
	uint32_t m_lfsr = 0xACE1u;
	int32_t  m_noiseLp = 0;
	int32_t  m_fricLp = 0, m_fricLp2 = 0, m_fricLp3 = 0;
	int32_t  m_fricY1 = 0, m_fricY2 = 0;   // Q8.24
	int32_t  m_radPrev = 0;
	int32_t  m_outLp = 0, m_outLp2 = 0;
	int32_t  m_envLevel = 0;

	// ---- Per-voice glottal oscillators -----------------------------------
	struct Voice
	{
		bool     active = false;
		double   phase = 0.0;   // [0,1)
		double   inc = 0.0;     // pitch / fs
		double   hz = 0.0;
	};
	Voice    m_voices[kMaxVoices];
};
