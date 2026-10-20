// =============================================================================
//  FOG2 (Studio Wiga) - polyphonic octave engine
//
//  How it works (same family of algorithms as the EHX POG / Sub'n'Up pedals):
//   * the mono input is split by a bank of ~150 narrow complex band-pass filters
//     (log spaced, ~2 bands per semitone above 170 Hz, 5 Hz apart below).
//   * every band gives an analytic signal  z = a . e^(i.phi)
//       +1 oct  ->  a . cos(2 phi)        (phase doubled)
//       +2 oct  ->  a . cos(4 phi)
//       -1 oct  ->  a . cos(phi / 2)      (half-phase phasor, tracked sample by sample)
//       -2 oct  ->  a . cos(phi / 4)
//   * only the bands that are a local spectral peak are allowed to speak, so each
//     partial of each string gives exactly one shifted partial -> true polyphony,
//     no phasing between neighbouring bands.
//   * ATTACK is applied per band: every new note swells in on its own.
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

struct BankConfig
{
    float q = 30.0f, bmin = 12.0f, spacing = 0.6f;   // band = max(bmin, f/q) ; step = spacing * band
    int   stages = 3;
};
inline BankConfig& bankConfig() { static BankConfig c; return c; }

class OctaveBank
{
public:
    static float bandwidth (float f) { return std::max (bankConfig().bmin, f / bankConfig().q); }   // combined -3 dB bandwidth
    static std::vector<float> allCentres()
    {
        std::vector<float> c; float f = 35.0f;
        while (f < 6000.0f) { c.push_back (f); f += bankConfig().spacing * bandwidth (f); }
        return c;
    }
    // owns the bands [fLo, fHi); 3 guard bands each side are computed only to judge peaks
    void prepare (double sampleRate, float fLo = 0.0f, float fHi = 1.0e9f)
    {
        fs = (float) sampleRate;
        centres.clear(); owned.clear();
        const auto all = allCentres();
        int first = -1, last = -1;
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i] >= fLo && all[(size_t) i] < fHi && all[(size_t) i] < 0.40f * fs) { if (first < 0) first = i; last = i; }
        for (int i = std::max (0, first - 3); first >= 0 && i <= std::min ((int) all.size() - 1, last + 3); ++i)
        {
            if (all[(size_t) i] >= 0.42f * fs) break;
            centres.push_back (all[(size_t) i]);
            owned.push_back (i >= first && i <= last ? 1.0f : 0.0f);
        }
        N = (int) centres.size();
        auto rs = [this] (std::vector<float>& v, float val = 0.0f) { v.assign ((size_t) N + 2, val); };
        rs (pr); rs (pi); rs (cg);
        rs (y1r); rs (y1i); rs (y2r); rs (y2i); rs (y3r); rs (y3i);
        stages = bankConfig().stages;
        rs (upr, 1.0f); rs (upi); rs (wr, 1.0f); rs (wi); rs (qr, 1.0f); rs (qi);
        rs (amp); rs (slow); rs (gate); rs (env, 1.0f); rs (retrig);
        rs (vU1); rs (vU2); rs (vD1); rs (vD2); rs (wUp1); rs (wUp2); rs (onsetHold);

        for (int k = 0; k < N; ++k)
        {
            const float fc = centres[(size_t) k];
            // -3 dB bandwidth of one stage so that the cascade has bandwidth(fc)
            const float bw = bandwidth (fc) / std::sqrt (std::pow (2.0f, 1.0f / (float) stages) - 1.0f);
            const float r  = std::exp (-juce_pi_f * bw / fs);
            const float w  = 2.0f * juce_pi_f * fc / fs;
            pr[(size_t) k] = r * std::cos (w);
            pi[(size_t) k] = r * std::sin (w);
            cg[(size_t) k] = 1.0f - r;
            // anti-alias: the shifted partial must stay below ~0.42 fs
            wUp1[(size_t) k] = taper (2.0f * fc, 0.36f * fs, 0.44f * fs);
            wUp2[(size_t) k] = taper (4.0f * fc, 0.36f * fs, 0.44f * fs);
        }
        gateUp   = 1.0f - std::exp (-1.0f / (0.004f * fs));
        gateDown = 1.0f - std::exp (-1.0f / (0.006f * fs));
        slowCoef = 1.0f - std::exp (-1.0f / (0.060f * fs));
        duckStep = 1.0f / (0.003f * fs);
        reset();
    }

    void reset()
    {
        for (int k = 0; k < N + 2; ++k)
        {
            const size_t i = (size_t) k;
            y1r[i] = y1i[i] = y2r[i] = y2i[i] = y3r[i] = y3i[i] = 0.0f;
            upr[i] = 1.0f; upi[i] = 0.0f; wr[i] = 1.0f; wi[i] = 0.0f; qr[i] = 1.0f; qi[i] = 0.0f;
            amp[i] = slow[i] = gate[i] = 0.0f; env[i] = 1.0f; retrig[i] = 0.0f; onsetHold[i] = 0.0f;
        }
    }

    // attackSeconds <= 0  ->  no swell
    void process (const float* in, int numSamples, float attackSeconds,
                  float* outM2, float* outM1, float* outP1, float* outP2)
    {
        const bool  useAttack = attackSeconds > 0.0005f;
        const float attStep   = useAttack ? 1.0f / (attackSeconds * fs) : 1.0f;
        const float floorAmp  = 2.0e-5f;     // ~ -94 dBFS band level: below = silence
        const int   holdLen   = (int) (0.08f * fs);

        for (int n = 0; n < numSamples; ++n)
        {
            const float x = in[n];

            // ---- 1. filter bank + phase work (independent per band -> vectorisable) ----
            for (int k = 0; k < N; ++k)
            {
                const size_t i = (size_t) k;
                const float p_r = pr[i], p_i = pi[i], c = cg[i];
                const float a1r = c * x + p_r * y1r[i] - p_i * y1i[i];
                const float a1i =         p_r * y1i[i] + p_i * y1r[i];
                y1r[i] = a1r; y1i[i] = a1i;
                const float a2r = c * a1r + p_r * y2r[i] - p_i * y2i[i];
                const float a2i = c * a1i + p_r * y2i[i] + p_i * y2r[i];
                y2r[i] = a2r; y2i[i] = a2i;
                float zr = a2r, zi = a2i;
                if (stages > 2)
                {
                    zr = c * a2r + p_r * y3r[i] - p_i * y3i[i];
                    zi = c * a2i + p_r * y3i[i] + p_i * y3r[i];
                    y3r[i] = zr; y3i[i] = zi;
                }

                const float mag = std::sqrt (zr * zr + zi * zi) + 1.0e-20f;
                const float ur = zr / mag, ui = zi / mag;
                amp[i] = mag;

                // rotation since last sample  d = u . conj(u_prev)
                const float dr = ur * upr[i] + ui * upi[i];
                const float di = ui * upr[i] - ur * upi[i];
                upr[i] = ur; upi[i] = ui;

                // half rotation  h = sqrt(d)  (d is close to 1 -> principal root is the right one)
                float hr = 1.0f + dr, hi = di;
                float hn = hr * hr + hi * hi;
                if (hn < 1.0e-12f) { hr = 0.0f; hi = 1.0f; hn = 1.0f; }
                hn = 1.0f / std::sqrt (hn); hr *= hn; hi *= hn;
                // quarter rotation
                float qrr = 1.0f + hr, qri = hi;
                float qn = 1.0f / std::sqrt (qrr * qrr + qri * qri + 1.0e-20f);
                qrr *= qn; qri *= qn;

                // advance the sub-octave phasors and keep them on the unit circle
                float nwr = wr[i] * hr - wi[i] * hi, nwi = wr[i] * hi + wi[i] * hr;
                float g1 = 1.5f - 0.5f * (nwr * nwr + nwi * nwi);
                wr[i] = nwr * g1; wi[i] = nwi * g1;
                float nqr = qr[i] * qrr - qi[i] * qri, nqi = qr[i] * qri + qi[i] * qrr;
                float g2 = 1.5f - 0.5f * (nqr * nqr + nqi * nqi);
                qr[i] = nqr * g2; qi[i] = nqi * g2;

                const float c2 = ur * ur - ui * ui, s2 = 2.0f * ur * ui;   // e^(i2phi)
                vU1[i] = c2;
                vU2[i] = c2 * c2 - s2 * s2;                                 // Re e^(i4phi)
                vD1[i] = wr[i];
                vD2[i] = qr[i];
            }

            // ---- 2. peak picking, gating, per-band attack, summing ----
            float sM2 = 0, sM1 = 0, sP1 = 0, sP2 = 0;
            for (int k = 0; k < N; ++k)
            {
                const size_t i = (size_t) k;
                const float a  = amp[i];
                const float al = k > 0     ? amp[i - 1] : 0.0f;
                const float ar = k < N - 1 ? amp[i + 1] : 0.0f;
                const bool peak = a > al && a >= ar && a > floorAmp;

                gate[i] += ((peak ? 1.0f : 0.0f) - gate[i]) * (peak ? gateUp : gateDown);

                // silent bands silently line their sub-octave phasors up with the louder
                // neighbour, so a note that glides from one band to the next stays coherent
                if (gate[i] < 0.02f)
                {
                    const size_t j = (al > ar) ? (k > 0 ? i - 1 : i) : (k < N - 1 ? i + 1 : i);
                    if (j != i)
                    {
                        if (wr[i] * wr[j] + wi[i] * wi[j] < 0.0f) { wr[i] = -wr[i]; wi[i] = -wi[i]; }
                        const float d0 =  qr[i] * qr[j] + qi[i] * qi[j];
                        const float d1 = -qi[i] * qr[j] + qr[i] * qi[j];
                        if (std::abs (d1) > std::abs (d0))
                        {   // rotate by +-90 deg
                            const float tr = qr[i];
                            if (d1 > 0) { qr[i] = -qi[i]; qi[i] = tr; } else { qr[i] = qi[i]; qi[i] = -tr; }
                        }
                        else if (d0 < 0.0f) { qr[i] = -qr[i]; qi[i] = -qi[i]; }
                        vD1[i] = wr[i]; vD2[i] = qr[i];
                    }
                }

                // per band onset detection -> attack swell
                slow[i] += (a - slow[i]) * slowCoef;
                if (useAttack)
                {
                    if (onsetHold[i] > 0.0f) onsetHold[i] -= 1.0f;
                    else if (a > 1.9f * slow[i] + floorAmp && a > 4.0f * floorAmp)
                    {
                        retrig[i] = 1.0f; onsetHold[i] = (float) holdLen;
                    }
                    if (retrig[i] > 0.0f)
                    {
                        env[i] -= duckStep;
                        if (env[i] <= 0.0f) { env[i] = 0.0f; retrig[i] = 0.0f; }
                    }
                    else env[i] = std::min (1.0f, env[i] + attStep);
                }
                else env[i] = 1.0f;

                const float A = gate[i] * env[i] * a * 2.25f * owned[i];   // complex band = half the real amplitude
                if (A <= 0.0f) continue;
                sM2 += A * vD2[i];
                sM1 += A * vD1[i];
                sP1 += A * vU1[i] * wUp1[i];
                sP2 += A * vU2[i] * wUp2[i];
            }
            outM2[n] = sM2; outM1[n] = sM1; outP1[n] = sP1; outP2[n] = sP2;
        }
    }

    int getNumBands() const { return N; }

