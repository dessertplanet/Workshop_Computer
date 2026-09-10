// phoneme_rom.h — SSI-263 per-phoneme acoustic ROM, shared by the port engine.
//
// Values are the 64-entry table extracted from visual6502 die shots of the
// SSI 263P (see Ssi263.cpp for provenance). Kept in a standalone header so the
// port engine (SsiVoice) and any host tooling use identical data without
// pulling in the reference engine's class.
#pragma once

#include <cstdint>

struct PhonemeSpec
{
	uint16_t f1, f2, f3;   // formant center frequencies, Hz
	bool     voiced;       // glottal excitation (unused by DSP; kept for clarity)
	bool     fricative;    // noise excitation
	float    voicedLevel;  // VA / 15
	float    fricLevel;    // FA / 15
};

static constexpr int kPhonemeCount = 64;

static constexpr PhonemeSpec kPhonemeRom[kPhonemeCount] =
{
	{    0,    0,    0, false, false, 0.00f, 0.00f },   // 00 PA   (pause)
	{  314, 2330, 2756, true,  false, 0.80f, 0.00f },   // 01 E    meet
	{  446, 2330, 2677, true,  false, 0.67f, 0.00f },   // 02 E1   bent
	{  256, 2257, 2756, true,  false, 0.73f, 0.00f },   // 03 Y    before
	{  314, 2174, 2511, true,  false, 0.40f, 0.00f },   // 04 YI   year
	{  365, 2330, 2756, true,  false, 0.80f, 0.00f },   // 05 AY   please
	{  256, 2408, 2832, true,  false, 0.60f, 0.00f },   // 06 IE   any
	{  446, 2007, 2598, true,  false, 0.53f, 0.00f },   // 07 I    six
	{  482, 2096, 2511, true,  false, 0.53f, 0.00f },   // 08 A    made
	{  482, 1922, 2425, true,  false, 0.53f, 0.00f },   // 09 AI   care
	{  577, 1823, 2511, true,  false, 0.53f, 0.00f },   // 0A EH   nest
	{  605, 1922, 2511, true,  false, 0.53f, 0.00f },   // 0B EH1  belt
	{  683, 1922, 2511, true,  false, 0.40f, 0.00f },   // 0C AE   dad
	{  731, 1730, 2511, true,  false, 0.40f, 0.00f },   // 0D AE1  after
	{  731, 1261, 2511, true,  false, 0.40f, 0.00f },   // 0E AH   got
	{  731, 1387, 2511, true,  false, 0.47f, 0.00f },   // 0F AH1  father
	{  683, 1106, 2425, true,  false, 0.40f, 0.00f },   // 10 AW   office
	{  516,  943, 2511, true,  false, 0.53f, 0.00f },   // 11 O    store
	{  446,  943, 2511, true,  false, 0.60f, 0.00f },   // 12 OU   boat
	{  546, 1106, 2425, true,  false, 0.53f, 0.00f },   // 13 OO   look
	{  365, 1620, 2425, true,  false, 0.67f, 0.00f },   // 14 IU   you
	{  406, 1387, 2425, true,  false, 0.60f, 0.00f },   // 15 IU1  could
	{  365,  943, 2244, true,  false, 0.67f, 0.00f },   // 16 U    tune
	{  256,  722, 2142, true,  false, 0.67f, 0.00f },   // 17 U1   cartoon
	{  546, 1387, 2511, true,  false, 0.67f, 0.00f },   // 18 UH   wonder
	{  605, 1261, 2511, true,  false, 0.53f, 0.00f },   // 19 UH1  love
	{  657, 1261, 2511, true,  false, 0.40f, 0.00f },   // 1A UH2  what
	{  657, 1514, 2511, true,  false, 0.47f, 0.00f },   // 1B UH3  nut
	{  482, 1387, 1696, true,  false, 0.53f, 0.00f },   // 1C ER   bird
	{  365,  943, 1424, true,  false, 0.53f, 0.00f },   // 1D R    roof
	{  314, 1261, 1822, true,  false, 0.53f, 0.00f },   // 1E R1   rug
	{  516, 1620, 2336, true,  false, 0.40f, 0.00f },   // 1F R2   mutter
	{  365, 1261, 2756, true,  false, 0.47f, 0.00f },   // 20 L    lift
	{  256, 1514, 2832, true,  false, 0.53f, 0.00f },   // 21 L1   play
	{  446,  943, 2756, true,  false, 0.60f, 0.00f },   // 22 LF   fall
	{  365,  722, 2336, true,  false, 0.53f, 0.00f },   // 23 W    water
	{  256, 1261, 2598, true,  false, 0.53f, 0.00f },   // 24 B    bag
	{  256, 1922, 2756, true,  false, 0.53f, 0.00f },   // 25 D    paid
	{  365, 2007, 2244, true,  false, 0.67f, 0.00f },   // 26 KV   tag
	{  406, 1106, 2244, false, true,  0.00f, 1.00f },   // 27 P    pen
	{  406, 1922, 2756, false, true,  0.00f, 1.00f },   // 28 T    tart
	{  365, 2007, 2244, false, true,  0.00f, 0.27f },   // 29 K    kit
	{  516, 1922, 2598, true,  false, 0.40f, 0.00f },   // 2A HV   (hold vocal)
	{  516, 1922, 2598, false, false, 0.00f, 0.00f },   // 2B HVC  (hold vocal closure)
	{  516, 1922, 2598, false, true,  0.00f, 0.53f },   // 2C HF   heart
	{  516, 1922, 2598, false, false, 0.00f, 0.00f },   // 2D HFC  (hold fric closure)
	{  516, 1922, 2598, true,  false, 0.27f, 0.00f },   // 2E HN   (hold nasal)
	{  365, 1106, 2677, true,  true,  0.07f, 0.67f },   // 2F Z    zero
	{  176, 1730, 2598, false, true,  0.00f, 1.00f },   // 30 S    same
	{  314, 2096, 2756, true,  true,  0.07f, 0.67f },   // 31 J    measure
	{  314, 2096, 2756, false, true,  0.00f, 0.40f },   // 32 SCH  ship
	{  314, 1261, 2336, true,  true,  0.20f, 0.27f },   // 33 V    very
	{  314, 1261, 2336, false, true,  0.00f, 0.27f },   // 34 F    four
	{  365, 1730, 2756, true,  true,  0.07f, 0.13f },   // 35 THV  there
	{  446, 1823, 2425, false, true,  0.00f, 0.13f },   // 36 TH   with
	{  176, 1261, 2336, true,  false, 0.67f, 0.00f },   // 37 M    more
	{  176, 1823, 2677, true,  false, 0.53f, 0.00f },   // 38 N    nine
	{  314, 2174, 2756, true,  false, 0.27f, 0.00f },   // 39 NG   rang
	{  516, 1922, 2425, true,  false, 0.53f, 0.00f },   // 3A :A   maerchen
	{  314, 1823, 2336, true,  false, 0.40f, 0.00f },   // 3B :OH  loewe
	{  256, 1730, 2336, true,  false, 0.67f, 0.00f },   // 3C :U   fuenf
	{  176, 1922, 2425, true,  false, 0.67f, 0.00f },   // 3D :UH  menu
	{  482, 1730, 2425, true,  false, 0.47f, 0.00f },   // 3E E2   bitte
	{  256,  943, 2756, true,  false, 0.53f, 0.00f },   // 3F LB   lube
};
