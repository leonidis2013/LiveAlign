// Tests for the LiveAlign measurement engine (plain C++, no JUCE). Known signals in, known answers out.
//   g++ -std=c++17 -O2 -ISource Tests/AnalysisTests.cpp -o analysis_tests && ./analysis_tests
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "Analysis/Fft.h"
#include "Analysis/Banding.h"
#include "Analysis/TransferEngine.h"
#include "Analysis/RtaEngine.h"
#include "Analysis/SplMeter.h"
#include "Analysis/DelayFinder.h"
#include "Analysis/FreqTable.h"
#include "Analysis/Generator.h"
#include "Analysis/TargetCurves.h"
#include "Analysis/Crossover.h"

using namespace la;

static int failures = 0, checks = 0;

static void check (bool ok, const std::string& what, double got = NAN, double want = NAN)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL  %s  (got %.4f, want %.4f)\n", what.c_str(), got, want);
    }
}

static void near (double got, double want, double tol, const std::string& what)
{
    check (std::isfinite (got) && std::abs (got - want) <= tol, what, got, want);
}

struct Biquad   // RBJ cookbook low-pass, direct form I
{
    double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    Biquad (double fs, double fc, double q)
    {
        const double w = 2.0 * kPi * fc / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 - c) / 2.0 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    double operator() (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

static std::vector<float> noise (int n, unsigned seed, double rms = 0.1)
{
    std::mt19937 g (seed);
    std::normal_distribution<double> d (0.0, rms);
    std::vector<float> v ((size_t) n);
    for (auto& s : v) s = (float) d (g);
    return v;
}

static std::vector<float> delayed (const std::vector<float>& x, int d, float gain = 1.0f)
{
    std::vector<float> y (x.size(), 0.0f);
    for (size_t i = (size_t) d; i < x.size(); ++i) y[i] = gain * x[i - (size_t) d];
    return y;
}

static size_t nearestIndex (const std::vector<double>& f, double target)
{
    size_t best = 0;
    for (size_t i = 0; i < f.size(); ++i)
        if (std::abs (std::log (f[i] / target)) < std::abs (std::log (f[best] / target))) best = i;
    return best;
}

static void testFft()
{
    const int n = 1024;
    Fft fft (n);
    std::vector<cplx> d ((size_t) n);
    for (int i = 0; i < n; ++i) d[(size_t) i] = cplx (std::sin (2.0 * kPi * 37.0 * i / n), 0.0);
    auto orig = d;
    fft.forward (d.data());
    near (std::abs (d[37]), n / 2.0, 1e-6, "FFT: sine lands in its bin");
    near (std::abs (d[36]), 0.0, 1e-6, "FFT: neighbour bin empty");
    fft.inverse (d.data());
    double err = 0.0;
    for (int i = 0; i < n; ++i) err = std::max (err, std::abs (d[(size_t) i] - orig[(size_t) i]));
    near (err, 0.0, 1e-9, "FFT: round trip");
}

static void testPowerSpectrum()
{
    const double fs = 48000.0;
    PowerSpectrum ps;
    ps.prepare (fs, 16384, 2048);
    ps.setTimeConstant (0.0);
    const double amp = 0.25;   // -12.04 dBFS
    std::vector<float> x (65536);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2.0 * kPi * 1000.0 * i / fs));
    ps.push (x.data(), (int) x.size());
    double total = 0.0;
    for (double p : ps.averaged()) total += p;
    near (powerToDbfs (total), 20.0 * std::log10 (amp), 0.05, "RTA: sine total level (Parseval)");

    std::vector<double> centres { 1000.0 }, out;
    PowerSpectrum::bands (ps.averaged(), ps.binHz(), centres, 1.0 / 3.0, out);
    near (powerToDbfs (out[0]), 20.0 * std::log10 (amp), 0.1, "RTA: 1/3-octave band holds the 1 kHz sine");

