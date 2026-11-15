#pragma once
// DOMINATRIX - master bus glue compressor engine.
//
// Signal flow (per sample):
//   ANALOG input stage  (tape transport wow/flutter, head bump + HF roll-off,
//                        L/R mismatch, crosstalk, hiss + hum, slow component drift)
//      |-- dry copy -----------------------------------------------+
//   AUTO GLUE COMPRESSOR (no user threshold/ratio/attack/release:  |
//                        DISCIPLINE sets everything, see below)    |
//   ---- oversampled from here (dry and wet both, so MIX never combs) ----
//   SATURATION (TAPE / TUBE / IRON / CONSOLE / WHIP)               |
//   MIX  <---------------------------------------------------------+
//   OUTPUT gain -> SOFT CLIPPER at the CEILING (-1 dB / -6 dB / OFF)
//   ---- back to base rate ----
//   safety clamp at the ceiling, loudness meters, MATCH (level-matched listening)
//
// The automatic compressor:
//   * sidechain high-passed at 90 Hz, stereo linked, so the low end doesn't pump
//   * threshold follows the mix's own level (slow program-level tracker), so
//     DISCIPLINE works the same on a quiet or a hot mix
//   * ratio and threshold offset both come from DISCIPLINE
//   * attack is program dependent: punchy material (high crest factor) gets a
//     slower attack so transients survive; dense material gets a faster one
//   * release is dual-stage auto: short bursts recover fast, sustained
//     compression recovers slowly (no pumping)
//   * makeup gain is automatic
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>

namespace dom
{
enum SatType { tape = 0, tube, iron, console, whip, numSatTypes };
enum Ceiling { ceilMinus1 = 0, ceilMinus6 = 1, ceilOff = 2 };

inline float ceilingDbFor (int c) { return c == ceilMinus1 ? -1.0f : (c == ceilMinus6 ? -6.0f : 0.0f); }

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float xf) noexcept
    {
        const double x = xf, y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
    void setRaw (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    enum Kind { lowpass, highpass, peak, lowShelf, highShelf };
    // RBJ cookbook
    void design (Kind k, double sr, double f, double q, double gainDb = 0.0)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, sr * 0.49, f) / sr;
        const double cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q), sa = 2.0 * std::sqrt (A) * al;
        switch (k)
        {
            case lowpass:   setRaw ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al); break;
            case highpass:  setRaw ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al); break;
            case peak:      setRaw (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A); break;
            case lowShelf:  setRaw (A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sa),
                                    (A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa); break;
            case highShelf: setRaw (A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sa),
                                    (A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa); break;
        }
    }
};

// ITU-R BS.1770 K-weighting (pre-filter shelf + RLB high-pass), any sample rate
struct KWeight
{
    Biquad shelf, hp;
    void prepare (double sr)
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            shelf.setRaw (Vh + Vb * K / Q + K * K, 2.0 * (K * K - Vh), Vh - Vb * K / Q + K * K, 1.0 + K / Q + K * K, 2.0 * (K * K - 1.0), 1.0 - K / Q + K * K);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            // BS.1770 specifies a unity numerator for the RLB high-pass
            const double a0 = 1.0 + K / Q + K * K;
            hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
            hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    void reset() { shelf.reset(); hp.reset(); }
    inline float process (float x) noexcept { return hp.process (shelf.process (x)); }
};

//==============================================================================
// Loudness meter: momentary (400 ms), short-term (3 s) and gated integrated LUFS.
// Audio thread writes; the integrated value is computed on the UI thread from a
// histogram of 400 ms block loudnesses (0.1 LU bins), so nothing allocates.
class LoudnessMeter
{
public:
    static constexpr int kBins = 900;          // -70 .. +20 LUFS in 0.1 LU steps
    void prepare (double sr)
    {
        subLen = juce::jmax (1, (int) std::lround (sr * 0.1));
        clearAll();
    }
    void requestReset() { resetRequested.store (true); }

    // weighted = K-weighted samples already summed per channel (sum of squares added by caller)
    inline void addPower (double sumSq, int n)
    {
        if (resetRequested.exchange (false)) clearAll();
        while (n > 0)
        {
            const int take = juce::jmin (n, subLen - subCount);
            const double frac = (double) take / (double) juce::jmax (1, n);
            acc += sumSq * frac; sumSq -= sumSq * frac;
            subCount += take; n -= take;
            if (subCount >= subLen) closeSubBlock();
        }
    }

