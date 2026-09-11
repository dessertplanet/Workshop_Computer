// daisy_demo.h — hard-coded "Daisy Bell" test score for the first hardware
// bring-up: the famous 1961 Bell Labs speech-synthesis demo ("Daisy, Daisy,
// give me your answer, do"). Each entry is one phoneme with the melody note it
// is sung on and its duration; the card steps through them on loop, setting the
// glottal voice pitch per syllable. Consonants are short, vowels (the held
// nuclei) longer. The melody is an approximate Daisy Bell contour.
#pragma once

#include <cstdint>

struct DaisySeg
{
	uint8_t  phoneme;  // SSI-263 phoneme code (see phoneme_rom.h)
	uint8_t  note;     // MIDI note number for this syllable
	uint8_t  units;    // Casso song units, approximately 41.3 ms each
};

static constexpr double kDaisyXckHz = 1022727.0;
static constexpr uint8_t kDaisyFilter = 0xE6;
static constexpr uint8_t kDaisyArticulation = 5;
static constexpr uint8_t kDaisyAmplitude = 0x0C;
static constexpr int32_t kDaisyUnitSamples = 991; // 1280 cycles * 33 at 1.022727 MHz
static constexpr int kDemoVoiceCount = 8;
static constexpr int8_t kDemoChordSemitones[kDemoVoiceCount] = {
	0, 4, 7, 12, 16, 19, 24, 28
};

// Phoneme codes used: PA .00  E .01  Y .03  AY .05  I .07  AE .0C  U .16
//   U1 .17  ER .1C  W .23  D .25  KV(g) .26  Z .2F  S .30  V .33  M .37  N .38
// Melody sits in a natural voice range (C3-A3, ~131-220 Hz); higher octaves
// make the formant vowels thin and quiet.
static constexpr DaisySeg kDaisy[] = {
	// Casso's Daisy Bell score: consonants take fixed short slices and each
	// vowel nucleus receives the remainder of its syllable.
	{ 0x25, 55,  1 }, { 0x08, 55, 23 }, { 0x01, 55,  6 }, // Dai  G3: D A E
	{ 0x2F, 52,  3 }, { 0x01, 52, 27 },                   // sy   E3: Z E
	{ 0x25, 48,  1 }, { 0x08, 48, 23 }, { 0x01, 48,  6 }, // Dai  C3: D A E
	{ 0x2F, 43,  3 }, { 0x01, 43, 27 },                   // sy   G2: Z E
	{ 0x26, 45,  1 }, { 0x07, 45,  7 }, { 0x33, 45,  2 }, // give A2: KV I V
	{ 0x37, 47,  2 }, { 0x01, 47,  8 },                   // me   B2: M E
	{ 0x03, 48,  2 }, { 0x11, 48,  8 },                   // your C3: Y O
	{ 0x0C, 45, 17 }, { 0x38, 45,  3 },                   // an   A2: AE N
	{ 0x30, 48,  3 }, { 0x1C, 48,  7 },                   // swer C3: S ER
	{ 0x25, 43,  1 }, { 0x16, 43, 49 },                   // do   G2: D U
	{ 0x00, 43, 10 },                                     // rest G2: PA
};

static constexpr int kDaisyLen = static_cast<int>(sizeof(kDaisy) / sizeof(kDaisy[0]));

static constexpr uint8_t kNoPhoneme = 0xFF;

struct DaisyStep
{
	uint8_t onset;
	uint8_t onsetUnits;
	uint8_t nucleus;
	uint8_t coda;
	uint8_t codaUnits;
};

static constexpr DaisyStep kDaisySteps[] = {
	{ 0x25, 1, 0x08, 0x01,  6 }, // Dai:  D - A  - E
	{ 0x2F, 3, 0x01, kNoPhoneme, 0 }, // sy:   Z - E
	{ 0x25, 1, 0x08, 0x01,  6 }, // Dai:  D - A  - E
	{ 0x2F, 3, 0x01, kNoPhoneme, 0 }, // sy:   Z - E
	{ 0x26, 1, 0x07, 0x33,  2 }, // give: KV - I  - V
	{ 0x37, 2, 0x01, kNoPhoneme, 0 }, // me:   M - E
	{ 0x03, 2, 0x11, kNoPhoneme, 0 }, // your: Y - O
	{ kNoPhoneme, 0, 0x0C, 0x38, 3 }, // an:       AE - N
	{ 0x30, 3, 0x1C, kNoPhoneme, 0 }, // swer: S - ER
	{ 0x25, 1, 0x16, 0x00, 10 }, // do:   D - U  - pause
};

static constexpr int kDaisyStepCount =
	static_cast<int>(sizeof(kDaisySteps) / sizeof(kDaisySteps[0]));
