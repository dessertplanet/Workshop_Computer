// ssi_probe.h — host-only signal-range instrumentation for SsiVoice.
// Enabled by compiling SsiVoice.cpp with -DSSIVOICE_PROBE.
#pragma once
#include <cstddef>

constexpr int kSsiProbeCount = 16;
extern double g_ssiProbeMax[kSsiProbeCount];

inline void SsiProbeUpdate(int idx, double val)
{
	if (idx < 0 || idx >= kSsiProbeCount) return;
	double a = val < 0 ? -val : val;
	if (a > g_ssiProbeMax[idx]) g_ssiProbeMax[idx] = a;
}