    float getMomentary() const { return momentary.load(); }
    float getShortTerm() const { return shortTerm.load(); }
    float computeIntegrated() const
    {
        double sumP = 0, cnt = 0;
        for (int b = 0; b < kBins; ++b)
            if (const auto c = hist[(size_t) b].load (std::memory_order_relaxed)) { sumP += c * binPower (b); cnt += c; }
        if (cnt < 1) return -100.0f;
        const double gate = -0.691 + 10.0 * std::log10 (sumP / cnt) - 10.0;
        sumP = 0; cnt = 0;
        for (int b = 0; b < kBins; ++b)
            if (binLufs (b) > gate)
                if (const auto c = hist[(size_t) b].load (std::memory_order_relaxed)) { sumP += c * binPower (b); cnt += c; }
        if (cnt < 1) return -100.0f;
        return (float) (-0.691 + 10.0 * std::log10 (sumP / cnt));
    }

private:
    static double binLufs (int b) { return -70.0 + ((double) b + 0.5) * 0.1; }
    static double binPower (int b) { return std::pow (10.0, (binLufs (b) + 0.691) / 10.0); }
    static float toLufs (double p) { return p <= 1e-12 ? -100.0f : (float) (-0.691 + 10.0 * std::log10 (p)); }

    void clearAll()
    {
        ring.fill (0.0); ringPos = 0; ringFill = 0; acc = 0; subCount = 0;
        for (auto& h : hist) h.store (0, std::memory_order_relaxed);
        momentary.store (-100.0f); shortTerm.store (-100.0f);
    }
    void closeSubBlock()
    {
        ring[(size_t) ringPos] = acc / (double) subLen;
        ringPos = (ringPos + 1) % 30; ringFill = juce::jmin (30, ringFill + 1);
        acc = 0; subCount = 0;
        auto meanLast = [&] (int k)
        {
            double s = 0;
            for (int i = 1; i <= k; ++i) s += ring[(size_t) ((ringPos - i + 30) % 30)];
            return s / (double) k;
        };
        if (ringFill >= 4)
        {
            const double pm = meanLast (4);
            const float lm = toLufs (pm);
            momentary.store (lm);
            if (lm > -70.0f)
            {
                const int b = juce::jlimit (0, kBins - 1, (int) ((lm + 70.0f) * 10.0f));
                hist[(size_t) b].fetch_add (1, std::memory_order_relaxed);
            }
        }
        shortTerm.store (toLufs (meanLast (ringFill)));
    }

    int subLen = 4800, subCount = 0, ringPos = 0, ringFill = 0;
    double acc = 0;
    std::array<double, 30> ring {};
    std::array<std::atomic<uint32_t>, kBins> hist {};
    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f };
    std::atomic<bool> resetRequested { false };
};

//==============================================================================
// Saturation shapers. All have unity gain for quiet signals, so SATURATION adds
// harmonics and rounds peaks rather than just changing the volume.
inline float ntanh (float x, float k, float b)
{
    const float tb = std::tanh (b);
    return (std::tanh (k * x + b) - tb) / (k * (1.0f - tb * tb));
}
inline float natan (float x, float k, float b)
{
    return (std::atan (k * x + b) - std::atan (b)) * (1.0f + b * b) / k;
}
inline float nhard (float x, float k) // smooth-but-firm odd clipper, x / (1 + |x|^4)^(1/4)
{
    const float v = k * x, a = v * v;
    return v / std::sqrt (std::sqrt (1.0f + a * a)) / k;
}

//==============================================================================
// Final true-peak safety stage. The soft clipper does the real work; this only catches the
// small inter-sample overshoot that band-limiting a clipped waveform creates, so the
// CEILING is a true-peak ceiling (-1 dBTP is what the streaming services ask for).
// Estimates the 4x-interpolated peak, then applies a look-ahead gain dip that is
// guaranteed to reach the needed gain by the time the peak goes out.
class TruePeakStage
{
public:
    void prepare (double sr, int maxBlock)
    {
        side = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
        side->initProcessing ((size_t) maxBlock);
        sideBuf.setSize (2, maxBlock);
        D = (int) std::ceil (side->getLatencyInSamples());
        L = juce::jlimit (8, 256, (int) std::lround (sr * 0.0008));
        len = juce::nextPowerOfTwo (D + L + 8);
        for (auto& b : x) b.assign ((size_t) len, 0.0f);
        gRing.assign ((size_t) L + 1, 1.0f);
        hRing.assign ((size_t) L, 1.0f);
        hSum = (double) L; pos = 0; gPos = 0; hPos = 0; hr = 1.0f; lastTp = 0.0f;
        rel = (float) std::exp (-1.0 / (sr * 0.04));
    }
    int latency() const { return D + L; }