private:
    static constexpr float juce_pi_f = 3.14159265358979f;
    static float taper (float f, float lo, float hi)
    {
        if (f <= lo) return 1.0f;
        if (f >= hi) return 0.0f;
        return 0.5f + 0.5f * std::cos (juce_pi_f * (f - lo) / (hi - lo));
    }

    float fs = 48000.0f;
    int N = 0;
    std::vector<float> centres, owned;
    std::vector<float> pr, pi, cg, y1r, y1i, y2r, y2i, y3r, y3i, upr, upi, wr, wi, qr, qi;
    int stages = 2;
    std::vector<float> amp, slow, gate, env, retrig, vU1, vU2, vD1, vD2, wUp1, wUp2, onsetHold;
    float gateUp = 0.01f, gateDown = 0.01f, slowCoef = 0.001f, duckStep = 0.01f;
};

// -----------------------------------------------------------------------------
//  2-pole resonant low-pass (TPT state variable filter), stereo
// -----------------------------------------------------------------------------
struct ResonantLP
{
    void prepare (double sr) { fs = (float) sr; reset(); }
    void reset() { for (auto& s : ic1) s = 0; for (auto& s : ic2) s = 0; }
    void setParams (float cutoffHz, float q)
    {
        cutoffHz = std::min (cutoffHz, 0.45f * fs);
        g = std::tan (3.14159265f * cutoffHz / fs);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process (int ch, float v0)
    {
        const float v3 = v0 - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        return v2;
    }
    float fs = 48000.0f, g = 0.5f, k = 1.4f, a1 = 0, a2 = 0, a3 = 0;
    float ic1[2] {}, ic2[2] {};
};

// -----------------------------------------------------------------------------
//  Detune: modulated short delay (rate AND depth rise with the fader, like the POG2)
//  The two outputs use LFOs 90 deg apart -> wide, Leslie-like shimmer.
// -----------------------------------------------------------------------------
struct Detuner
{
    void prepare (double sr)
    {
        fs = (float) sr;
        size = (int) (0.05f * fs) + 4;
        buf.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); wp = 0; phase = 0; }