    // White noise: band power = variance * bandwidth / (fs / 2)
    PowerSpectrum pn;
    pn.prepare (fs, 16384, 2048);
    pn.setTimeConstant (100.0);
    auto w = noise (48000 * 20, 7, 0.1);
    pn.push (w.data(), (int) w.size());
    std::vector<double> oct { 1000.0 }, ob;
    PowerSpectrum::bands (pn.averaged(), pn.binHz(), oct, 1.0, ob);
    const double bw = 1000.0 * (std::sqrt (2.0) - 1.0 / std::sqrt (2.0));
    near (10.0 * std::log10 (ob[0]), 10.0 * std::log10 (0.01 * bw / (fs / 2.0)), 0.3, "RTA: white noise octave band power");
}

static void testTransferGainDelay()
{
    const double fs = 48000.0;
    TransferEngine te;
    te.prepare (fs, 16384);
    te.setAverages (16);
    te.setDelaySamples (100);
    auto ref = noise (48000 * 6, 11);
    auto mic = delayed (ref, 100, 0.5f);
    te.push (mic.data(), ref.data(), (int) ref.size());
    check (te.frames() > 20, "TF: frames were processed", te.frames(), 20);

    auto f = logGrid (20.0, 20000.0, 12);
    std::vector<float> mag, ph, coh;
    te.banded (f, 1.0 / 24.0, mag, ph, coh);
    for (double tf : { 50.0, 1000.0, 10000.0 })
    {
        const size_t i = nearestIndex (f, tf);
        near (mag[i], -6.0206, 0.05, "TF: -6 dB gain at " + std::to_string ((int) tf) + " Hz");
        near (ph[i], 0.0, 1.0, "TF: zero phase with delay compensated at " + std::to_string ((int) tf) + " Hz");
        near (coh[i], 1.0, 0.01, "TF: coherence 1 for a clean path at " + std::to_string ((int) tf) + " Hz");
    }

    // Without compensation the impulse response peaks at the true delay.
    TransferEngine t2;
    t2.prepare (fs, 16384);
    t2.setAverages (8);
    t2.push (mic.data(), ref.data(), (int) ref.size());
    std::vector<float> ir;
    t2.impulse (ir);
    size_t pk = 0;
    for (size_t i = 0; i < ir.size(); ++i) if (std::abs (ir[i]) > std::abs (ir[pk])) pk = i;
    near ((double) pk, 100.0, 0.0, "TF: impulse response peak at the delay");
    near (ir[pk], 0.5, 0.02, "TF: impulse response peak height = gain");
}

static void testTransferFilter()
{
    const double fs = 48000.0;
    TransferEngine te;
    te.prepare (fs, 32768);
    te.setAverages (16);
    auto ref = noise (48000 * 10, 3);
    std::vector<float> mic (ref.size());
    Biquad lp (fs, 1000.0, std::sqrt (0.5));
    for (size_t i = 0; i < ref.size(); ++i) mic[i] = (float) lp (ref[i]);
    te.push (mic.data(), ref.data(), (int) ref.size());

    std::vector<double> f { 100.0, 1000.0, 4000.0 };
    std::vector<float> mag, ph, coh;
    te.banded (f, 1.0 / 48.0, mag, ph, coh);
    near (mag[0], 0.0, 0.1, "TF: Butterworth low-pass flat at 100 Hz");
    near (mag[1], -3.01, 0.15, "TF: Butterworth low-pass -3 dB at fc");
    near (ph[1], -90.0, 2.0, "TF: Butterworth low-pass -90 degrees at fc");
    // Analytic response of the bilinear-transformed filter at 4 kHz
    const double w = 2.0 * kPi * 4000.0 / fs;
    const cplx z = std::polar (1.0, -w);
    const double c = std::cos (2.0 * kPi * 1000.0 / fs), al = std::sin (2.0 * kPi * 1000.0 / fs) / (2.0 * std::sqrt (0.5));
    const double a0 = 1.0 + al;
    const cplx h = ((1.0 - c) / 2.0 / a0 * (1.0 + 2.0 * z + z * z)) / (1.0 - 2.0 * c / a0 * z + (1.0 - al) / a0 * z * z);
    near (mag[2], 20.0 * std::log10 (std::abs (h)), 0.3, "TF: Butterworth low-pass at 4 kHz matches theory");
}