    // in place; thr = ceiling gain; when !on it is just the matching delay
    void process (float* const* ch, int numCh, int n, float thr, bool on)
    {
        // accurate 4x true-peak estimate of what's coming
        for (int c = 0; c < 2; ++c) sideBuf.copyFrom (c, 0, ch[juce::jmin (c, numCh - 1)], n);
        juce::dsp::AudioBlock<float> blk (sideBuf.getArrayOfWritePointers(), 2, (size_t) n);
        auto up = side->processSamplesUp (blk);
        const int mask = len - 1;
        for (int i = 0; i < n; ++i)
        {
            float tp = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float* u = up.getChannelPointer ((size_t) c) + 4 * i;
                tp = std::max ({ tp, std::abs (u[0]), std::abs (u[1]), std::abs (u[2]), std::abs (u[3]) });
            }
            const float tpw = std::max (tp, lastTp);    // covers +-1 sample of alignment slop
            lastTp = tp;
            for (int c = 0; c < numCh; ++c) x[(size_t) c][(size_t) pos] = ch[c][i];

            const float g = on && tpw > thr ? thr / tpw : 1.0f;
            gRing[(size_t) gPos] = g; gPos = (gPos + 1) % (L + 1);
            float h = 1.0f;
            for (auto v : gRing) h = std::min (h, v);          // min over the look-ahead window
            hr = std::min (h, 1.0f + rel * (hr - 1.0f));        // release
            hSum += (double) hr - (double) hRing[(size_t) hPos];
            hRing[(size_t) hPos] = hr; hPos = (hPos + 1) % L;
            const float gain = on ? (float) std::min (1.0, hSum / (double) L) : 1.0f;
            const int outIdx = (pos - D - L) & mask;
            for (int c = 0; c < numCh; ++c) ch[c][i] = x[(size_t) c][(size_t) outIdx] * gain;
            pos = (pos + 1) & mask;
        }
        if (++resum > 64) { resum = 0; hSum = 0; for (auto v : hRing) hSum += v; }
    }

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> side;
    juce::AudioBuffer<float> sideBuf;
    int D = 0, L = 38, len = 64, pos = 0, gPos = 0, hPos = 0, resum = 0;
    std::array<std::vector<float>, 2> x;
    std::vector<float> gRing, hRing;
    double hSum = 38;
    float hr = 1.0f, rel = 0.999f, lastTp = 0.0f;
};

struct Params
{
    float discipline = 0.3f, saturation = 0.2f, analog = 0.15f, mix = 1.0f, outputDb = 0.0f; // 0..1 except dB
    int satType = tape, ceiling = ceilMinus1, quality = 1; // quality: 0 = 2x, 1 = 4x, 2 = 8x
    bool match = false;
};

