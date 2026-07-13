#include "fastmath.h"

#include <stdint.h>
#include <string.h>

#include <limits.h>

#include "fastmath_lut.h"

#include "lua.h"
#include "lauxlib.h"

// --- Fixed-point helpers ---

#define FM_Q16_ONE  (1 << 16)
#define FM_PI_Q16   205887  // round(pi * 65536)
#define FM_PIO2_Q16 102944  // round((pi/2) * 65536)
#define FM_LN2_Q16  45426   // round(ln(2) * 65536)

// phase units: 2^32 == 2*pi
#define FM_PHASE_PER_RAD  (uint32_t)(684695129u) // round(2^32 / (2*pi))

static inline int32_t fm_interp_q(int32_t a, int32_t b, uint32_t frac, uint32_t frac_bits) {
    // frac in [0, 2^frac_bits)
    int32_t diff = b - a;
    int64_t t = (int64_t)diff * (int64_t)frac;
    return a + (int32_t)(t >> frac_bits);
}

static inline float fm_nan_f(void) {
    volatile float z = 0.0f;
    return z / z;
}

static inline float fm_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline int32_t fm_sin_q30_from_quarter_phase(uint32_t quarter_phase) {
    // quarter_phase in [0, 0x4000_0000]
    if (quarter_phase >= 0x40000000u) {
        return fm_sin_q30_quarter_lut[FM_SIN_Q30_LUT_SIZE];
    }

    const uint32_t frac_bits = 30u - FM_SIN_Q30_LUT_BITS;
    uint32_t idx = quarter_phase >> frac_bits;            // 0..1023
    uint32_t frac = quarter_phase & ((1u << frac_bits) - 1u);

    int32_t a = fm_sin_q30_quarter_lut[idx];
    int32_t b = fm_sin_q30_quarter_lut[idx + 1u];
    return fm_interp_q(a, b, frac, frac_bits);
}

static inline int32_t fm_sin_q30_from_phase(uint32_t phase) {
    uint32_t quadrant = phase >> 30;          // 0..3
    uint32_t offset = phase & 0x3FFFFFFFu;    // 0..(2^30-1)

    int32_t v;
    switch (quadrant) {
        case 0:
            v = fm_sin_q30_from_quarter_phase(offset);
            break;
        case 1:
            v = fm_sin_q30_from_quarter_phase(0x40000000u - offset);
            break;
        case 2:
            v = -fm_sin_q30_from_quarter_phase(offset);
            break;
        default: // 3
            v = -fm_sin_q30_from_quarter_phase(0x40000000u - offset);
            break;
    }
    return v;
}

static inline float fm_sin_f(float x) {
    // Clamp to avoid float->int overflow UB. (Huge |x| is rare in crow scripts.)
    x = fm_clampf(x, -1000000.0f, 1000000.0f);
    int64_t p = (int64_t)(x * (float)FM_PHASE_PER_RAD);
    uint32_t phase = (uint32_t)p;
    int32_t s_q30 = fm_sin_q30_from_phase(phase);
    return (float)s_q30 / (float)(1u << 30);
}

static inline float fm_cos_f(float x) {
    x = fm_clampf(x, -1000000.0f, 1000000.0f);
    int64_t p = (int64_t)(x * (float)FM_PHASE_PER_RAD);
    uint32_t phase = (uint32_t)p + 0x40000000u;
    int32_t c_q30 = fm_sin_q30_from_phase(phase);
    return (float)c_q30 / (float)(1u << 30);
}

static inline int32_t fm_atan_q16_from_ratio_q16(uint32_t r_q16) {
    // r_q16 in [0, 65536]
    const uint32_t frac_bits = 16u - FM_ATAN_Q16_LUT_BITS; // 6
    uint32_t idx = r_q16 >> frac_bits;                     // 0..1024
    if (idx >= FM_ATAN_Q16_LUT_SIZE) {
        return fm_atan_q16_lut[FM_ATAN_Q16_LUT_SIZE];
    }
    uint32_t frac = r_q16 & ((1u << frac_bits) - 1u);

    int32_t a = fm_atan_q16_lut[idx];
    int32_t b = fm_atan_q16_lut[idx + 1u];
    return fm_interp_q(a, b, frac, frac_bits);
}