static void testCoherence()
{
    const double fs = 48000.0;
    TransferEngine te;
    te.prepare (fs, 8192);
    te.setAverages (64);
    auto ref = noise (48000 * 30, 21);
    auto n2 = noise (48000 * 30, 99);
    std::vector<float> mic (ref.size());
    for (size_t i = 0; i < ref.size(); ++i) mic[i] = ref[i] + n2[i];   // 0 dB signal-to-noise
    te.push (mic.data(), ref.data(), (int) ref.size());
    std::vector<double> f { 1000.0, 5000.0 };
    std::vector<float> mag, ph, coh;
    te.banded (f, 1.0 / 3.0, mag, ph, coh);
    near (coh[0], 0.5, 0.08, "TF: coherence 0.5 at 0 dB signal-to-noise (1 kHz)");
    near (coh[1], 0.5, 0.08, "TF: coherence 0.5 at 0 dB signal-to-noise (5 kHz)");
    near (mag[0], 0.0, 0.5, "TF: magnitude unbiased by uncorrelated noise");
}

static void testDelayFinder()
{
    const double fs = 48000.0;
    auto ref = noise (1 << 17, 5);
    auto mic = delayed (ref, 1234, 0.3f);
    auto room = noise (1 << 17, 6, 0.02);
    for (size_t i = 0; i < mic.size(); ++i) mic[i] += room[i];
    auto r = findDelay (ref.data(), mic.data(), (int) ref.size(), fs * 1.0, fs * 0.05);
    check (r.valid, "Delay: found", r.peakToNoiseDb, 12.0);
    near (r.samples, 1234.0, 0.5, "Delay: 1234 samples");
    check (! r.inverted, "Delay: polarity normal");

    auto inv = delayed (ref, 480, -0.5f);
    auto ri = findDelay (ref.data(), inv.data(), (int) ref.size(), fs, fs * 0.05);
    near (ri.samples, 480.0, 0.5, "Delay: inverted path delay");
    check (ri.inverted, "Delay: inverted polarity detected");

    // Sub-like path: 4th-order low-pass at 100 Hz plus room noise. The peak lands within a few ms of the
    // pure delay (the filter's own group delay adds to it).
    std::vector<float> sub (ref.size(), 0.0f);
    Biquad a (fs, 100.0, 0.7071), b (fs, 100.0, 0.7071);
    for (size_t i = 2400; i < ref.size(); ++i) sub[i] = (float) b (a (ref[i - 2400]));
    auto noiseSub = noise (1 << 17, 8, 0.002);
    for (size_t i = 0; i < sub.size(); ++i) sub[i] += noiseSub[i];
    auto rs = findDelay (ref.data(), sub.data(), (int) ref.size(), fs, fs * 0.05);
    check (rs.valid, "Delay: band-limited (sub) path found", rs.peakToNoiseDb, 12.0);
    check (rs.samples >= 2400.0 && rs.samples < 2400.0 + 0.006 * fs, "Delay: band-limited path within 6 ms after the pure delay",
           rs.samples, 2400.0);

    std::vector<float> silence (ref.size(), 0.0f);
    auto rn = findDelay (ref.data(), silence.data(), (int) ref.size(), fs, 0.0);
    check (! rn.valid, "Delay: nothing found in silence");
}