//==============================================================================
class Engine
{
public:
    static constexpr int maxCh = 2;
    static constexpr int kChunk = 512;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        blockMax = juce::jlimit (1, kChunk, maxBlock);
        for (int q = 0; q < 3; ++q)
            for (auto* o : { &osW, &osD })
            {
                (*o)[(size_t) q] = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) maxCh, (size_t) (q + 1),
                    juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
                (*o)[(size_t) q]->initProcessing ((size_t) blockMax);
            }
        dryBuf.setSize (maxCh, blockMax);

        // tape transport: fixed centre delay so ANALOG 0 is a plain (exact) delay
        wowCentre = (int) std::ceil (sr * 0.00045) + 2;
        wowLen = juce::nextPowerOfTwo (wowCentre * 2 + 8);
        for (auto& b : wowBuf) b.assign ((size_t) wowLen, 0.0f);
        wowWrite = 0; wowPhase = 0; flutPhase = 0;

        for (int c = 0; c < maxCh; ++c)
        {
            auto& t = tone[(size_t) c];
            t.bump.design (Biquad::lowShelf, sr, 72.0, 0.7, 1.3);
            t.hp.design (Biquad::highpass, sr, 16.0, 0.6);
            t.roll.design (Biquad::highShelf, sr, c == 0 ? 11000.0 : 9800.0, 0.7, c == 0 ? -2.0 : -2.3);
            t.bump.reset(); t.hp.reset(); t.roll.reset();
            scHp[(size_t) c].design (Biquad::highpass, sr, 90.0, 0.707);
            scHp[(size_t) c].reset();
            kIn[(size_t) c].prepare (sr); kOut[(size_t) c].prepare (sr);
            pink[(size_t) c] = {};
        }
        humPhase = 0;
        drift.fill (0.0f); driftTarget.fill (0.0f); driftCount = 0;
        driftCoef = (float) std::exp (-1.0 / (sr * 2.0));

        // compressor
        progMs = std::pow (10.0, -18.0 / 10.0); rmsMs = progMs; peakEnv = 0.0f; env1 = env2 = 0.0f;
        aRms   = (float) std::exp (-1.0 / (sr * 0.25));
        aPeak  = (float) std::exp (-1.0 / (sr * 0.25));
        aProgUp = (float) std::exp (-1.0 / (sr * 2.0));
        aProgDn = (float) std::exp (-1.0 / (sr * 8.0));
        aWarm = (float) std::exp (-1.0 / (sr * 0.3));
        aRelFast = (float) std::exp (-1.0 / (sr * 0.12));
        aSlowAtk = (float) std::exp (-1.0 / (sr * 0.5));
        aRelSlow = (float) std::exp (-1.0 / (sr * 1.5));

        matchInMs = matchOutMs = 0.0;
        aMatch = std::exp (-1.0 / (sr * 3.0));
        loudness.prepare (sr);
        tpStage.prepare (sr, blockMax);
        warmLeft = (int) (sr * 1.5);

        for (auto* s : { &sDisc, &sSat, &sAnalog, &sMix, &sOut, &sCeil, &sMake })
            s->reset (sr, 0.04);
        sMatch.reset (sr, 0.3);
        sMatch.setCurrentAndTargetValue (1.0f);
        activeQ = -1;
        snapNext = true;
    }

    int latencyFor (int q) const
    {
        q = juce::jlimit (0, 2, q);
        return (osW[(size_t) q] == nullptr ? 0 : (int) std::lround (osW[(size_t) q]->getLatencyInSamples())) + wowCentre + tpStage.latency();
    }
    int getLatency() const { return latencyFor (activeQ < 0 ? prm.quality : activeQ); }
    LoudnessMeter& getLoudness() { return loudness; }

    // static makeup for a DISCIPLINE setting: tuned so the glue doesn't just make things quieter
    static float makeupFor (float d)
    {
        if (d <= 0.0f) return 0.0f;
        return 0.55f * gainReduction (9.0f - thresholdOffset (d), ratioFor (d));
    }
    static float ratioFor (float d) { return 1.5f + 2.5f * d; }
    static float thresholdOffset (float d) { return 11.0f - 13.0f * d; }   // dB above the program level
    static constexpr float kneeDb = 8.0f;
    static float gainReduction (float overDb, float ratio)
    {
        const float s = 1.0f - 1.0f / ratio;
        if (2.0f * overDb < -kneeDb) return 0.0f;
        if (2.0f * overDb > kneeDb) return s * overDb;
        const float t = overDb + kneeDb * 0.5f;
        return s * t * t / (2.0f * kneeDb);
    }

    void setParams (const Params& p)
    {
        prm = p;
        sDisc.setTargetValue (p.discipline);
        sSat.setTargetValue (p.saturation);
        sAnalog.setTargetValue (p.analog);
        sMix.setTargetValue (p.mix);
        sOut.setTargetValue (juce::Decibels::decibelsToGain (p.outputDb));
        sCeil.setTargetValue (juce::Decibels::decibelsToGain (ceilingDbFor (p.ceiling)));
        sMake.setTargetValue (makeupFor (p.discipline));
        if (snapNext)
        {
            for (auto* s : { &sDisc, &sSat, &sAnalog, &sMix, &sOut, &sCeil, &sMake }) s->setCurrentAndTargetValue (s->getTargetValue());
            curType = prevType = p.satType; typeFade = 1.0f;
            snapNext = false;
        }
    }

    // Processes numCh (1 or 2) channels in place.
    void process (float* const* ch, int numCh, int n)
    {
        numCh = juce::jlimit (1, maxCh, numCh);
        if (prm.quality != activeQ)
        {
            activeQ = juce::jlimit (0, 2, prm.quality);
            osW[(size_t) activeQ]->reset(); osD[(size_t) activeQ]->reset();
            designSat (sr * (double) (1 << (activeQ + 1)));
        }
        for (int start = 0; start < n; start += blockMax)
        {
            float* ptrs[maxCh] = { ch[0] + start, numCh > 1 ? ch[1] + start : ch[0] + start };
            processChunk (ptrs, numCh, std::min (blockMax, n - start));
        }
    }

    // metering (UI reads; peaks are exchanged with 0 by the UI)
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, grDb { 0 }, clipDb { 0 };
    std::atomic<float> threshDb { -18 }, ratioNow { 1.5f }, attackMs { 10 }, releaseMs { 120 }, makeupDb { 0 }, matchDb { 0 };

