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
	uint16_t durMs;    // duration in milliseconds
};

// Phoneme codes used: PA .00  E .01  Y .03  AY .05  I .07  AE .0C  U .16
//   U1 .17  ER .1C  W .23  D .25  KV(g) .26  Z .2F  S .30  V .33  M .37  N .38
// Melody sits in a natural voice range (C3-A3, ~131-220 Hz); higher octaves
// make the formant vowels thin and quiet.
static constexpr DaisySeg kDaisy[] = {
	// "Dai-sy"  (C4, A3)
	{ 0x25, 60,  70 }, { 0x05, 60, 330 },   // D  AY
	{ 0x2F, 57,  90 }, { 0x01, 57, 330 },   // Z  E
	// "Dai-sy"  (F3, A3)
	{ 0x25, 53,  70 }, { 0x05, 53, 330 },   // D  AY
	{ 0x2F, 57,  90 }, { 0x01, 57, 330 },   // Z  E
	// "give"    (A3)
	{ 0x26, 57,  70 }, { 0x07, 57, 120 }, { 0x33, 57, 90 },  // KV I V
	// "me"      (G3)
	{ 0x37, 55,  90 }, { 0x01, 55, 280 },   // M  E
	// "your"    (F3)
	{ 0x03, 53,  70 }, { 0x17, 53, 300 },   // Y  U1
	// "an-"     (G3)
	{ 0x0C, 55,  70 }, { 0x38, 55, 260 },   // AE N
	// "swer"    (A3)
	{ 0x30, 57, 140 }, { 0x23, 57, 70 }, { 0x1C, 57, 280 },  // S W ER
	// "do"      (F3)
	{ 0x25, 53,  70 }, { 0x16, 53, 460 },   // D  U
	// breath before the loop
	{ 0x00, 53, 300 },                       // PA
};

static constexpr int kDaisyLen = static_cast<int>(sizeof(kDaisy) / sizeof(kDaisy[0]));
