// Headless checks (run locally and in CI):
//  1) DSP: ceiling holds (-1 / -6 dB) in every saturation type and quality, true peaks stay
//     close, everything at 0 is transparent, DISCIPLINE compresses and is level independent,
//     makeup keeps the level, each saturation type behaves like itself, ANALOG adds its
//     character, MIX doesn't comb, OUTPUT drives the clipper, MATCH matches, LUFS reads right
//  2) presets: factory presets hold their ceilings, user preset round trip
//  3) renders the editor to PNGs
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

static bool writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    if (auto os = f.createOutputStream()) return juce::PNGImageFormat().writeImageToStream (img, *os);
    return false;
}

static void setP (DominatrixProcessor& p, const char* id, float v)
{
    auto* rp = p.apvts.getParameter (id);
    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

static int prog (DominatrixProcessor& p, const juce::String& name)
{
    for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == name) return i;
    return 0;
}

static void allOff (DominatrixProcessor& p)
{
    p.setCurrentProgram (0);
    setP (p, "discipline", 0); setP (p, "saturation", 0); setP (p, "analog", 0);
    setP (p, "mix", 100); setP (p, "outputDb", 0); setP (p, "ceiling", 2); setP (p, "match", 0);
}

using Gen = std::function<float (int64_t n, int ch)>;

static Gen sine (float amp, float freq, double sr = 48000.0)
{
    return [=] (int64_t n, int) { return amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) n / sr); };
}

// a crude but repeatable "mix": kick, snare, hats and a sustained chord
static Gen drumMix (float level, double sr = 48000.0)
{
    return [=] (int64_t n, int ch)
    {
        const double t = (double) n / sr;
        const double tb = std::fmod (t, 0.5), ts = std::fmod (t + 0.25, 0.5), th = std::fmod (t, 0.125);
        auto noise = [] (int64_t k) { uint32_t h = (uint32_t) k * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return (float) (h & 0xffff) / 32768.0f - 1.0f; };
        const double kick = std::exp (-tb * 14.0) * std::sin (juce::MathConstants<double>::twoPi * 52.0 * tb * (1.0 + 1.5 * std::exp (-tb * 30.0)));
        const double snare = (ts < 0.25 ? std::exp (-ts * 22.0) : 0.0) * (0.55 * noise (n + ch) + 0.3 * std::sin (juce::MathConstants<double>::twoPi * 190.0 * ts));
        const double hat = std::exp (-th * 90.0) * 0.12 * noise (n * 7 + 3 + ch);
        const double pad = 0.09 * (std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) + std::sin (juce::MathConstants<double>::twoPi * 277.2 * t + ch)
                                   + std::sin (juce::MathConstants<double>::twoPi * 329.6 * t));
        return level * (float) (0.6 * kick + snare + hat + pad);
    };
}

struct Render { std::vector<float> l, r; };