static inline float fm_atan2_f(float y, float x) {
    // Convert to Q16 for integer-heavy core.
    // Clamp to avoid float->int overflow UB.
    x = fm_clampf(x, -32767.0f, 32767.0f);
    y = fm_clampf(y, -32767.0f, 32767.0f);
    int32_t xq = (int32_t)(x * (float)FM_Q16_ONE);
    int32_t yq = (int32_t)(y * (float)FM_Q16_ONE);

    if (xq == 0) {
        if (yq > 0) return (float)FM_PIO2_Q16 / (float)FM_Q16_ONE;
        if (yq < 0) return -(float)FM_PIO2_Q16 / (float)FM_Q16_ONE;
        return 0.0f;
    }
    if (yq == 0) {
        return (xq < 0) ? (float)FM_PI_Q16 / (float)FM_Q16_ONE : 0.0f;
    }

    uint32_t ax = (uint32_t)(xq < 0 ? -xq : xq);
    uint32_t ay = (uint32_t)(yq < 0 ? -yq : yq);

    uint32_t maxv = ax > ay ? ax : ay;
    uint32_t minv = ax > ay ? ay : ax;

    // ratio in Q16: min/max
    uint32_t r_q16 = (maxv == 0) ? 0 : (uint32_t)(((uint64_t)minv << 16) / (uint64_t)maxv);
    if (r_q16 > 0x00010000u) r_q16 = 0x00010000u;

    int32_t base_q16 = fm_atan_q16_from_ratio_q16(r_q16);
    int32_t angle_q16 = (ay > ax) ? (FM_PIO2_Q16 - base_q16) : base_q16;

    // quadrant fix
    if (xq < 0) {
        angle_q16 = FM_PI_Q16 - angle_q16;
    }
    if (yq < 0) {
        angle_q16 = -angle_q16;
    }

    return (float)angle_q16 / (float)FM_Q16_ONE;
}

static inline int32_t fm_log2_q16(float x) {
    if (!(x > 0.0f)) {
        // Signal invalid domain to callers; they will return NaN at float boundary.
        return INT32_MIN;
    }

    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));

    int32_t exp = (int32_t)((bits >> 23) & 0xFF) - 127;
    uint32_t mant = bits & 0x7FFFFFu;

    // 1.m in [1,2). Use top 8 mantissa bits as LUT index.
    uint32_t idx = mant >> (23 - 8);         // 0..255
    uint32_t frac = mant & ((1u << (23 - 8)) - 1u); // 15-bit fraction

    int32_t a = fm_log2_q16_lut[idx];
    int32_t b = fm_log2_q16_lut[idx + 1u];
    int32_t frac_q16 = fm_interp_q(a, b, frac, 23u - 8u); // keep as Q16

    return (exp << 16) + frac_q16;
}

static inline float fm_exp2_f(float x) {
    // Convert to Q16.
    // Clamp to avoid float->int overflow UB.
    x = fm_clampf(x, -32768.0f, 32767.0f);
    int32_t xq = (int32_t)(x * (float)FM_Q16_ONE);
    int32_t k = xq >> 16; // floor for negative values
    uint32_t frac_q16 = (uint32_t)xq & 0xFFFFu;

    // frac in [0,1): use LUT with 8-bit index + 8-bit frac for interpolation.
    uint32_t idx = frac_q16 >> 8;         // 0..255
    uint32_t frac8 = frac_q16 & 0xFFu;    // 0..255

    uint32_t a = fm_exp2_q16_lut[idx];
    uint32_t b = fm_exp2_q16_lut[idx + 1u];
    uint32_t base_q16 = (uint32_t)fm_interp_q((int32_t)a, (int32_t)b, frac8, 8u);

    // Scale by 2^k.
    uint64_t v = (uint64_t)base_q16;
    if (k >= 0) {
        if (k > 15) { // overflow risk in Q16
            return 3.402823466e+38f; // ~FLT_MAX
        }
        v <<= (uint32_t)k;
    } else {
        int32_t s = -k;
        if (s > 31) return 0.0f;
        v >>= (uint32_t)s;
    }

    return (float)v / (float)FM_Q16_ONE;
}

static inline float fm_log_f(float x) {
    int32_t log2_q16 = fm_log2_q16(x);
    if (log2_q16 == INT32_MIN) return fm_nan_f();
    // ln(x) = log2(x) * ln(2)
    int64_t ln_q16 = (int64_t)log2_q16 * (int64_t)FM_LN2_Q16;
    return (float)(ln_q16 >> 16) / (float)FM_Q16_ONE;
}

static inline float fm_exp_f(float x) {
    // exp(x) = exp2(x / ln2)
    const float inv_ln2 = 1.4426950408889634f;
    return fm_exp2_f(x * inv_ln2);
}

static inline int32_t fm_is_integer_like(float x) {
    // Accept integers exactly representable in float.
    int32_t xi = (int32_t)x;
    return (x == (float)xi);
}