static void testWeightingAndSpl()
{
    near (aWeightDb (1000.0), 0.0, 0.02, "A-weighting 1 kHz");
    near (aWeightDb (100.0), -19.1, 0.1, "A-weighting 100 Hz");
    near (aWeightDb (10000.0), -2.5, 0.1, "A-weighting 10 kHz");
    near (cWeightDb (1000.0), 0.0, 0.02, "C-weighting 1 kHz");
    near (cWeightDb (31.5), -3.0, 0.1, "C-weighting 31.5 Hz");

    const double fs = 48000.0;
    for (double f : { 1000.0, 100.0 })
    {
        SplMeter m;
        m.prepare (fs);
        std::vector<float> x (48000 * 4);
        const double amp = 0.1;   // -20 dBFS
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2.0 * kPi * f * i / fs));
        m.push (x.data(), (int) x.size());
        const std::string fs_ = std::to_string ((int) f) + " Hz";
        near (m.slowDb (SplMeter::Z), -20.0, 0.15, "SPL: Z slow, -20 dBFS sine at " + fs_);
        near (m.slowDb (SplMeter::A), -20.0 + aWeightDb (f), 0.15, "SPL: A slow at " + fs_);
        near (m.fastDb (SplMeter::C), -20.0 + cWeightDb (f), 0.15, "SPL: C fast at " + fs_);
        near (m.leqDb (SplMeter::Z), -20.0, 0.3, "SPL: Leq Z at " + fs_);
    }
}

static void testGenerator()
{
    const double fs = 48000.0;
    Generator g;
    g.prepare (fs);
    g.setType (GenType::Pink);
    g.setLevelDb (-20.0);
    g.setOn (true);
    std::vector<float> x (48000 * 20);
    g.process (x.data(), (int) x.size());
    double ms = 0.0;
    for (size_t i = 4800; i < x.size(); ++i) ms += (double) x[i] * x[i];
    ms /= (double) (x.size() - 4800);
    near (powerToDbfs (ms), -20.0, 0.3, "Generator: pink noise RMS level");

    PowerSpectrum ps;
    ps.prepare (fs, 16384, 4096);
    ps.setTimeConstant (100.0);
    ps.push (x.data(), (int) x.size());
    std::vector<double> centres { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 }, b;
    PowerSpectrum::bands (ps.averaged(), ps.binHz(), centres, 1.0, b);
    const double slope = (10.0 * std::log10 (b.back()) - 10.0 * std::log10 (b.front())) / 6.0;
    near (slope, 0.0, 0.5, "Generator: pink noise equal energy per octave");

    Generator s;
    s.prepare (fs);
    s.setType (GenType::Sine);
    s.setFrequency (1000.0);
    s.setLevelDb (-6.0);
    s.setOn (true);
    std::vector<float> y (48000);
    s.process (y.data(), (int) y.size());
    float pk = 0.0f;
    for (size_t i = 2400; i < y.size(); ++i) pk = std::max (pk, std::abs (y[i]));
    near (20.0 * std::log10 (pk), -6.0, 0.05, "Generator: sine peak level");

    s.setOn (false);
    s.process (y.data(), 2000);
    check (s.isSilent(), "Generator: ramps to silence within 20 ms");
    near (y[1999], 0.0, 1e-9, "Generator: silent after ramp");

    Generator sw;
    sw.prepare (fs);
    sw.setType (GenType::Sweep);
    sw.setSweepSeconds (2.0);
    sw.setLevelDb (-10.0);
    sw.setOn (true);
    std::vector<float> z (48000 * 3);
    sw.process (z.data(), (int) z.size());
    float zp = 0.0f;
    for (float v : z) zp = std::max (zp, std::abs (v));
    near (20.0 * std::log10 (zp), -10.0, 0.1, "Generator: sweep peak level");
    near (z[(size_t) (48000 * 2.2)], 0.0, 1e-9, "Generator: sweep gap is silent");
}

