#include "StateSpaceBlock.h"

#include <algorithm>
#include <cmath>

#if defined(__ARM_NEON) || defined(__aarch64__)
 #include <arm_neon.h>
 #define SSB_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <emmintrin.h>
 #define SSB_SSE2 1
#endif

namespace ar::fx
{
#if defined(SSB_NEON) || defined(SSB_SSE2)
namespace
{
// Two doubles per vector: the two channels of a stereo pair, or two rows of the one-channel product.
#if defined(SSB_NEON)
using V2 = float64x2_t;
inline V2 load (const double* p) noexcept { return vld1q_f64 (p); }
inline void store (double* p, V2 v) noexcept { vst1q_f64 (p, v); }
inline V2 splat (double x) noexcept { return vdupq_n_f64 (x); }
inline V2 add (V2 a, V2 b) noexcept { return vaddq_f64 (a, b); }
inline V2 mul (V2 a, V2 b) noexcept { return vmulq_f64 (a, b); }
inline V2 fmaScalar (V2 acc, V2 v, double s) noexcept { return vfmaq_n_f64 (acc, v, s); }
inline V2 fma (V2 acc, V2 a, V2 b) noexcept { return vfmaq_f64 (acc, a, b); }
inline V2 lane0 (V2 v) noexcept { return vdupq_laneq_f64 (v, 0); }
inline V2 lane1 (V2 v) noexcept { return vdupq_laneq_f64 (v, 1); }
inline double get0 (V2 v) noexcept { return vgetq_lane_f64 (v, 0); }
inline double get1 (V2 v) noexcept { return vgetq_lane_f64 (v, 1); }
#else
using V2 = __m128d;
inline V2 load (const double* p) noexcept { return _mm_loadu_pd (p); }
inline void store (double* p, V2 v) noexcept { _mm_storeu_pd (p, v); }
inline V2 splat (double x) noexcept { return _mm_set1_pd (x); }
inline V2 add (V2 a, V2 b) noexcept { return _mm_add_pd (a, b); }
inline V2 mul (V2 a, V2 b) noexcept { return _mm_mul_pd (a, b); }
inline V2 fmaScalar (V2 acc, V2 v, double s) noexcept { return _mm_add_pd (acc, _mm_mul_pd (v, _mm_set1_pd (s))); }
inline V2 fma (V2 acc, V2 a, V2 b) noexcept { return _mm_add_pd (acc, _mm_mul_pd (a, b)); }
inline V2 lane0 (V2 v) noexcept { return _mm_unpacklo_pd (v, v); }
inline V2 lane1 (V2 v) noexcept { return _mm_unpackhi_pd (v, v); }
inline double get0 (V2 v) noexcept { return _mm_cvtsd_f64 (v); }
inline double get1 (V2 v) noexcept { return _mm_cvtsd_f64 (_mm_unpackhi_pd (v, v)); }
#endif

/** One channel: the n + 1 rows of [A b; c d] [s; u] as pairs of rows, a column of pairs per state.
    The state stays in registers across the call. */
template <int N>
void runMono (const double* in, double* out, int count, const double* m, double* now, double* before) noexcept
{
    constexpr int P = (N + 2) / 2, ROWS = 2 * P;
    V2 acc[P], prev[P];
    for (int p = 0; p < P; ++p)
        acc[p] = prev[p] = load (now + 2 * p);
    for (int t = 0; t < count; ++t)
    {
        const V2 u = splat (in[t]);
        V2 a0[P], a1[P];
        const double* col = m + N * ROWS;
        for (int p = 0; p < P; ++p)
        {
            a0[p] = mul (load (col + 2 * p), u);
            a1[p] = splat (0.0);
        }
        for (int j = 0; j < N; ++j)
        {
            col = m + j * ROWS;
            const V2 sj = (j & 1) ? lane1 (acc[j >> 1]) : lane0 (acc[j >> 1]);
            for (int p = 0; p < P; ++p)
            {
                if (j & 1) a1[p] = fma (a1[p], load (col + 2 * p), sj);
                else       a0[p] = fma (a0[p], load (col + 2 * p), sj);
            }
        }
        for (int p = 0; p < P; ++p)
        {
            prev[p] = acc[p];
            acc[p] = add (a0[p], a1[p]);
        }
        out[t] = (N & 1) ? get1 (acc[N >> 1]) : get0 (acc[N >> 1]);
    }
    for (int p = 0; p < P; ++p)
    {
        store (now + 2 * p, acc[p]);
        store (before + 2 * p, prev[p]);
    }
}

/** Two channels, one vector per state; the matrices are scalars shared by both. */
template <int N>
void runStereo (const double* in, double* out, int count, const double* a, const double* b, const double* c, double d,
                double* now, double* before) noexcept
{
    V2 s[N], prev[N];
    for (int i = 0; i < N; ++i)
        s[i] = prev[i] = load (now + 2 * i);
    for (int t = 0; t < count; ++t)
    {
        const V2 u = load (in + 2 * t);
        V2 next[N];
        for (int i = 0; i < N; ++i)
        {
            V2 x0 = mul (u, splat (b[i])), x1 = splat (0.0);
            for (int j = 0; j < N; ++j)
            {
                if (j & 1) x1 = fmaScalar (x1, s[j], a[i * N + j]);
                else       x0 = fmaScalar (x0, s[j], a[i * N + j]);
            }
            next[i] = add (x0, x1);
        }
        V2 y0 = mul (u, splat (d)), y1 = splat (0.0);
        for (int j = 0; j < N; ++j)
        {
            if (j & 1) y1 = fmaScalar (y1, s[j], c[j]);
            else       y0 = fmaScalar (y0, s[j], c[j]);
        }
        store (out + 2 * t, add (y0, y1));
        for (int i = 0; i < N; ++i)
        {
            prev[i] = s[i];
            s[i] = next[i];
        }
    }
    for (int i = 0; i < N; ++i)
    {
        store (now + 2 * i, s[i]);
        store (before + 2 * i, prev[i]);
    }
}

constexpr int kMaxUnrolled = 14;
} // namespace
#endif

void StateSpaceBlock::prepare (int maxStates, int numLanes)
{
    lanes = numLanes == 2 ? 2 : 1;
    n = 0;
    rows = ((maxStates + 1) + 1) & ~1;
    m.assign ((size_t) rows * (size_t) (maxStates + 1), 0.0);
    a2.assign ((size_t) maxStates * (size_t) maxStates, 0.0);
    b2.assign ((size_t) maxStates, 0.0);
    c2.assign ((size_t) maxStates, 0.0);
    now.assign ((size_t) rows * (size_t) lanes, 0.0);
    before = now;
    g.assign ((size_t) maxStates, 0.0);
}

void StateSpaceBlock::reset() noexcept
{
    std::fill (now.begin(), now.end(), 0.0);
    std::fill (before.begin(), before.end(), 0.0);
}

void StateSpaceBlock::setModel (const circuit::StateSpace& s) noexcept
{
    const int newN = s.n;
    const int newRows = ((newN + 1) + 1) & ~1;
    if (n != 0 && n == newN)
    {
        // Carry the capacitors' voltage and current over a change of companion conductance:
        // hist' = hist + (g - g') v, with v the voltage of the last step, -(hist_{n+1} + hist_n) / 2g.
        for (int k = 0; k < n; ++k)
        {
            if (g[(size_t) k] == s.g[(size_t) k])
                continue;
            for (int l = 0; l < lanes; ++l)
            {
                const size_t at = (size_t) k * (size_t) lanes + (size_t) l;
                const double v = -(now[at] + before[at]) / (2.0 * g[(size_t) k]);
                now[at] += (g[(size_t) k] - s.g[(size_t) k]) * v;
                before[at] = -2.0 * s.g[(size_t) k] * v - now[at];
            }
        }
    }
    n = newN;
    rows = newRows;
    std::copy (s.g.begin(), s.g.end(), g.begin());
    std::copy (s.A.begin(), s.A.end(), a2.begin());
    std::copy (s.B.begin(), s.B.end(), b2.begin());
    std::copy (s.C.begin(), s.C.end(), c2.begin());
    d2 = s.D;
    // Column-major with the augmented row (the output) after the n state rows.
    std::fill (m.begin(), m.end(), 0.0);
    for (int j = 0; j < n; ++j)
    {
        double* col = &m[(size_t) j * (size_t) rows];
        for (int i = 0; i < n; ++i)
            col[i] = s.A[(size_t) i * (size_t) n + (size_t) j];
        col[n] = s.C[(size_t) j];
    }
    double* col = &m[(size_t) n * (size_t) rows];
    for (int i = 0; i < n; ++i)
        col[i] = s.B[(size_t) i];
    col[n] = s.D;
}

template <int L>
void StateSpaceBlock::runGeneric (const double* in, double* out, int numSamples) noexcept
{
    const int nn = n;
    for (int t = 0; t < numSamples; ++t)
    {
        for (int l = 0; l < L; ++l)
        {
            double next[64], y = d2 * in[t * L + l];
            for (int i = 0; i < nn; ++i)
            {
                double acc = b2[(size_t) i] * in[t * L + l];
                for (int j = 0; j < nn; ++j)
                    acc += a2[(size_t) i * (size_t) nn + (size_t) j] * now[(size_t) j * L + (size_t) l];
                next[i] = acc;
            }
            for (int j = 0; j < nn; ++j)
                y += c2[(size_t) j] * now[(size_t) j * L + (size_t) l];
            out[t * L + l] = y;
            for (int i = 0; i < nn; ++i)
            {
                before[(size_t) i * L + (size_t) l] = now[(size_t) i * L + (size_t) l];
                now[(size_t) i * L + (size_t) l] = next[i];
            }
        }
    }
}

void StateSpaceBlock::process (const double* in, double* out, int numSamples) noexcept
{
#if defined(SSB_NEON) || defined(SSB_SSE2)
    if (n >= 1 && n <= kMaxUnrolled)
    {
        // The count of states is fixed per network, so the loops are unrolled for it.
        if (lanes == 1)
        {
            switch (n)
            {
#define SSB_MONO(K) case K: runMono<K> (in, out, numSamples, m.data(), now.data(), before.data()); return;
                SSB_MONO (1) SSB_MONO (2) SSB_MONO (3) SSB_MONO (4) SSB_MONO (5) SSB_MONO (6) SSB_MONO (7)
                SSB_MONO (8) SSB_MONO (9) SSB_MONO (10) SSB_MONO (11) SSB_MONO (12) SSB_MONO (13) SSB_MONO (14)
#undef SSB_MONO
                default: break;
            }
        }
        else
        {
            switch (n)
            {
#define SSB_STEREO(K) case K: runStereo<K> (in, out, numSamples, a2.data(), b2.data(), c2.data(), d2, now.data(), before.data()); return;
                SSB_STEREO (1) SSB_STEREO (2) SSB_STEREO (3) SSB_STEREO (4) SSB_STEREO (5) SSB_STEREO (6) SSB_STEREO (7)
                SSB_STEREO (8) SSB_STEREO (9) SSB_STEREO (10) SSB_STEREO (11) SSB_STEREO (12) SSB_STEREO (13) SSB_STEREO (14)
#undef SSB_STEREO
                default: break;
            }
        }
    }
#endif
    if (lanes == 2) runGeneric<2> (in, out, numSamples);
    else            runGeneric<1> (in, out, numSamples);
}

void StateSpaceBlock::flushDenormals() noexcept
{
    for (auto* v : { &now, &before })
        for (int i = 0; i < n * lanes; ++i)
            if (std::abs ((*v)[(size_t) i]) < 1.0e-30)
                (*v)[(size_t) i] = 0.0;
}
} // namespace ar::fx