    // amount 0..1 ; writes left/right
    void process (float in, float amount, float& outL, float& outR)
    {
        buf[(size_t) wp] = in;
        if (amount < 0.005f) { outL = outR = in; advance(); return; }
        // low settings = slow chorus (12-string), high settings = fast rotary / vibrato.
        // speed and depth both rise, the pitch swing stays musical (about +-0.5 semitone max)
        const float rate  = 0.15f + 6.5f * amount * amount;                       // 0.15 .. 6.65 Hz
        const float depth = (0.40f + 3.0f * amount * (1.0f - 0.65f * amount)) * 0.001f * fs;
        const float base  = 0.006f * fs;
        phase += rate / fs; if (phase >= 1.0f) phase -= 1.0f;
        const float l = std::sin (6.2831853f * phase);
        const float r = std::sin (6.2831853f * (phase + 0.25f));
        const float dl = read (base + depth * (0.5f + 0.5f * l));
        const float dr = read (base + depth * (0.5f + 0.5f * r));
        const float wet = std::min (1.0f, amount * 3.0f);                                  // chorus amount
        const float vib = std::min (0.85f, std::max (0.0f, (amount - 0.55f) / 0.35f) * 0.85f); // pure pitch wobble
        const float cl = in * (1.0f - 0.5f * wet) + dl * 0.62f * wet;
        const float cr = in * (1.0f - 0.5f * wet) + dr * 0.62f * wet;
        outL = cl * (1.0f - vib) + dl * vib;
        outR = cr * (1.0f - vib) + dr * vib;
        advance();
    }
private:
    void advance() { if (++wp >= size) wp = 0; }
    float read (float delaySamples)
    {
        float rp = (float) wp - delaySamples;
        while (rp < 0) rp += (float) size;
        const int i0 = (int) rp; const float fr = rp - (float) i0;
        const int im1 = (i0 - 1 + size) % size, i1 = (i0 + 1) % size, i2 = (i0 + 2) % size;
        const float y0 = buf[(size_t) im1], y1 = buf[(size_t) i0], y2 = buf[(size_t) i1], y3 = buf[(size_t) i2];
        // 4-point Hermite
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + y1;
    }
    std::vector<float> buf; int size = 0, wp = 0; float fs = 48000.0f, phase = 0.0f;
};