static void testFreqTable()
{
    const std::string umik =
        "\"Sens Factor =-1.378dB, AGain =18dB, SERNO: 7023270\"\n"
        "10.054\t-2.5170\n"
        "100.000\t-0.2000\n"
        "1000.0\t0.0000\n"
        "10000\t1.5000\n";
    auto t = parseFreqTable (umik);
    check (t.freq.size() == 4, "Cal file: four points", (double) t.freq.size(), 4);
    check (t.hasSensFactor, "Cal file: sens factor found");
    near (t.sensFactorDb, -1.378, 1e-9, "Cal file: sens factor value");
    check (! t.hasPhase(), "Cal file: no phase column");
    near (t.dbAt (1000.0), 0.0, 1e-9, "Cal file: exact point");
    near (t.dbAt (std::sqrt (1000.0 * 10000.0)), 0.75, 1e-6, "Cal file: log-frequency interpolation");
    near (t.dbAt (5.0), -2.517, 1e-9, "Cal file: holds the end value below range");

    const std::string euro = "Freq;dB;Phase\n20,0;-1,5;10,0\n1000,0;0,0;0,0\n";
    auto e = parseFreqTable (euro);
    check (e.freq.size() == 2 && e.hasPhase(), "Decimal-comma file parsed with phase");
    near (e.db[0], -1.5, 1e-9, "Decimal-comma value");

    const std::string csv = "Frequency (Hz),Magnitude (dB),Phase (deg)\n100,80.5,-45\n200,81.0,-60\n";
    auto c = parseFreqTable (csv);
    check (c.freq.size() == 2 && c.hasPhase(), "CSV with header parsed");
    near (c.phase[1], -60.0, 1e-9, "CSV phase");

    FreqTable tr;
    tr.freq = { 100.0, 1000.0 };
    tr.db = { -3.0, 1.5 };
    tr.phase = { 45.0, -10.0 };
    tr.extra = { 0.9, 0.99 };
    tr.meta = { { "name", "Top left 15 ft" }, { "kind", "transfer" } };
    auto text = writeFrd (tr, { "LiveAlign trace" }, true);
    auto back = parseFreqTable (text);
    check (back.freq.size() == 2 && back.hasPhase() && back.extra.size() == 2, "Trace round trip: columns");
    check (back.metaValue ("name") == "Top left 15 ft", "Trace round trip: name");
    check (back.metaValue ("kind") == "transfer", "Trace round trip: kind");
    near (back.db[1], 1.5, 1e-3, "Trace round trip: value");
    near (back.extra[0], 0.9, 1e-3, "Trace round trip: coherence");
}

static void testBanding()
{
    auto c = octaveBandCentres (3, 20.0, 20000.0);
    check (c.size() == 31, "1/3-octave bands from 20 Hz to 20 kHz", (double) c.size(), 31);
    near (nominalFrequency (31.62), 31.5, 1e-9, "Nominal 31.5 Hz label");
    near (nominalFrequency (1587.4), 1600.0, 1e-9, "Nominal 1.6 kHz label");
    auto g = logGrid (10.0, 20480.0, 48);
    check (g.size() == 11 * 48 + 1, "Log grid size", (double) g.size(), 11 * 48 + 1);
}

static void testTargets()
{
    near (targets::db (6, 1000.0), 0.0, 1e-9, "X-curve 0 dB at 1 kHz");
    near (targets::db (6, 500.0), 0.0, 1e-9, "X-curve flat below 2 kHz");
    near (targets::db (6, 4000.0), -3.0, 1e-9, "X-curve -3 dB at 4 kHz");
    near (targets::db (6, 8000.0), -6.0, 1e-9, "X-curve -6 dB at 8 kHz");
    near (targets::db (1, 50.0), 0.0, 1e-9, "Flat target");
    near (targets::db (3, 4000.0), -2.0, 1e-9, "Gentle tilt -1 dB/oct");
    check (targets::db (4, 40.0) > 4.5 && targets::db (4, 40.0) < 6.5, "Worship target low-end lift", targets::db (4, 40.0), 5.5);
    check (targets::db (5, 40.0) > 8.0, "Concert target low-end lift", targets::db (5, 40.0), 9.0);
    check (targets::db (2, 50.0) < -10.0, "Speech target rolled off at 50 Hz", targets::db (2, 50.0), -12.0);
    for (int id = 0; id < targets::count(); ++id)
        near (targets::db (id, 1000.0), 0.0, 1e-9, std::string ("Target ") + targets::list()[(size_t) id].name + " is 0 dB at 1 kHz");
}