private:
    struct Tone { Biquad bump, hp, roll; };
    struct SatState
    {
        Biquad tapePre, tapePost, whipPre, whipPost;
        float ironLp = 0, dcX = 0, dcY = 0;
        void reset() { tapePre.reset(); tapePost.reset(); whipPre.reset(); whipPost.reset(); ironLp = dcX = dcY = 0; }
    };
    struct Pink { float b0 = 0, b1 = 0, b2 = 0; };

    void designSat (double osRate)
    {
        osSr = osRate;
        for (auto& s : sat)
        {
            s.tapePre.design (Biquad::highShelf, osRate, 3200.0, 0.6, 5.0);
            s.tapePost.design (Biquad::highShelf, osRate, 3200.0, 0.6, -5.0);
            s.whipPre.design (Biquad::peak, osRate, 2600.0, 0.7, 7.0);
            s.whipPost.design (Biquad::peak, osRate, 2600.0, 0.7, -3.5);
            s.reset();
        }
        ironCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 150.0 / osRate));
        dcCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 6.0 / osRate);
        typeFadeInc = (float) (1.0 / (osRate * 0.03));
    }

    inline float rnd() noexcept // fast white noise, -1..1
    {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return (float) (int32_t) rngState * (1.0f / 2147483648.0f);
    }

    // How much a type turns a -9 dBFS peak down at amount a; used to keep the level steady
    static float satCompensation (int type, float a)
    {
        if (a <= 0.0f) return 1.0f;
        const float A = 0.35f;
        float o;
        switch (type)
        {
            case tape:    o = ntanh (A, 1.0f + a, 0.05f); break;
            case tube:    o = 0.5f * (ntanh (A, 1.0f + 0.8f * a, 0.35f) - ntanh (-A, 1.0f + 0.8f * a, 0.35f)); break;
            case iron:    o = 0.5f * (ntanh (A, 1.0f + 2.2f * a, 0.03f) + ntanh (A, 1.0f + 0.4f * a, 0.0f)); break;
            case console: o = natan (A, 1.0f + 0.9f * a, 0.02f); break;
            default:      o = nhard (A, 1.0f + 2.5f * a); break;
        }
        return std::pow (A / std::max (1e-4f, o), 0.8f);
    }

    inline float saturate (int type, SatState& st, float x, float a, float bias) noexcept
    {
        float s;
        switch (type)
        {
            case tape:
                s = st.tapePost.process (ntanh (st.tapePre.process (x), 1.0f + 1.0f * a, 0.05f + bias));
                break;
            case tube:
                s = ntanh (x, 1.0f + 0.8f * a, 0.35f + bias);
                break;
            case iron:
            {
                st.ironLp += ironCoef * (x - st.ironLp);
                const float lo = st.ironLp, hi = x - lo;
                s = ntanh (lo, 1.0f + 2.2f * a, 0.03f + bias) + ntanh (hi, 1.0f + 0.4f * a, 0.0f);
                break;
            }
            case console:
                s = natan (x, 1.0f + 0.9f * a, 0.02f + bias);
                break;
            default: // whip
                s = st.whipPost.process (nhard (st.whipPre.process (x), 1.0f + 2.5f * a));
                break;
        }
        // DC blocker (the biased shapers make a little DC)
        const float o = s - st.dcX + dcCoef * st.dcY;
        st.dcX = s; st.dcY = o;
        return o;
    }

    void processChunk (float* const* ch, int numCh, int n)
    {
        // ---------------------------------------------------------------- input meters
        float inPk = 0.0f;
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i) inPk = std::max (inPk, std::abs (ch[c][i]));
        for (int i = 0; i < n; ++i)   // input loudness, always tracked so MATCH is ready the moment it's switched on
            {
                double s = 0;
                for (int c = 0; c < numCh; ++c) { const float k = kIn[(size_t) c].process (ch[c][i]); s += (double) k * k; }
                matchInMs = s + aMatch * (matchInMs - s);
            }

        // ---------------------------------------------------------------- drift (slow random)
        driftCount += n;
        if (driftCount > (int) (sr * 0.7))
        {
            driftCount = 0;
            for (auto& t : driftTarget) t = rnd();
        }
        const float dk = std::pow (driftCoef, (float) n);
        for (size_t k = 0; k < drift.size(); ++k) drift[k] = driftTarget[k] + dk * (drift[k] - driftTarget[k]);

        // ---------------------------------------------------------------- ANALOG input stage
        std::array<float, kChunk> an, dsc, mk;
        for (int i = 0; i < n; ++i) { an[(size_t) i] = sAnalog.getNextValue(); dsc[(size_t) i] = sDisc.getNextValue(); mk[(size_t) i] = sMake.getNextValue(); }
        const bool analogOn = an[0] > 0.0f || an[(size_t) n - 1] > 0.0f;
        {
            const double wowInc = juce::MathConstants<double>::twoPi * 0.55 / sr, flutInc = juce::MathConstants<double>::twoPi * 6.3 / sr;
            const double humInc = juce::MathConstants<double>::twoPi * 60.0 / sr;
            const float wowDepth = (float) (sr * 0.00012), flutDepth = (float) (sr * 0.000007);
            const float hissGain = juce::Decibels::decibelsToGain (-79.0f), humGain = juce::Decibels::decibelsToGain (-98.0f);
            for (int i = 0; i < n; ++i)
            {
                const float a = an[(size_t) i];
                float d = (float) wowCentre;
                if (analogOn)
                {
                    const float a2 = a * a;
                    d += a2 * (wowDepth * (float) std::sin (wowPhase) * (1.0f + 0.25f * drift[2])
                               + flutDepth * (float) std::sin (flutPhase));
                    wowPhase += wowInc * (1.0 + 0.05 * drift[3]); flutPhase += flutInc;
                    if (wowPhase > juce::MathConstants<double>::twoPi) wowPhase -= juce::MathConstants<double>::twoPi;
                    if (flutPhase > juce::MathConstants<double>::twoPi) flutPhase -= juce::MathConstants<double>::twoPi;
                }
                const int di = (int) std::floor (d);
                const float fr = d - (float) di;
                std::array<float, maxCh> v {};
                for (int c = 0; c < numCh; ++c)
                {
                    auto& buf = wowBuf[(size_t) c];
                    buf[(size_t) wowWrite] = ch[c][i];
                    auto at = [&] (int k) { return buf[(size_t) ((wowWrite - k + wowLen) & (wowLen - 1))]; };
                    if (fr == 0.0f) v[(size_t) c] = at (di);
                    else
                    {   // 4-point Hermite
                        const float xm1 = at (di - 1), x0 = at (di), x1 = at (di + 1), x2 = at (di + 2);
                        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                        v[(size_t) c] = ((c3 * fr + c2) * fr + c1) * fr + x0;
                    }
                }
                wowWrite = (wowWrite + 1) & (wowLen - 1);

                if (analogOn)
                {
                    // tone (head bump, subsonic roll, HF roll-off; L and R slightly different)
                    for (int c = 0; c < numCh; ++c)
                    {
                        auto& t = tone[(size_t) c];
                        const float x = v[(size_t) c];
                        const float nl = x + 0.014f * x * x - 0.010f * x * x * x;   // a little component non-linearity
                        const float f = t.roll.process (t.hp.process (t.bump.process (nl)));
                        const float mism = 1.0f + a * (c == 0 ? 0.014f : -0.014f) + a * 0.006f * drift[1];
                        v[(size_t) c] = (x + a * (f - x)) * mism;
                    }
                    if (numCh == 2)
                    {
                        const float ct = 0.004f * a, l = v[0], r = v[1];
                        v[0] = l + ct * r; v[1] = r + ct * l;
                    }
                    // hiss + hum
                    const float hg = hissGain * a * std::sqrt (a);
                    const float hum = humGain * a * a * (float) (std::sin (humPhase) + 0.35 * std::sin (3.0 * humPhase));
                    humPhase += humInc;
                    if (humPhase > juce::MathConstants<double>::twoPi) humPhase -= juce::MathConstants<double>::twoPi;
                    for (int c = 0; c < numCh; ++c)
                    {
                        auto& p = pink[(size_t) c];
                        const float w = rnd();
                        p.b0 = 0.99765f * p.b0 + w * 0.0990460f;
                        p.b1 = 0.96300f * p.b1 + w * 0.2965164f;
                        p.b2 = 0.57000f * p.b2 + w * 1.0526913f;
                        const float pk = (p.b0 + p.b1 + p.b2 + w * 0.1848f) * 0.18f;
                        v[(size_t) c] += hg * pk + hum;
                    }
                }
                for (int c = 0; c < numCh; ++c) { ch[c][i] = v[(size_t) c]; dryBuf.setSample (c, i, v[(size_t) c]); }
            }
        }

        // ---------------------------------------------------------------- AUTO GLUE COMPRESSOR
        float grMax = 0.0f;
        {
            const float crest = juce::Decibels::gainToDecibels (std::max (peakEnv, 1e-6f)) - 10.0f * std::log10 ((float) std::max (rmsMs, 1e-12));
            const float tAtk = 0.010f + 0.020f * juce::jlimit (0.0f, 1.0f, (crest - 6.0f) / 12.0f);
            const float aAtk = std::exp (-1.0f / ((float) sr * tAtk));
            const float gate = std::pow (10.0f, -50.0f / 10.0f);
            float pDb = 0.0f, thr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                for (int c = 0; c < numCh; ++c) x = std::max (x, std::abs (scHp[(size_t) c].process (ch[c][i])));
                const double x2 = (double) x * x;
                rmsMs = x2 + aRms * (rmsMs - x2);
                peakEnv = std::max (x, peakEnv * aPeak);
                if (rmsMs > gate)
                {
                    const float coef = warmLeft > 0 ? aWarm : (rmsMs > progMs ? aProgUp : aProgDn);
                    if (warmLeft > 0) --warmLeft;
                    progMs = rmsMs + coef * (progMs - rmsMs);
                }
                pDb = juce::jlimit (-40.0f, 0.0f, 10.0f * std::log10 ((float) std::max (progMs, 1e-12)));

                const float d = dsc[(size_t) i];
                thr = pDb + thresholdOffset (d) + 0.4f * an[(size_t) i] * drift[0];
                const float xDb = 20.0f * std::log10 (x + 1e-9f);
                const float target = d <= 0.0f ? 0.0f : std::min (15.0f, gainReduction (xDb - thr, ratioFor (d)) * std::min (1.0f, d * 20.0f));

                env1 = target > env1 ? target + aAtk * (env1 - target) : target + aRelFast * (env1 - target);
                env2 = env1 > env2 ? env1 + aSlowAtk * (env2 - env1) : env1 + aRelSlow * (env2 - env1);
                const float gr = std::max (env1, env2);
                grMax = std::max (grMax, gr);
                const float g = juce::Decibels::decibelsToGain (mk[(size_t) i] - gr);
                for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
            }
            const float d = sDisc.getCurrentValue();
            threshDb.store (thr);
            ratioNow.store (ratioFor (d));
            attackMs.store (tAtk * 1000.0f);
            const float slowShare = env1 > 0.05f ? juce::jlimit (0.0f, 1.0f, env2 / env1) : 0.0f;
            releaseMs.store (120.0f + slowShare * slowShare * 1380.0f);
            makeupDb.store (sMake.getCurrentValue());
        }

        // ---------------------------------------------------------------- oversampled: SATURATION, MIX, OUTPUT, CLIP
        std::array<float, kChunk> gSat, gMix, gOut, gCeil;
        for (int i = 0; i < n; ++i)
        {
            gSat[(size_t) i] = sSat.getNextValue(); gMix[(size_t) i] = sMix.getNextValue();
            gOut[(size_t) i] = sOut.getNextValue(); gCeil[(size_t) i] = sCeil.getNextValue();
        }
        if (prm.satType != curType && typeFade >= 1.0f)
        {
            prevType = curType; curType = prm.satType; typeFade = 0.0f;
            std::swap (satOld, sat);         // 'satOld' keeps the outgoing type's state while it fades out
            for (auto& s : sat) s.reset();   // fresh state for the incoming type
        }

        float* wp[maxCh] = { ch[0], numCh > 1 ? ch[1] : ch[0] };
        float* dp[maxCh] = { dryBuf.getWritePointer (0), dryBuf.getWritePointer (numCh > 1 ? 1 : 0) };
        juce::dsp::AudioBlock<float> wBlock (wp, (size_t) numCh, (size_t) n), dBlock (dp, (size_t) numCh, (size_t) n);
        auto& ow = *osW[(size_t) activeQ];
        auto upW = ow.processSamplesUp (wBlock);
        auto upD = osD[(size_t) activeQ]->processSamplesUp (dBlock);
        const int factor = 1 << (activeQ + 1);
        const int upN = (int) upW.getNumSamples();
        const bool clipOn = prm.ceiling != ceilOff;
        const float bias = 0.04f * drift[0] * sAnalog.getCurrentValue();
        const float knee = 0.82f;
        float clipPk = 0.0f;
        const float fadeStart = typeFade;
        const float compCur = satCompensation (curType, sSat.getCurrentValue()), compPrev = satCompensation (prevType, sSat.getCurrentValue());
        for (int c = 0; c < numCh; ++c)
        {
            auto* w = upW.getChannelPointer ((size_t) c);
            const auto* dr = upD.getChannelPointer ((size_t) c);
            float tf = fadeStart;
            for (int i = 0; i < upN; ++i)
            {
                const int bi = std::min (n - 1, i / factor);
                const float a = gSat[(size_t) bi];
                float x = w[i];
                if (a > 0.0f || tf < 1.0f)
                {
                    const float blend = std::min (1.0f, a * 2.0f);
                    float s = saturate (curType, sat[(size_t) c], x, a, bias) * compCur;
                    if (tf < 1.0f)
                    {
                        const float so = saturate (prevType, satOld[(size_t) c], x, a, bias) * compPrev;
                        s = so + tf * (s - so);
                        tf = std::min (1.0f, tf + typeFadeInc);
                    }
                    x += blend * (s - x);
                }
                const float m = gMix[(size_t) bi];
                float y = (dr[i] + m * (x - dr[i])) * gOut[(size_t) bi];
                if (clipOn)
                {
                    const float cl = gCeil[(size_t) bi] * kInternalCeil;
                    const float u = std::abs (y) / cl;
                    clipPk = std::max (clipPk, u);
                    if (u > knee) y = std::copysign (cl * (knee + (1.0f - knee) * std::tanh ((u - knee) / (1.0f - knee))), y);
                }
                w[i] = y;
            }
            if (c == numCh - 1) typeFade = tf;
        }
        ow.processSamplesDown (wBlock);

        // ---------------------------------------------------------------- safety ceiling, meters, MATCH
        sMatch.setTargetValue (1.0f);
        if (prm.match)
        {
            const double r = matchOutMs > 1e-12 ? std::sqrt (matchInMs / matchOutMs) : 1.0;
            sMatch.setTargetValue ((float) juce::jlimit (0.0625, 2.0, r));   // -24 .. +6 dB
        }
        tpStage.process (ch, numCh, n, gCeil[(size_t) n - 1] * 0.995f, clipOn);
        double kSum = 0;
        float outPk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float mg = sMatch.getNextValue();
            double s = 0;
            for (int c = 0; c < numCh; ++c)
            {
                float y = ch[c][i];
                if (clipOn) y = juce::jlimit (-gCeil[(size_t) i], gCeil[(size_t) i], y);
                const float k = kOut[(size_t) c].process (y);
                s += (double) k * k;
                y *= mg;
                ch[c][i] = y;
                outPk = std::max (outPk, std::abs (y));
            }
            if (numCh == 1) s *= 2.0;   // mono is played on both speakers
            kSum += s;
            matchOutMs = s + aMatch * (matchOutMs - s);
        }
        loudness.addPower (kSum, n);

        inPeak.store (std::max (inPeak.load(), inPk));
        outPeak.store (std::max (outPeak.load(), outPk));
        grDb.store (std::max (grDb.load(), grMax));
        clipDb.store (std::max (clipDb.load(), clipOn && clipPk > knee ? juce::Decibels::gainToDecibels (clipPk / knee) : 0.0f));
        matchDb.store (juce::Decibels::gainToDecibels (sMatch.getCurrentValue()));
    }