static inline float fm_pow_f(float a, float b) {
    if (a == 2.0f) {
        return fm_exp2_f(b);
    }

    if (a < 0.0f) {
        if (!fm_is_integer_like(b)) {
            return fm_nan_f();
        }
        int32_t log2a_q16 = fm_log2_q16(-a);
        if (log2a_q16 == INT32_MIN) return fm_nan_f();
        float mag = fm_exp2_f(b * ((float)log2a_q16 / (float)FM_Q16_ONE));
        int32_t bi = (int32_t)b;
        return (bi & 1) ? -mag : mag;
    }

    if (a == 0.0f) {
        if (b > 0.0f) return 0.0f;
        if (b == 0.0f) return 1.0f;
        return 3.402823466e+38f; // +inf-ish
    }

    int32_t log2a_q16 = fm_log2_q16(a);
    if (log2a_q16 == INT32_MIN) return fm_nan_f();
    float log2a = (float)log2a_q16 / (float)FM_Q16_ONE;
    return fm_exp2_f(b * log2a);
}

// --- Lua bindings ---

static int l_fast_sin(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    lua_pushnumber(L, (lua_Number)fm_sin_f(x));
    return 1;
}

static int l_fast_cos(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    lua_pushnumber(L, (lua_Number)fm_cos_f(x));
    return 1;
}

static int l_fast_atan(lua_State *L) {
    float y = (float)luaL_checknumber(L, 1);
    float x = (float)luaL_optnumber(L, 2, 1.0);
    lua_pushnumber(L, (lua_Number)fm_atan2_f(y, x));
    return 1;
}

static int l_fast_exp(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    lua_pushnumber(L, (lua_Number)fm_exp_f(x));
    return 1;
}

static int l_fast_log(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    if (lua_isnoneornil(L, 2)) {
        lua_pushnumber(L, (lua_Number)fm_log_f(x));
        return 1;
    }

    float base = (float)luaL_checknumber(L, 2);
    if (base == 2.0f) {
        int32_t v = fm_log2_q16(x);
        lua_pushnumber(L, (lua_Number)((v == INT32_MIN) ? fm_nan_f() : ((float)v / (float)FM_Q16_ONE)));
        return 1;
    }
    if (base == 10.0f) {
        // log10(x) = log2(x)/log2(10)
        const float inv_log2_10 = 0.3010299956639812f;
        int32_t v = fm_log2_q16(x);
        lua_pushnumber(L, (lua_Number)((v == INT32_MIN) ? fm_nan_f() : (((float)v / (float)FM_Q16_ONE) * inv_log2_10)));
        return 1;
    }

    float ln_x = fm_log_f(x);
    float ln_b = fm_log_f(base);
    lua_pushnumber(L, (lua_Number)(ln_x / ln_b));
    return 1;
}

static int l_fast_pow(lua_State *L) {
    float a = (float)luaL_checknumber(L, 1);
    float b = (float)luaL_checknumber(L, 2);
    lua_pushnumber(L, (lua_Number)fm_pow_f(a, b));
    return 1;
}

int fastmath_lua_install(lua_State *L, int patch_math_table) {
    static const luaL_Reg lib[] = {
        {"sin", l_fast_sin},
        {"cos", l_fast_cos},
        {"atan", l_fast_atan},
        {"exp", l_fast_exp},
        {"log", l_fast_log},
        {"pow", l_fast_pow},
        {NULL, NULL},
    };

    luaL_newlib(L, lib);
    lua_setglobal(L, "fastmath");

    if (!patch_math_table) return 0;

    lua_getglobal(L, "math");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }

    // Idempotency guard: if math.ssin already exists, assume we've patched.
    lua_getfield(L, -1, "ssin");
    int already = lua_isfunction(L, -1);
    lua_pop(L, 1);
    if (already) {
        lua_pop(L, 1);
        return 0;
    }

    // Preserve originals: math.ssin, math.scos, ...
    const char *names[] = {"sin", "cos", "atan", "exp", "log", "pow"};
    const char *save_names[] = {"ssin", "scos", "satan", "sexp", "slog", "spow"};

    lua_getglobal(L, "fastmath");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }

    for (int i = 0; i < 6; i++) {
        // save original
        lua_getfield(L, -2, names[i]);      // math[name]
        lua_setfield(L, -2, save_names[i]); // math[save]=orig (pops)

        // overwrite with fast
        lua_getfield(L, -1, names[i]);      // fastmath[name]
        lua_setfield(L, -3, names[i]);      // math[name]=fast (pops)
    }

    lua_pop(L, 2); // fastmath, math
    return 0;
}