static double magDb (cplx h) { return 20.0 * std::log10 (std::abs (h)); }

static void testCrossover()
{
    using namespace la::xo;
    const Filter lr24 { LinkwitzRiley, 24, 100.0 };
    near (magDb (response (lr24, false, 100.0)), -6.02, 0.02, "LR24 low-pass -6 dB at the corner");
    near (magDb (response (lr24, true, 100.0)), -6.02, 0.02, "LR24 high-pass -6 dB at the corner");
    near (magDb (response (lr24, false, 400.0)), -48.2, 0.5, "LR24 low-pass 24 dB/oct two octaves up");
    double worst = 0.0;
    for (double f = 20.0; f < 2000.0; f *= 1.1)
        worst = std::max (worst, std::abs (magDb (response (lr24, true, f) + response (lr24, false, f))));
    near (worst, 0.0, 0.01, "LR24 high + low sum flat");

    const Filter bw12 { Butterworth, 12, 100.0 };
    near (magDb (response (bw12, false, 100.0)), -3.01, 0.02, "BW12 -3 dB at the corner");
    const Filter be24 { Bessel, 24, 100.0 };
    near (magDb (response (be24, false, 100.0)), -3.01, 0.02, "Bessel 24 -3 dB at the corner");
    check (! slopeValid (LinkwitzRiley, 18) && slopeValid (LinkwitzRiley, 24) && ! slopeValid (Bessel, 36), "Valid slopes");

    // LR12 needs one side inverted to sum flat
    const Filter lr12 { LinkwitzRiley, 12, 100.0 };
    near (magDb (response (lr12, true, 100.0) - response (lr12, false, 100.0)), 0.0, 0.01, "LR12 sums flat with one side inverted");

    // Synthetic pair: a top that rolls off below 70 Hz and a sub that rolls off above 160 Hz, sub arriving 2 ms late.
    const auto grid = analysisGrid();
    Response top, sub;
    top.f = sub.f = grid;
    const Filter topNat { Butterworth, 12, 70.0 }, subNat { Butterworth, 24, 160.0 };
    for (double f : grid)
    {
        top.h.push_back (response (topNat, true, f));
        sub.h.push_back (response (subNat, false, f) * std::polar (2.0, -2.0 * kPi * f * 0.002));   // +6 dB, 2 ms late
    }
    Capabilities caps;
    Options opt;
    const auto r = optimise (top, sub, caps, opt);
    check (r.valid, "Crossover found");
    check (r.filter.freq >= 60.0 && r.filter.freq <= 160.0, "Crossover frequency in the overlap", r.filter.freq, 100.0);
    near (r.gainLowDb, -6.0, 1.5, "Sub level matched to the top");
    check (r.costDb < 1.0, "Summed response close to flat", r.costDb, 0.5);
    check (r.efficiency > 0.85, "Summation efficiency high", r.efficiency, 0.95);

    // The same pair in a room: a reflection 4 ms late puts notches in both responses. The crossover still lands
    // in the overlap, not at the edge of the range.
    Response topRoom = top, subRoom = sub;
    for (size_t i = 0; i < grid.size(); ++i)
    {
        const cplx comb = 1.0 + 0.45 * std::polar (1.0, -2.0 * kPi * grid[i] * 0.004);
        topRoom.h[i] *= comb;
        subRoom.h[i] *= comb;
    }
    const auto rr = optimise (topRoom, subRoom, caps, opt);
    check (rr.valid && rr.filter.freq >= 60.0 && rr.filter.freq <= 160.0, "Crossover in the overlap despite a room notch", rr.filter.freq, 100.0);
    check (rr.efficiency > 0.85, "Room: sources meet in phase", rr.efficiency, 0.95);

    // Coherence: a stretch of the overlap measured in noise is not trusted, and the result says so
    Response topNoisy = top, subNoisy = sub;
    topNoisy.coh.assign (grid.size(), 0.95);
    subNoisy.coh.assign (grid.size(), 0.95);
    for (size_t i = 0; i < grid.size(); ++i)
        if (grid[i] > 75.0 && grid[i] < 130.0) { topNoisy.coh[i] = 0.2; subNoisy.coh[i] = 0.2; }
    Options strict = opt;
    strict.minCoherence = 0.5;
    const auto rn = optimise (topNoisy, subNoisy, caps, strict);
    check (rn.valid, "Crossover found with a noisy stretch");
    check (rn.reliability < 0.9, "Noisy stretch lowers the reliability", rn.reliability, 0.6);
    check (optimise (topNoisy, subNoisy, caps, opt).reliability > 0.99, "Coherence ignored when no minimum is set");

    FreqTable rel;
    for (double f = 20.0; f <= 20000.0; f *= 1.05)
    {
        rel.freq.push_back (f);
        rel.db.push_back (f < 40.0 ? -30.0 : 0.0);
        rel.extra.push_back (f > 5000.0 ? 0.3 : 0.9);
    }
    const double share = usableReliability (rel, 0.5);
    check (share > 0.6 && share < 0.85, "Usable reliability counts the trusted share of the useful range", share, 0.72);
    check (usableReliability (FreqTable {}, 0.5) < 0.0, "No coherence data: unknown reliability");

    // A measured table with delay compensation: phase must line up after fromTable
    FreqTable t;
    for (double f = 20.0; f <= 20000.0; f *= 1.05)
    {
        t.freq.push_back (f);
        t.db.push_back (0.0);
        double p = -360.0 * f * 0.001;   // 1 ms of residual delay, wrapped
        p = std::fmod (p, 360.0);
        if (p < -180.0) p += 360.0;
        t.phase.push_back (p);
    }
    const auto resp = fromTable (t, 5.0, 4.0, grid);   // measured with 5 ms used, reference is 4 ms -> 2 ms total
    size_t k = 0;
    while (grid[k] < 125.0) ++k;
    const double expected = -360.0 * grid[k] * 0.002;
    double diff = std::arg (resp.h[k]) * 180.0 / kPi - expected;
    diff = std::remainder (diff, 360.0);
    near (diff, 0.0, 2.0, "Measured phase brought to the timing reference");

    // Minimum phase from magnitude only: a 2nd-order Butterworth high-pass at 100 Hz is +90 degrees at the corner
    FreqTable mag;
    const Filter hp2 { Butterworth, 12, 100.0 };
    for (double f = 5.0; f <= 24000.0; f *= 1.02) { mag.freq.push_back (f); mag.db.push_back (magDb (response (hp2, true, f))); }
    addMinimumPhase (mag);
    check (mag.hasPhase(), "Minimum phase added");
    near (mag.phaseAt (100.0), 90.0, 4.0, "Minimum phase of a high-pass at its corner");
    near (mag.phaseAt (1000.0), std::arg (response (hp2, true, 1000.0)) * 180.0 / kPi, 3.0, "Minimum phase above the corner");
}

int main()
{
    testTargets();
    testFft();
    testBanding();
    testPowerSpectrum();
    testTransferGainDelay();
    testTransferFilter();
    testCoherence();
    testDelayFinder();
    testWeightingAndSpl();
    testGenerator();
    testFreqTable();
    testCrossover();

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