// renders through the real processBlock; returns what came out after `skipSec`
static Render render (DominatrixProcessor& p, const Gen& g, double seconds, double skipSec, double sr = 48000.0, bool prepare = true)
{
    if (prepare) p.prepareToPlay (sr, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    Render out;
    const int64_t total = (int64_t) (seconds * sr), skip = (int64_t) (skipSec * sr);
    for (int64_t pos = 0; pos < total; pos += 512)
    {
        for (int i = 0; i < 512; ++i)
            for (int c = 0; c < 2; ++c) buf.setSample (c, i, g (pos + i, c));
        p.processBlock (buf, midi);
        if (pos >= skip)
        {
            out.l.insert (out.l.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
            out.r.insert (out.r.end(), buf.getReadPointer (1), buf.getReadPointer (1) + 512);
        }
    }
    return out;
}

static float peak (const std::vector<float>& v) { float m = 0; for (auto x : v) m = std::max (m, std::abs (x)); return m; }
static float rmsDb (const std::vector<float>& v)
{
    double s = 0; for (auto x : v) s += (double) x * x;
    return (float) juce::Decibels::gainToDecibels (std::sqrt (s / (double) std::max<size_t> (1, v.size())), -200.0);
}
static float truePeak (const std::vector<float>& v)
{
    juce::dsp::Oversampling<float> os (1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
    os.initProcessing (512);
    float m = 0;
    std::vector<float> tmp (512);
    for (size_t pos = 0; pos + 512 <= v.size(); pos += 512)
    {
        std::copy (v.begin() + (long) pos, v.begin() + (long) pos + 512, tmp.begin());
        float* ptr[] = { tmp.data() };
        juce::dsp::AudioBlock<float> b (ptr, 1, 512);
        auto up = os.processSamplesUp (b);
        if (pos == 0) continue;   // skip the meter's own start-up (it starts from silence mid-waveform)
        for (size_t i = 0; i < up.getNumSamples(); ++i) m = std::max (m, std::abs (up.getSample (0, (int) i)));
    }
    return m;
}

static double dftMag (const std::vector<float>& v, double f, double sr)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) i / (double) (v.size() - 1));
        re += v[i] * w * std::cos (juce::MathConstants<double>::twoPi * f * (double) i / sr);
        im -= v[i] * w * std::sin (juce::MathConstants<double>::twoPi * f * (double) i / sr);
    }
    return std::sqrt (re * re + im * im);
}
// energy in harmonics 2..10 relative to the fundamental
static float harmonicsDb (const std::vector<float>& v, float freq, double sr = 48000.0)
{
    const double fund = dftMag (v, freq, sr);
    double h = 0;
    for (int k = 2; k <= 10; ++k) if (freq * k < sr * 0.5) { const double m = dftMag (v, freq * k, sr); h += m * m; }
    return (float) juce::Decibels::gainToDecibels (std::sqrt (h) / fund, -200.0);
}
static float harmonicDb (const std::vector<float>& v, float freq, int k, double sr = 48000.0)
{
    return (float) juce::Decibels::gainToDecibels (dftMag (v, freq * k, sr) / dftMag (v, freq, sr), -200.0);
}
static std::vector<float> trim (const std::vector<float>& v, size_t n) { return std::vector<float> (v.begin(), v.begin() + (long) std::min (n, v.size())); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory());
    dir.createDirectory();

    if (argc > 2 && juce::String (argv[2]) == "lab")
    {
        DominatrixProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        for (float o : { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f })
            for (int pre : { 0, 4 })
            {
                p.setCurrentProgram (pre);
                setP (p, "outputDb", o);
                const auto y = render (p, drumMix (0.9f), 4.0, 1.0);
                std::printf ("preset %d out %+4.0f: peak %.2f  TP %.2f dBTP  clip %.2f\n", pre, o, juce::Decibels::gainToDecibels (peak (y.l)),
                             juce::Decibels::gainToDecibels (truePeak (y.l)), p.getEngine().clipDb.exchange (0));
            }
        {
            auto st = [] (int64_t n, int ch)
            {
                const double t = (double) n / 48000.0;
                const double mid = 0.3 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t);
                const double side = 0.15 * std::sin (juce::MathConstants<double>::twoPi * 1300.0 * t) + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 60.0 * t);
                return (float) (ch == 0 ? mid + side : mid - side);
            };
            for (float wd : { 100.0f, 200.0f })
                for (float o : { 6.0f, 12.0f })
                    for (int q : { 0, 1, 2 })
                    {
                        p.setCurrentProgram (0); setP (p, "width", wd); setP (p, "outputDb", o); setP (p, "quality", (float) q);
                        const auto y = render (p, st, 2.0, 1.0);
                        std::printf ("sines width %.0f out %+.0f q%d: TP L %.2f R %.2f\n", wd, o, q, juce::Decibels::gainToDecibels (truePeak (y.l)), juce::Decibels::gainToDecibels (truePeak (y.r)));
                    }
        }
        for (int qq = 0; qq < 3; ++qq)
        {
            p.setCurrentProgram (prog (p, "Total Control")); setP (p, "quality", (float) qq);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            render (p, drumMix (0.9f), 20.0, 30.0);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("CPU q%d: 20 s of stereo audio in %.0f ms (%.1f%% of one core, incl. test signal)\n", qq, ms, ms / 200.0);
        }
        return 0;
    }
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); std::fflush (stdout); if (! ok) ++failures; };
    const size_t N = 16384;

    DominatrixProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    check (p.getProgramName (p.getCurrentProgram()) == "Init", "opens on Init");

    // ---------------- ceiling holds, everywhere
    for (int ceil : { 0, 1 })
        for (int type = 0; type < 5; ++type)
            for (int q : { 0, 1, 2 })
            {
                p.setCurrentProgram (0);
                setP (p, "ceiling", (float) ceil); setP (p, "satType", (float) type); setP (p, "quality", (float) q);
                setP (p, "discipline", 100); setP (p, "saturation", 100); setP (p, "analog", 100); setP (p, "outputDb", 12);
                const auto y = render (p, drumMix (1.0f), 3.0, 1.0);
                const float lim = juce::Decibels::decibelsToGain (ceil == 0 ? -1.0f : -6.0f);
                const float pk = std::max (peak (y.l), peak (y.r)), tp = truePeak (y.l);
                check (pk <= lim + 1e-5f && juce::Decibels::gainToDecibels (tp / lim) < 0.3f,
                       juce::String::formatted ("ceiling %s holds: type %d q %d -> sample peak %.2f dBFS, true peak %.2f dBTP",
                                                ceil == 0 ? "-1" : "-6", type, q, juce::Decibels::gainToDecibels (pk), juce::Decibels::gainToDecibels (tp)));
            }

    // ---------------- transparency with everything off
    for (int q : { 0, 1, 2 })
    {
        allOff (p); setP (p, "quality", (float) q);
        for (float f : { 1000.0f, 12000.0f })
        {
            const auto y = render (p, sine (0.25f, f), 1.0, 0.5);
            const float h = f < 5000 ? harmonicsDb (trim (y.l, N), f) : -200.0f;
            const float gain = rmsDb (y.l) - juce::Decibels::gainToDecibels (0.25f / std::sqrt (2.0f));
            check (h < -90.0f && std::abs (gain) < (f < 5000 ? 0.05f : 0.15f),
                   juce::String::formatted ("everything at 0 is transparent (q %d, %.0f Hz): harmonics %.1f dB, gain %+.3f dB", q, f, h, gain));
        }
        check (p.getLatencySamples() == p.getEngine().latencyFor (q), juce::String::formatted ("latency reported for q %d: %d samples", q, p.getLatencySamples()));
    }
    {   // impulse lands exactly at the reported latency
        allOff (p);
        const auto y = render (p, [] (int64_t n, int) { return n == 1000 ? 0.5f : 0.0f; }, 0.2, 0.0);
        size_t at = 0; float m = 0;
        for (size_t i = 0; i < y.l.size(); ++i) if (std::abs (y.l[i]) > m) { m = std::abs (y.l[i]); at = i; }
        check (std::abs ((int) at - 1000 - p.getLatencySamples()) <= 1, juce::String::formatted ("impulse arrives at the reported latency (%d vs %d)", (int) at - 1000, p.getLatencySamples()));
    }
    {   // the soft clipper leaves signals under its knee alone
        allOff (p); setP (p, "ceiling", 0);
        const auto y = render (p, sine (0.5f, 1000.0f), 1.0, 0.5);
        check (harmonicsDb (trim (y.l, N), 1000.0f) < -90.0f, "clipper is transparent at -6 dBFS with the -1 dB ceiling");
    }

    // ---------------- DISCIPLINE
    {
        auto grFor = [&] (float d, float level)
        {
            allOff (p); setP (p, "discipline", d);
            render (p, drumMix (level), 6.0, 6.0);           // settle (prepare resets)
            p.getEngine().grDb.store (0);
            render (p, drumMix (level), 3.0, 3.0, 48000.0, false);
            return p.getEngine().grDb.load();
        };
        float last = -1.0f;
        for (float d : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            const float gr = grFor (d, 0.8f);
            std::printf ("   discipline %3.0f%%: peak gain reduction %.2f dB, %s\n", d, gr, d > 0 ? "" : "(off)");
            if (d == 0.0f) check (gr == 0.0f, "DISCIPLINE 0 does nothing");
            else check (gr > last + 0.2f && gr < 15.0f, juce::String::formatted ("DISCIPLINE %.0f compresses more", d));
            last = gr;
        }
        const float hot = grFor (60.0f, 0.9f), quiet = grFor (60.0f, 0.09f);
        check (std::abs (hot - quiet) < 1.5f, juce::String::formatted ("auto threshold: same GR at -20 dB input (%.2f vs %.2f dB)", hot, quiet));

        // auto makeup keeps the level in the same place
        allOff (p);
        const float ref = rmsDb (render (p, drumMix (0.5f), 6.0, 3.0).l);
        for (float d : { 30.0f, 60.0f, 100.0f })
        {
            allOff (p); setP (p, "discipline", d);
            const float lvl = rmsDb (render (p, drumMix (0.5f), 6.0, 3.0).l);
            check (std::abs (lvl - ref) < 2.0f, juce::String::formatted ("makeup holds the level at DISCIPLINE %.0f (%+.2f dB)", d, lvl - ref));
        }
        // readouts
        allOff (p); setP (p, "discipline", 50);
        render (p, drumMix (0.5f), 4.0, 4.0);
        auto& e = p.getEngine();
        std::printf ("   rules @50%%: thresh %.1f dB  ratio %.2f  attack %.1f ms  release %.0f ms  makeup %.2f dB\n",
                     e.threshDb.load(), e.ratioNow.load(), e.attackMs.load(), e.releaseMs.load(), e.makeupDb.load());
        check (e.attackMs.load() >= 10.0f && e.attackMs.load() <= 30.0f && e.ratioNow.load() > 2.5f && e.ratioNow.load() < 3.0f, "auto attack/ratio in range");
    }

    // ---------------- SATURATION types
    {
        const char* names[] = { "TAPE", "TUBE", "IRON", "CONSOLE", "WHIP" };
        auto satH = [&] (int type, float amt, float freq, float amp = 0.5f)
        {
            allOff (p); setP (p, "satType", (float) type); setP (p, "saturation", amt);
            return trim (render (p, sine (amp, freq), 1.0, 0.5).l, N);
        };
        for (int type = 0; type < 5; ++type)
        {
            float last = -300.0f;
            juce::String line = juce::String ("   ") + names[type] + ":";
            for (float a : { 0.0f, 25.0f, 50.0f, 100.0f })
            {
                const float h = harmonicsDb (satH (type, a, 500.0f), 500.0f);
                line << juce::String::formatted ("  %3.0f%% %.1f dB", a, h);
                if (a == 0.0f) check (h < -90.0f, juce::String (names[type]) + " at SATURATION 0 is clean");
                else check (h > last + 1.0f, juce::String::formatted ("%s %.0f adds more harmonics", names[type], a));
                last = h;
            }
            std::printf ("%s\n", line.toRawUTF8());
        }
        {
            const auto y = satH (1, 60.0f, 500.0f);
            const float h2 = harmonicDb (y, 500.0f, 2), h3 = harmonicDb (y, 500.0f, 3);
            check (h2 > h3 + 3.0f, juce::String::formatted ("TUBE is mostly 2nd harmonic (H2 %.1f, H3 %.1f dB)", h2, h3));
        }
        {
            const float lo = harmonicsDb (satH (2, 60.0f, 60.0f), 60.0f), hi = harmonicsDb (satH (2, 60.0f, 1500.0f), 1500.0f);
            check (lo > hi + 6.0f, juce::String::formatted ("IRON saturates the lows more (60 Hz %.1f vs 1.5 kHz %.1f dB)", lo, hi));
        }
        {   // tape squashes loud highs more than loud lows (HF compression)
            auto loss = [&] (float f) { allOff (p); const float ref = peak (render (p, sine (0.7f, f), 1.0, 0.5).l);
                                        return juce::Decibels::gainToDecibels (peak (satH (0, 100.0f, f, 0.7f)) / ref); };
            const float lo = loss (300.0f), hi = loss (6000.0f);
            check (hi < lo - 0.5f, juce::String::formatted ("TAPE compresses loud highs more (6 kHz %+.2f vs 300 Hz %+.2f dB)", hi, lo));
        }
        for (int type = 0; type < 5; ++type)
        {   // SATURATION adds harmonics, not a volume change
            allOff (p);
            const float ref = rmsDb (render (p, drumMix (0.5f), 3.0, 1.0).l);
            allOff (p); setP (p, "satType", (float) type); setP (p, "saturation", 100);
            const float lvl = rmsDb (render (p, drumMix (0.5f), 3.0, 1.0).l);
            check (std::abs (lvl - ref) < 2.0f, juce::String::formatted ("%s 100 keeps the level (%+.2f dB)", names[type], lvl - ref));
        }
        {
            const float con = harmonicsDb (satH (3, 100.0f, 500.0f), 500.0f), whp = harmonicsDb (satH (4, 100.0f, 500.0f), 500.0f);
            check (whp > con + 6.0f, juce::String::formatted ("WHIP bites harder than CONSOLE (%.1f vs %.1f dB)", whp, con));
        }
    }

    // ---------------- ANALOG
    {
        allOff (p); setP (p, "analog", 100);
        const auto quiet = render (p, [] (int64_t, int) { return 0.0f; }, 2.0, 1.0);
        const float nf = rmsDb (quiet.l);
        check (nf > -100.0f && nf < -70.0f, juce::String::formatted ("ANALOG 100 has a vintage noise floor (%.1f dBFS RMS)", nf));
        allOff (p);
        const float clean = rmsDb (render (p, drumMix (0.5f), 3.0, 1.0).l);
        allOff (p); setP (p, "analog", 100);
        const auto a = render (p, drumMix (0.5f), 3.0, 1.0);
        check (std::abs (rmsDb (a.l) - clean) < 1.0f, juce::String::formatted ("ANALOG keeps the level (%+.2f dB)", rmsDb (a.l) - clean));
        double diffLR = 0; for (size_t i = 0; i < a.l.size(); ++i) diffLR = std::max (diffLR, (double) std::abs (a.l[i] - a.r[i]));
        const auto s = render (p, sine (0.5f, 1000.0f), 1.0, 0.5);
        const float thd = harmonicsDb (trim (s.l, N), 1000.0f);
        check (thd > -75.0f && thd < -35.0f, juce::String::formatted ("ANALOG 100 adds a touch of harmonics (%.1f dB)", thd));
        auto gainAt = [&] (float f) { allOff (p); setP (p, "analog", 100); return rmsDb (render (p, sine (0.25f, f), 1.0, 0.5).l) - juce::Decibels::gainToDecibels (0.25f / std::sqrt (2.0f)); };
        const float bump = gainAt (70.0f), top = gainAt (16000.0f);
        check (bump > 0.3f && top < -0.8f && top > -4.0f, juce::String::formatted ("ANALOG 100 tone: head bump %+.2f dB @70 Hz, top end %+.2f dB @16 kHz", bump, top));
        // wow & flutter: the 1 kHz tone's zero crossings wander
        allOff (p); setP (p, "analog", 100);
        const auto w = render (p, sine (0.5f, 3000.0f), 4.0, 1.0);
        std::vector<double> zc;
        for (size_t i = 1; i < w.l.size(); ++i) if (w.l[i - 1] < 0 && w.l[i] >= 0) zc.push_back ((double) i - 1 + w.l[i - 1] / (w.l[i - 1] - w.l[i]));
        double maxDev = 0;
        for (size_t i = 1; i < zc.size(); ++i) maxDev = std::max (maxDev, std::abs ((zc[i] - zc[i - 1]) - 16.0) / 16.0);
        check (maxDev > 0.0001 && maxDev < 0.005, juce::String::formatted ("ANALOG wow & flutter is subtle (max pitch deviation %.3f%%)", maxDev * 100.0));
    }

    // ---------------- MIX
    {
        allOff (p); setP (p, "discipline", 0); setP (p, "saturation", 0); setP (p, "mix", 50);
        const auto y = render (p, sine (0.25f, 10000.0f), 1.0, 0.5);
        const float g = rmsDb (y.l) - juce::Decibels::gainToDecibels (0.25f / std::sqrt (2.0f));
        check (std::abs (g) < 0.15f, juce::String::formatted ("MIX 50 doesn't comb (10 kHz %+.2f dB)", g));
        allOff (p); setP (p, "discipline", 100); setP (p, "saturation", 100); setP (p, "mix", 0);
        const auto d = render (p, drumMix (0.5f), 3.0, 1.0);
        allOff (p);
        const auto ref = render (p, drumMix (0.5f), 3.0, 1.0);
        float err = 0; for (size_t i = 0; i < d.l.size(); ++i) err = std::max (err, std::abs (d.l[i] - ref.l[i]));
        check (err < 1e-5f, juce::String::formatted ("MIX 0 is the dry signal (max diff %.2g)", err));
    }

    // ---------------- OUTPUT drives the clipper
    {
        float last = -1.0f;
        for (float o : { 0.0f, 4.0f, 7.0f, 10.0f })
        {
            allOff (p); setP (p, "ceiling", 0); setP (p, "outputDb", o);
            p.getEngine().clipDb.store (0);
            const auto y = render (p, sine (0.5f, 200.0f), 1.0, 0.5);
            const float c = p.getEngine().clipDb.load();
            std::printf ("   output %+.0f dB -> clipping %.2f dB, peak %.2f dBFS\n", o, c, juce::Decibels::gainToDecibels (peak (y.l)));
            if (o > 0) check (c > last + 0.5f, juce::String::formatted ("more OUTPUT = more clipping (+%.0f dB)", o));
            last = c;
        }
        allOff (p); setP (p, "outputDb", 6);
        const auto y = render (p, sine (0.5f, 200.0f), 1.0, 0.5);
        check (std::abs (juce::Decibels::gainToDecibels (peak (y.l) / 0.5f) - 6.0f) < 0.1f, "CEILING OFF: OUTPUT is plain gain");
    }

    // ---------------- MATCH
    {
        allOff (p); setP (p, "ceiling", 0); setP (p, "outputDb", 9); setP (p, "discipline", 60); setP (p, "match", 1);
        const auto y = render (p, drumMix (0.5f), 10.0, 7.0);
        allOff (p);
        const auto ref = render (p, drumMix (0.5f), 10.0, 7.0);
        check (std::abs (rmsDb (y.l) - rmsDb (ref.l)) < 1.5f, juce::String::formatted ("MATCH plays back at the input's loudness (%+.2f dB)", rmsDb (y.l) - rmsDb (ref.l)));
        setP (p, "match", 0);
    }

    // ---------------- LUFS
    {
        allOff (p);
        render (p, sine (0.1f, 1000.0f), 6.0, 6.0);
        auto& L = p.getEngine().getLoudness();
        const float integ = L.computeIntegrated(), st = L.getShortTerm();
        check (std::abs (integ + 20.0f) < 0.3f && std::abs (st + 20.0f) < 0.3f,
               juce::String::formatted ("LUFS: -20 dBFS 1 kHz stereo reads -20 LUFS (integrated %.2f, short-term %.2f)", integ, st));
    }

    // ---------------- WIDTH
    {
        // a stereo test signal: shared centre + different sides (incl. a low side component)
        auto st = [] (int64_t n, int ch)
        {
            const double t = (double) n / 48000.0;
            const double mid = 0.3 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t);
            const double side = 0.15 * std::sin (juce::MathConstants<double>::twoPi * 1300.0 * t) + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 60.0 * t);
            return (float) (ch == 0 ? mid + side : mid - side);
        };
        auto sideOf = [] (const Render& r, float lpHz)
        {   // RMS of the side signal; lpHz > 0 isolates the 60 Hz part, < 0 the 1.3 kHz part
            std::vector<float> s (r.l.size());
            for (size_t i = 0; i < s.size(); ++i) s[i] = 0.5f * (r.l[i] - r.r[i]);
            return (float) juce::Decibels::gainToDecibels (dftMag (trim (s, 16384), lpHz > 0 ? 60.0 : 1300.0, 48000.0) + 1e-12);
        };
        allOff (p);
        const auto ref = render (p, st, 1.5, 0.75);
        allOff (p); setP (p, "width", 100);
        const auto same = render (p, st, 1.5, 0.75);
        float err = 0; for (size_t i = 0; i < same.l.size(); ++i) err = std::max ({ err, std::abs (same.l[i] - ref.l[i]), std::abs (same.r[i] - ref.r[i]) });
        check (err == 0.0f, "WIDTH 100 changes nothing");

        allOff (p); setP (p, "width", 0);
        const auto mono = render (p, st, 1.5, 0.75);
        float d = 0; for (size_t i = 0; i < mono.l.size(); ++i) d = std::max (d, std::abs (mono.l[i] - mono.r[i]));
        check (d < 1e-4f, juce::String::formatted ("WIDTH 0 is mono (max L-R %.2g)", d));

        allOff (p); setP (p, "width", 160);
        const auto wide = render (p, st, 3.0, 2.0);
        const float hiRef = sideOf (ref, -1), hiWide = sideOf (wide, -1), loRef = sideOf (ref, 1), loWide = sideOf (wide, 1);
        const float midRef = (float) juce::Decibels::gainToDecibels (dftMag (trim (ref.l, 16384), 440.0, 48000.0));
        const float midWide = (float) juce::Decibels::gainToDecibels (dftMag (trim (wide.l, 16384), 440.0, 48000.0));
        std::printf ("   width 160: side @1.3k %+.2f dB, side @60 Hz %+.2f dB, centre %+.2f dB\n", hiWide - hiRef, loWide - loRef, midWide - midRef);
        check (hiWide - hiRef > 2.0f, "WIDTH 160 widens the sides");
        check (std::abs ((loWide - loRef) - (midWide - midRef)) < 1.0f, "WIDTH never widens the lows (they follow the centre)");
        check (std::abs (rmsDb (wide.l) - rmsDb (ref.l)) < 1.5f, juce::String::formatted ("WIDTH keeps the loudness (%+.2f dB)", rmsDb (wide.l) - rmsDb (ref.l)));

        allOff (p); setP (p, "bassMono", 1);
        const auto bm = render (p, st, 1.5, 0.75);
        check (sideOf (bm, 1) - loRef < -20.0f && std::abs (sideOf (bm, -1) - hiRef) < 0.5f,
               juce::String::formatted ("BASS MONO removes the low side (%.1f dB) and leaves the rest", sideOf (bm, 1) - loRef));

        for (int ceil : { 0, 1 })
        {
            p.setCurrentProgram (0); setP (p, "ceiling", (float) ceil); setP (p, "width", 200); setP (p, "outputDb", 12);
            const auto y = render (p, st, 2.0, 1.0);
            const float lim = juce::Decibels::decibelsToGain (ceil == 0 ? -1.0f : -6.0f);
            const float tp = std::max (truePeak (y.l), truePeak (y.r));
            check (std::max (peak (y.l), peak (y.r)) <= lim + 1e-5f && juce::Decibels::gainToDecibels (tp / lim) < 0.3f,
                   juce::String::formatted ("ceiling holds at WIDTH 200 (%s, sample peak %.2f, true peak %.2f dBTP)", ceil == 0 ? "-1" : "-6", juce::Decibels::gainToDecibels (std::max (peak (y.l), peak (y.r))), juce::Decibels::gainToDecibels (tp)));
        }
        // phase meter
        allOff (p); render (p, sine (0.3f, 500.0f), 1.0, 1.0);
        const float c1 = p.getEngine().correlation.load();
        allOff (p); render (p, [] (int64_t n, int ch) { return (ch ? -0.3f : 0.3f) * (float) std::sin (juce::MathConstants<double>::twoPi * 500.0 * (double) n / 48000.0); }, 1.0, 1.0);
        const float c2 = p.getEngine().correlation.load();
        check (c1 > 0.99f && c2 < -0.99f, juce::String::formatted ("phase meter reads +1 for mono, -1 for out of phase (%.2f, %.2f)", c1, c2));
    }

    // ---------------- presets
    for (int i = 0; i < p.getNumPrograms(); ++i)
    {
        p.setCurrentProgram (i);
        const int ceil = (int) p.apvts.getRawParameterValue ("ceiling")->load();
        const auto y = render (p, drumMix (1.0f), 2.0, 0.5);
        const float pk = std::max (peak (y.l), peak (y.r));
        if (ceil != 2) check (pk <= juce::Decibels::decibelsToGain (ceil == 0 ? -1.0f : -6.0f) + 1e-5f,
                              juce::String::formatted ("preset '%s' holds its ceiling (%.2f dBFS)", p.getProgramName (i).toRawUTF8(), juce::Decibels::gainToDecibels (pk)));
        else check (std::isfinite (pk) && pk > 0.0f, "preset '" + p.getProgramName (i) + "' renders");
    }
    {
        p.setCurrentProgram (0);
        setP (p, "satType", 2); setP (p, "discipline", 67.5f);
        check (p.saveUserPreset ("__test dom"), "save user preset");
        const auto f = DominatrixProcessor::fileForPresetName ("__test dom");
        check (f.existsAsFile() && f.getFullPathName().contains ("Castle Danger"), "preset file in the Castle Danger folder: " + f.getFullPathName());
        p.setCurrentProgram (0);
        check (p.loadUserPreset (f) && p.apvts.getRawParameterValue ("satType")->load() == 2.0f
               && std::abs (p.apvts.getRawParameterValue ("discipline")->load() - 67.5f) < 0.01f, "load user preset");
        f.deleteFile();
    }

    // ---------------- GUI
    {
        DominatrixProcessor q;
        q.setPlayConfigDetails (2, 2, 48000.0, 512);
        q.setCurrentProgram (4); // Loud & Proud
        render (q, drumMix (0.9f), 4.0, 4.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
        int64_t t = 0;
        auto run = [&] (const Gen& g, int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                juce::AudioBuffer<float> buf (2, 1600); juce::MidiBuffer midi;
                for (int s = 0; s < 1600; ++s) for (int c = 0; c < 2; ++c) buf.setSample (c, s, g (t + s, c));
                t += 1600;
                q.processBlock (buf, midi);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
            }
        };
        run (drumMix (0.9f), 40);
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f), dir.getChildFile ("gui.png")), "main snapshot");

        q.setCurrentProgram (prog (q, "Total Control"));
        run (drumMix (0.9f), 30);
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), dir.getChildFile ("gui_heavy.png")), "heavy snapshot");

        q.setCurrentProgram (prog (q, "Wide Screen"));
        run (drumMix (0.7f), 20);
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), dir.getChildFile ("gui_wide.png")), "wide snapshot");
        q.setCurrentProgram (prog (q, "Safe Word (Clipper Only)"));
        setP (q, "ceiling", 2); setP (q, "match", 1);
        run (drumMix (0.5f), 20);
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), dir.getChildFile ("gui_off.png")), "clipper off / match snapshot");
        setP (q, "match", 0);

        if (auto* de = dynamic_cast<DominatrixEditor*> (ed.get())) de->getSaveButton().triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), dir.getChildFile ("gui_save.png")), "save dialog snapshot");
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