public:
    static constexpr float kInternalCeil = 0.989f; // ~ -0.1 dB margin so true peaks stay under the ceiling

private:
    double sr = 44100.0, osSr = 176400.0;
    int blockMax = 512, activeQ = -1;
    bool snapNext = true;
    Params prm;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> osW, osD;
    juce::AudioBuffer<float> dryBuf;

    // analog
    int wowCentre = 24, wowLen = 64, wowWrite = 0;
    std::array<std::vector<float>, maxCh> wowBuf;
    double wowPhase = 0, flutPhase = 0, humPhase = 0;
    std::array<Tone, maxCh> tone;
    std::array<Pink, maxCh> pink;
    std::array<float, 4> drift {}, driftTarget {};
    int driftCount = 0;
    float driftCoef = 0.9999f;
    uint32_t rngState = 0x9e3779b9u;

    // compressor
    std::array<Biquad, maxCh> scHp;
    double rmsMs = 0, progMs = 0;
    float peakEnv = 0, env1 = 0, env2 = 0;
    float aRms = 0, aPeak = 0, aProgUp = 0, aProgDn = 0, aWarm = 0, aRelFast = 0, aSlowAtk = 0, aRelSlow = 0;
    int warmLeft = 0;

    // saturation
    std::array<SatState, maxCh> sat, satOld;
    int curType = tape, prevType = tape;
    float typeFade = 1.0f, typeFadeInc = 0.001f, ironCoef = 0.01f, dcCoef = 0.9999f;

    // loudness / match
    std::array<KWeight, maxCh> kIn, kOut;
    double matchInMs = 0, matchOutMs = 0, aMatch = 0.9999;
    LoudnessMeter loudness;
    TruePeakStage tpStage;

    juce::SmoothedValue<float> sDisc { 0.0f }, sSat { 0.0f }, sAnalog { 0.0f }, sMix { 1.0f }, sOut { 1.0f }, sCeil { 1.0f }, sMake { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> sMatch { 1.0f };
};
} // namespace dom