// -----------------------------------------------------------------------------
//  One bank running at fs / D, with its own anti-alias decimator / interpolator
// -----------------------------------------------------------------------------
class DecimatedBank
{
public:
    void prepare (double sampleRate, int factor, int maxBlock, float fLo, float fHi)
    {
        D = factor;
        bank.prepare (sampleRate / D, fLo, fHi);
        if (D > 1)
        {
            const int nTaps = 16 * D + 1;
            taps.assign ((size_t) nTaps, 0.0f);
            const double fc = 0.45 / D, PI = 3.14159265358979;
            double sum = 0;
            for (int i = 0; i < nTaps; ++i)
            {
                const double m = i - (nTaps - 1) * 0.5;
                const double sinc = m == 0 ? 2 * fc : std::sin (2 * PI * fc * m) / (PI * m);
                const double win = 0.42 - 0.5 * std::cos (2 * PI * i / (nTaps - 1)) + 0.08 * std::cos (4 * PI * i / (nTaps - 1));
                taps[(size_t) i] = (float) (sinc * win); sum += sinc * win;
            }
            for (auto& t : taps) t = (float) (t / sum);
        }
        hin.assign (taps.size() + 1, 0.0f);
        for (auto& h : hout) h.assign (taps.size() + 1, 0.0f);
        const size_t lo = (size_t) (maxBlock / D + 2);
        lowIn.assign (lo, 0.0f);
        for (auto& v : lowOut) v.assign (lo, 0.0f);
        reset();
    }
    void reset()
    {
        bank.reset(); std::fill (hin.begin(), hin.end(), 0.0f);
        for (auto& h : hout) std::fill (h.begin(), h.end(), 0.0f);
        phase = hp = ophase = ohp = 0;
    }
    int latency() const { return D > 1 ? (int) taps.size() - 1 : 0; }   // in full-rate samples

    // ADDS its four voices into out[0..3]
    void process (const float* in, int n, float attackSec, float* const* out, float* scratch[4])
    {
        if (D == 1)
        {
            bank.process (in, n, attackSec, scratch[0], scratch[1], scratch[2], scratch[3]);
            for (int v = 0; v < 4; ++v) for (int i = 0; i < n; ++i) out[v][i] += scratch[v][i];
            return;
        }
        const int T = (int) taps.size();
        int m = 0;
        for (int i = 0; i < n; ++i)
        {
            hin[(size_t) hp] = in[i];
            if (phase == 0)
            {
                float acc = 0; int idx = hp;
                for (int t = 0; t < T; ++t) { acc += taps[(size_t) t] * hin[(size_t) idx]; if (--idx < 0) idx = T - 1; }
                lowIn[(size_t) m++] = acc;
            }
            if (++hp >= T) hp = 0;
            if (++phase >= D) phase = 0;
        }
        bank.process (lowIn.data(), m, attackSec, lowOut[0].data(), lowOut[1].data(), lowOut[2].data(), lowOut[3].data());
        int li = 0;
        for (int i = 0; i < n; ++i)
        {
            const bool fresh = (ophase == 0) && li < m;
            for (int v = 0; v < 4; ++v)
            {
                auto& h = hout[(size_t) v];
                h[(size_t) ohp] = fresh ? lowOut[(size_t) v][(size_t) li] * (float) D : 0.0f;
                // only every D-th tap meets a non-zero sample (polyphase)
                float acc = 0;
                int idx = ohp, t = 0;
                // find the first tap that lines up with a stuffed (non-zero) sample
                const int off = ophase;   // samples since last fresh value
                idx = ohp - off; if (idx < 0) idx += T; t = off;
                for (; t < T; t += D) { acc += taps[(size_t) t] * h[(size_t) idx]; idx -= D; if (idx < 0) idx += T; }
                out[v][i] += acc;
            }
            if (ophase == 0 && li < m) ++li;
            if (++ohp >= T) ohp = 0;
            if (++ophase >= D) ophase = 0;
        }
    }
private:
    OctaveBank bank;
    int D = 1, phase = 0, hp = 0, ophase = 0, ohp = 0;
    std::vector<float> taps, hin, lowIn;
    std::vector<float> hout[4], lowOut[4];
};

// -----------------------------------------------------------------------------
//  Two-rate engine: bands under 1 kHz run at ~12 kHz, the others at <= 48 kHz.
//  The faster path is delayed so both arrive together.
// -----------------------------------------------------------------------------
class OctaveEngine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        const int dh = sampleRate >= 160000.0 ? 4 : (sampleRate >= 80000.0 ? 2 : 1);
        const int dl = dh * 4;
        constexpr float split = 1000.0f;
        low.prepare  (sampleRate, dl, maxBlock, 0.0f, split);
        high.prepare (sampleRate, dh, maxBlock, split, 1.0e9f);
        comp = low.latency() - high.latency();
        for (auto& d : delay) d.assign ((size_t) comp + 1, 0.0f);
        for (auto& s : scratch) s.assign ((size_t) maxBlock + 8, 0.0f);
        for (auto& s : hiOut)   s.assign ((size_t) maxBlock + 8, 0.0f);
        dp = 0;
    }
    void reset() { low.reset(); high.reset(); for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f); dp = 0; }
    int latency() const { return low.latency(); }

    // out[0..3] = -2, -1, +1, +2   (overwritten)
    void process (const float* in, int n, float attackSec, float* const* out)
    {
        if ((size_t) n > scratch[0].size()) n = (int) scratch[0].size();   // never happens (prepare with maxBlock)
        float* sc[4] = { scratch[0].data(), scratch[1].data(), scratch[2].data(), scratch[3].data() };
        float* ho[4] = { hiOut[0].data(), hiOut[1].data(), hiOut[2].data(), hiOut[3].data() };
        for (int v = 0; v < 4; ++v) { std::fill (out[v], out[v] + n, 0.0f); std::fill (ho[v], ho[v] + n, 0.0f); }
        low.process (in, n, attackSec, out, sc);
        high.process (in, n, attackSec, ho, sc);
        const int L = comp + 1;
        for (int i = 0; i < n; ++i)
        {
            for (int v = 0; v < 4; ++v)
            {
                auto& d = delay[(size_t) v];
                d[(size_t) dp] = ho[v][i];
                const int rp = (dp + 1) % L;            // oldest sample = comp samples ago
                out[v][i] += comp > 0 ? d[(size_t) rp] : ho[v][i];
            }
            if (++dp >= L) dp = 0;
        }
    }
private:
    DecimatedBank low, high;
    int comp = 0, dp = 0;
    std::vector<float> delay[4], scratch[4], hiOut[4];
};
