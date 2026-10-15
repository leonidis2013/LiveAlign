// Tests for the LiveAlign measurement engine (plain C++, no JUCE). Known signals in, known answers out.
//   g++ -std=c++17 -O2 -ISource Tests/AnalysisTests.cpp -o analysis_tests && ./analysis_tests
#include <cmath>
#include <complex>
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
#include "Analysis/CrossoverPair.h"
#include "Analysis/RoomAcoustics.h"
#include "Analysis/FeedbackDetector.h"
#include "Analysis/Polarity.h"
#include "Analysis/Deconvolve.h"
#include "Analysis/EqFit.h"

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

static void testEqFit()
{
    using namespace la::eq;
    near (responseDb ({ 1000.0, -6.0, 2.0 }, 1000.0), -6.0, 0.01, "Peaking band: gain at its frequency");
    near (responseDb ({ 1000.0, -6.0, 2.0 }, 20.0), 0.0, 0.05, "Peaking band: flat far away");

    std::vector<double> f, meas, target;
    for (double x = 20.0; x <= 20000.0; x *= std::pow (2.0, 1.0 / 24.0)) f.push_back (x);
    // a room that is +6 dB around 250 Hz and 4 dB down over a wide area around 3 kHz, plus 2 dB louder overall
    const Band hump { 250.0, 6.0, 2.0 }, hole { 3000.0, -4.0, 0.8 };
    for (double x : f) { meas.push_back (2.0 + responseDb (hump, x) + responseDb (hole, x)); target.push_back (0.0); }
    Options o;
    o.fLo = 60.0;
    o.fHi = 12000.0;
    o.maxBoostDb = 6.0;
    const auto r = fit (f, meas, target, o);
    check (! r.bands.empty(), "EQ: bands suggested");
    bool cut250 = false, boost3k = false;
    for (const auto& b : r.bands)
    {
        cut250 |= b.freq > 200.0 && b.freq < 310.0 && b.gainDb < -4.5;
        boost3k |= b.freq > 2000.0 && b.freq < 4500.0 && b.gainDb > 2.0;
    }
    check (cut250, "EQ: cut at the 250 Hz hump");
    check (boost3k, "EQ: boost for the wide 3 kHz dip");
    near (r.offsetDb, 2.0, 1.0, "EQ: overall level matched, not equalised");
    check (r.maxAfter < 1.5, "EQ: within 1.5 dB of the target after", r.maxAfter, 1.0);

    // a narrow, deep dip (a reflection) is not boosted
    std::vector<double> notch;
    const Band null { 1000.0, -12.0, 12.0 };
    for (double x : f) notch.push_back (responseDb (null, x));
    Options o2;
    o2.maxBoostDb = 6.0;
    const auto r2 = fit (f, notch, target, o2);
    bool boosted = false;
    for (const auto& b : r2.bands) boosted |= b.gainDb > 0.0 && b.freq > 700.0 && b.freq < 1400.0;
    check (! boosted, "EQ: no boost into a narrow null");

    // limits: no boost allowed, cuts limited
    Options o3;
    o3.maxBoostDb = 0.0;
    o3.maxCutDb = 3.0;
    const auto r3 = fit (f, meas, target, o3);
    bool ok = true;
    for (const auto& b : r3.bands) ok = ok && b.gainDb <= 0.0 && b.gainDb >= -3.0;
    check (ok, "EQ: within the processor's limits");

    // small, narrow wiggles are left alone; a real hump beside them is still found
    std::vector<double> wiggle;
    const Band tiny { 800.0, 1.6, 8.0 }, real { 4000.0, 5.0, 1.5 };
    for (double x : f) wiggle.push_back (responseDb (tiny, x) + responseDb (real, x));
    const auto r4 = fit (f, wiggle, target, Options {});
    bool noTiny = true, gotReal = false;
    for (const auto& b : r4.bands)
    {
        noTiny = noTiny && ! (b.freq > 600.0 && b.freq < 1100.0);
        gotReal |= b.freq > 3000.0 && b.freq < 5500.0 && b.gainDb < -3.5;
        noTiny = noTiny && std::abs (b.gainDb) >= 1.0;
    }
    check (noTiny, "EQ: no band for a small narrow wiggle");
    check (gotReal, "EQ: the real hump next to it is cut");
}

// The crossover pair follows the roles, not the order of the list.
static void testCrossoverPair()
{
    using namespace la::xo;
    // Sub, Main L, Main R (in that order): high = Main L, low = Sub
    const std::vector<int> subFirst { kRoleSub, kRoleMain, kRoleMain };
    const auto p = defaultPair (subFirst);
    check (p.high == 1 && p.low == 0, "Crossover pair: Sub + Main L + Main R gives HIGH = Main L, LOW = Sub", p.high * 10 + p.low, 10);
    check (! pairInverted (subFirst, p), "Crossover pair: the default pair is not inverted");

    // the usual order still works, and a fill does not take the sub's place
    const auto q = defaultPair ({ kRoleMain, kRoleMain, kRoleFill, kRoleSub });
    check (q.high == 0 && q.low == 3, "Crossover pair: Main L, Main R, Fill, Sub gives Main L / Sub", q.high * 10 + q.low, 3);

    // an inverted pair (sub above, top below) is flagged; deliberate same-kind pairs are not
    check (pairInverted (subFirst, { 0, 1 }), "Crossover pair: HIGH = Sub, LOW = Main L is flagged as inverted");
    check (! pairInverted ({ kRoleSub, kRoleSub }, { 0, 1 }), "Crossover pair: sub to sub is allowed");
    check (! pairInverted ({ kRoleMain, kRoleMain }, { 1, 0 }), "Crossover pair: top to top is allowed");

    // no sub: the next speaker below the top; only subs: sub to sub
    const auto r = defaultPair ({ kRoleFill, kRoleMain });
    check (r.high == 1 && r.low == 0, "Crossover pair: without a sub the other speaker is low", r.high * 10 + r.low, 10);
    const auto s = defaultPair ({ kRoleSub, kRoleSub });
    check (s.valid (2), "Crossover pair: two subs make a valid pair");
}

// Room acoustics from a synthetic exponentially decaying noise (a diffuse room with a known reverberation time).
static void testRoomAcoustics()
{
    using namespace la::acoustics;
    const double fs = 48000.0, rt = 1.0;
    std::mt19937 rng (7);
    std::normal_distribution<double> nd (0.0, 1.0);
    std::vector<float> ir ((size_t) (1.6 * fs), 0.0f);
    const int at = 480;   // direct sound at 10 ms
    for (size_t i = (size_t) at; i < ir.size(); ++i)
    {
        const double t = (double) (i - (size_t) at) / fs;
        ir[i] = (float) (nd (rng) * std::exp (-6.9078 * t / rt));
    }
    for (auto& v : ir) v += (float) (nd (rng) * 1e-4);   // a noise floor about 80 dB down
    ir[(size_t) at] = 4.0f;                                // direct sound
    const auto r = analyse (ir, fs);
    check (r.valid, "Acoustics: result");
    near (r.band[3].t30, rt, 0.12, "Acoustics: T30 at 1 kHz");
    near (r.band[2].t20, rt, 0.12, "Acoustics: T20 at 500 Hz");
    near (r.band[4].edt, rt, 0.2, "Acoustics: EDT at 2 kHz");
    // exponential decay with T = 1 s: C80 about 3 dB, C50 about 0.4 dB (minus a little for the direct sound added)
    check (r.band[3].c80 > 1.5 && r.band[3].c80 < 6.0, "Acoustics: C80 at 1 kHz", r.band[3].c80, 3.1);
    check (r.band[3].c50 < r.band[3].c80, "Acoustics: C50 below C80");
    check (std::isfinite (r.sti) && r.sti > 0.45 && r.sti < 0.72, "Acoustics: STI for a 1 s room", r.sti, 0.58);

    // a drier room: shorter RT, higher STI and clarity
    std::vector<float> dry (ir.size(), 0.0f);
    for (size_t i = (size_t) at; i < dry.size(); ++i)
        dry[i] = (float) (nd (rng) * std::exp (-6.9078 * (double) (i - (size_t) at) / fs / 0.4));
    dry[(size_t) at] = 4.0f;
    const auto d = analyse (dry, fs);
    near (d.band[3].rt(), 0.4, 0.06, "Acoustics: RT of a dry room");
    check (d.sti > r.sti && d.band[3].c80 > r.band[3].c80, "Acoustics: a drier room is clearer");

    // noise ratings
    const std::array<double, kNoiseBands> nc30 { 57, 48, 41, 35, 31, 29, 28, 27 };
    near (noiseCriterion (nc30), 30.0, 0.5, "Noise: the NC-30 curve rates NC 30");
    const std::array<double, kNoiseBands> quiet { 40, 30, 25, 20, 18, 15, 12, 10 };
    check (noiseCriterion (quiet) < 20.0, "Noise: a quiet room is below NC 20", noiseCriterion (quiet), 18);
    const std::array<double, kNoiseBands> flat40 { 40, 40, 40, 40, 40, 40, 40, 40 };
    near (noiseRating (flat40), 45.7, 0.3, "Noise: NR of a flat 40 dB spectrum (set by 8 kHz)");

    // the 0.18 measures on the 1 s room
    check (r.band[3].ts > 50.0 && r.band[3].ts < 85.0, "Acoustics: centre time Ts at 1 kHz (about 72 ms for 1 s)", r.band[3].ts, 72.0);
    check (d.band[3].ts < r.band[3].ts, "Acoustics: a drier room has a shorter Ts", d.band[3].ts, r.band[3].ts);
    near (r.b63.rt(), rt, 0.3, "Acoustics: RT in the 63 Hz band");
    near (r.rt3[10], rt, 0.15, "Acoustics: RT in the 1 kHz third octave");
    near (r.rt3[0], rt, 0.25, "Acoustics: RT in the 100 Hz third octave");
    near (bassRatio (r), 1.0, 0.2, "Acoustics: bass ratio of an even room");
    near (trebleRatio (r), 1.0, 0.2, "Acoustics: treble ratio of an even room");
    check (r.echoSpeech < 1.0 && r.echoMusic < 1.8, "Acoustics: no echo in a diffuse decay", r.echoSpeech, 0.0);
    check (! std::isfinite (r.flutterMs), "Acoustics: no flutter in a diffuse decay", r.flutterMs, NAN);
    near (alcons (0.6), 6.6, 0.2, "Acoustics: %ALcons for STI 0.60");
    near (schroederHz (1.0, 1000.0), 63.2, 0.2, "Acoustics: Schroeder frequency, 1 s in 1000 m3");
    near (criticalDistanceM (10.0, 1000.0, 1.0), 5.7, 0.05, "Acoustics: critical distance, Q 10, 1000 m3, 1 s");
    {
        const std::array<double, kBands> none { NAN, NAN, NAN, NAN, NAN, NAN, NAN };
        const double c70 = stiCorrected (d, 70.0, none);
        check (std::isfinite (c70) && c70 <= d.sti + 1e-9 && c70 > d.sti - 0.06, "STI: masking and threshold at 70 dBA cost little", c70, d.sti);
        const std::array<double, kBands> loud { 60, 60, 60, 60, 60, 60, 60 };
        const double cn = stiCorrected (d, 70.0, loud);
        check (cn < c70 - 0.1, "STI: noise 60 dB under 70 dBA speech lowers it", cn, c70);
        const std::array<double, kBands> quietN { 20, 15, 10, 5, 5, 5, 5 };
        check (std::abs (stiCorrected (d, 70.0, quietN) - c70) < 0.02, "STI: a quiet room changes nothing", stiCorrected (d, 70.0, quietN), c70);
    }
    {
        const auto sp = spreadOf ({ 1.0, 1.2, NAN, 0.8 });
        check (sp.n == 3 && std::abs (sp.mean - 1.0) < 1e-9 && std::abs (sp.lo - 0.8) < 1e-9 && std::abs (sp.hi - 1.2) < 1e-9 && std::abs (sp.sd - 0.2) < 1e-9,
               "Acoustics: spread across points", sp.sd, 0.2);
    }

    // a first reflection 12 ms after the direct sound, then a diffuse tail from 30 ms
    {
        std::vector<float> e (ir.size(), 0.0f);
        e[(size_t) at] = 4.0f;
        e[(size_t) at + 576] = 2.0f;   // 12 ms, -6 dB
        for (size_t i = (size_t) at + 1440; i < e.size(); ++i) e[i] = (float) (0.3 * nd (rng) * std::exp (-6.9078 * (double) (i - (size_t) at) / fs / 0.6));
        for (auto& v : e) v += (float) (nd (rng) * 1e-5);
        const auto g = analyse (e, fs);
        near (g.itdgMs, 12.0, 1.0, "Acoustics: initial time delay gap");
        check (g.etc.size() > 700, "Acoustics: energy-time curve", (double) g.etc.size(), 810);
    }
    // a strong late reflection at 150 ms (a back wall): an echo. Near the speaker: direct sound well above the reverb.
    {
        auto pulse = [&] (std::vector<float>& v, size_t i0, double amp)
        {
            for (int k = 0; k < 48; ++k) v[i0 + (size_t) k] += (float) (amp * std::sin (2.0 * 3.14159265358979 * 1000.0 * k / fs) * 0.5 * (1.0 - std::cos (2.0 * 3.14159265358979 * k / 48.0)));
        };
        std::vector<float> base (ir.size(), 0.0f);
        for (size_t i = (size_t) at + 96; i < base.size(); ++i) base[i] = (float) (0.04 * nd (rng) * std::exp (-6.9078 * (double) (i - (size_t) at) / fs / 0.5));
        pulse (base, (size_t) at, 4.0);
        auto e = base;
        pulse (e, (size_t) at + 7200, 3.0);
        const auto g = analyse (e, fs), b = analyse (base, fs);
        check (g.echoAudible(), "Acoustics: a strong reflection at 150 ms is an echo", g.echoSpeech, 1.0);
        near (g.echoMs, 150.0, 8.0, "Acoustics: when the echo arrives");
        check (std::isfinite (g.echoDb) && g.echoDb > -8.0, "Acoustics: the echo's level", g.echoDb, -2.5);
        check (! b.echoAudible() && b.echoSpeech < g.echoSpeech, "Acoustics: no reflection, no echo", b.echoSpeech, g.echoSpeech);
    }
    // flutter: repeats every 20 ms (two parallel walls about 3.4 m apart)
    {
        auto e = dry;
        for (int k = 1; k < 40; ++k) e[(size_t) at + (size_t) (k * 960)] += (float) (2.0 * std::exp (-6.9078 * k * 0.02 / 0.8));
        const auto g = analyse (e, fs);
        near (g.flutterMs, 20.0, 1.5, "Acoustics: flutter echo period");
    }
}

// Feedback ring-out: a steady narrow peak is a ring; a short one is not.
static void testFeedbackDetector()
{
    using namespace la::fb;
    std::vector<double> f;
    for (double x = 40.0; x <= 16000.0; x *= std::pow (2.0, 1.0 / 24.0)) f.push_back (x);
    std::mt19937 rng (3);
    std::normal_distribution<double> nd (0.0, 2.0);
    auto frame = [&] (double ringHz, double ringDb)
    {
        std::vector<float> db (f.size());
        for (size_t i = 0; i < f.size(); ++i)
        {
            double v = -40.0 - 3.0 * std::log2 (f[i] / 1000.0) + nd (rng);
            if (ringHz > 0.0) v = std::max (v, -40.0 + ringDb - 60.0 * std::abs (std::log2 (f[i] / ringHz)) * 24.0 / 2.0);
            db[i] = (float) v;
        }
        return db;
    };
    Detector det;
    std::vector<Ring> found;
    for (int k = 0; k < 10; ++k) for (const auto& r : det.push (f, frame (1250.0, 25.0))) found.push_back (r);
    check (found.size() == 1, "Feedback: a steady ring is found once", (double) found.size(), 1);
    if (! found.empty())
    {
        near (found[0].freq, 1250.0, 1250.0 * 0.02, "Feedback: ring frequency");
        check (found[0].cutDb <= -4.0 && found[0].cutDb >= -12.0, "Feedback: a sensible cut", found[0].cutDb, -8);
    }
    Detector det2;
    int brief = 0;
    for (int k = 0; k < 12; ++k) brief += (int) det2.push (f, frame (k < 2 ? 3000.0 : 0.0, 25.0)).size();
    check (brief == 0, "Feedback: a short peak is not a ring", brief, 0);
    Detector det3;
    int none = 0;
    for (int k = 0; k < 20; ++k) none += (int) det3.push (f, frame (0.0, 0.0)).size();
    check (none == 0, "Feedback: noise alone gives no rings", none, 0);
}

// A speaker measured close up: Butterworth high-pass `hpOrder` at `hp` Hz and low-pass `lpOrder` at `lp` Hz
// (minimum phase), a small timing error, optionally inverted, with a little phase noise.
static la::FreqTable speakerTf (double hp, int hpOrder, double lp, int lpOrder, bool inverted, double tauMs, unsigned seed = 1, double noiseDeg = 8.0)
{
    using C = std::complex<double>;
    la::FreqTable t;
    unsigned st = seed;
    auto rnd = [&st] { st = st * 1664525u + 1013904223u; return (double) (st >> 8) / (double) (1u << 24) - 0.5; };
    auto bw = [] (C s, int order, bool high)
    {
        // normalised Butterworth poles; s already scaled by the corner (high-pass: 1/s)
        C h (1.0, 0.0);
        const double pi = 3.14159265358979323846;
        for (int k = 0; k < order; ++k)
        {
            const double a = pi * (2.0 * k + order + 1) / (2.0 * order);
            const C pole (std::cos (a), std::sin (a));
            h *= (high ? C (1.0, 0.0) : -pole) / (s - pole) * (high ? s : C (1.0, 0.0)) / (high ? C (1.0, 0.0) : C (1.0, 0.0));
        }
        return h;
    };
    for (int i = 0; i < 537; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 536.0);
        const C jw (0.0, f);
        C h = bw (C (0.0, f / lp), lpOrder, false);
        // high-pass: H_hp(s) = H_lp(wc / s)
        h *= bw (C (0.0, -hp / f), hpOrder, false);
        if (inverted) h = -h;
        h *= std::exp (C (0.0, -2.0 * 3.14159265358979323846 * f * tauMs * 0.001));
        t.freq.push_back (f);
        t.db.push_back (20.0 * std::log10 (std::abs (h) + 1e-12));
        double ph = std::arg (h) * 180.0 / 3.14159265358979323846 + noiseDeg * rnd();
        while (ph > 180.0) ph -= 360.0;
        while (ph < -180.0) ph += 360.0;
        t.phase.push_back (ph);
        t.extra.push_back (0.95);
        (void) jw;
    }
    return t;
}

static double pi180() { return 3.14159265358979323846 / 180.0; }

static void testPolarity()
{
    using namespace la::polarity;
    // a top, crossover bypassed: 2nd order at 70 Hz, rolling off above 18 kHz; timed 0.03 ms off
    const auto top = speakerTf (70.0, 2, 18000.0, 2, false, 0.03);
    const auto topInv = speakerTf (70.0, 2, 18000.0, 2, true, -0.04, 7);
    auto a = absolute (top, 0.4, true, false);
    check (a.verdict == Normal, "Polarity: top wired normally", a.band.phaseDeg, 0.0);
    a = absolute (topInv, 0.4, true, true);
    check (a.verdict == Inverted, "Polarity: top inverted", a.band.phaseDeg, 180.0);
    a = absolute (topInv, 0.4, true, false);
    check (a.verdict == Unsure, "Polarity: phase and impulse disagree gives check", a.verdict, Unsure);
    a = absolute (topInv, 0.4, false, false);
    check (a.verdict == Unsure && a.phaseSaysInverted, "Polarity: the phase alone saying inverted gives check", a.band.phaseDeg, 180.0);

    // a sub, crossover bypassed: ported (4th order at 35 Hz), the driver rolling off above about 600 Hz
    const auto sub = speakerTf (35.0, 4, 600.0, 2, false, 0.1, 3);
    const auto subInv = speakerTf (35.0, 4, 600.0, 2, true, 0.1, 4);
    a = absolute (sub, 0.4, true, false);
    check (a.verdict == Normal, "Polarity: sub wired normally", a.band.phaseDeg, 0.0);
    a = absolute (subInv, 0.4, true, true);
    check (a.verdict == Inverted, "Polarity: sub inverted", a.band.phaseDeg, 180.0);

    // a sub timed 4 ms late by the timing search (its impulse is long), with the processor's low-pass left in: still clear
    const auto subLate = speakerTf (30.0, 2, 110.0, 4, false, 4.0, 13);
    const auto subLateInv = speakerTf (30.0, 2, 110.0, 4, true, 4.0, 14);
    a = absolute (subLate, 0.4, true, false);
    check (a.verdict == Normal, "Polarity: sub timed 4 ms late, normal", a.band.phaseDeg, 0.0);
    a = absolute (subLateInv, 0.4, true, true);
    check (a.verdict == Unsure && a.phaseSaysInverted, "Polarity: sub timed 4 ms late, inverted: check (one sign only)", a.band.phaseDeg, 180.0);
    a = absolute (subLate, 0.4, true, true);
    check (a.verdict == Normal, "Polarity: a sub's ringing impulse peak does not decide", a.band.phaseDeg, 0.0);

    // Main R against Main L (same model): same, then one wired the other way round
    const auto right = speakerTf (70.0, 2, 18000.0, 2, false, -0.05, 9);
    const auto rightInv = speakerTf (70.0, 2, 18000.0, 2, true, 0.02, 11);
    auto r = relative (right, top, 0.4);
    check (r.verdict == Normal, "Polarity: Main R matches Main L", r.band.phaseDeg, 0.0);
    r = relative (rightInv, top, 0.4);
    check (r.verdict == Inverted, "Polarity: Main R opposite to Main L", r.band.phaseDeg, 180.0);
    // the second sub of a pair, opposite to the first
    r = relative (subInv, sub, 0.4);
    check (r.verdict == Inverted, "Polarity: second sub opposite to the first", r.band.phaseDeg, 180.0);

    // a two-way top wired correctly: woofer (2nd order at 70 Hz, LR4 low-pass at 2 kHz) and horn (LR4 high-pass at
    // 2 kHz) summed: the box's own crossover turns the phase 360 degrees, which must not read as inverted
    {
        using C = std::complex<double>;
        auto twoWay = [] (bool inverted, unsigned seed)
        {
            la::FreqTable t;
            unsigned st = seed;
            auto rnd = [&st] { st = st * 1664525u + 1013904223u; return (double) (st >> 8) / (double) (1u << 24) - 0.5; };
            const double pi = 3.14159265358979323846;
            auto bw2 = [] (C s) { return 1.0 / (s * s + std::sqrt (2.0) * s + 1.0); };   // 2nd order Butterworth low-pass, s normalised
            for (int i = 0; i < 537; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 536.0);
                const C sw (0.0, f / 2000.0), sh (0.0, -2000.0 / f), s70 (0.0, -70.0 / f), s18 (0.0, f / 18000.0);
                const C woofer = bw2 (s70) * bw2 (sw) * bw2 (sw);
                const C horn = bw2 (sh) * bw2 (sh) * bw2 (s18);
                C h = woofer + horn;
                if (inverted) h = -h;
                h *= std::exp (C (0.0, -2.0 * pi * f * 0.05 * 0.001));
                t.freq.push_back (f);
                t.db.push_back (20.0 * std::log10 (std::abs (h) + 1e-12));
                double ph = std::arg (h) * 180.0 / pi + 6.0 * rnd();
                ph = std::remainder (ph, 360.0);
                t.phase.push_back (ph);
                t.extra.push_back (0.95);
            }
            return t;
        };
        const auto tw = twoWay (false, 21), twInv = twoWay (true, 22);
        a = absolute (tw, 0.4, true, false);
        check (a.verdict == Normal, "Polarity: two-way top with its own crossover, wired normally", a.band.phaseDeg, 0.0);
        a = absolute (tw, 0.4, false, false);
        check (a.verdict != Inverted, "Polarity: two-way top, phase alone, never inverted", a.band.phaseDeg, 0.0);
        a = absolute (twInv, 0.4, true, true);
        check (a.verdict == Inverted, "Polarity: two-way top inverted (phase and peak agree)", a.band.phaseDeg, 180.0);
        // far away: a strong reflection 4 ms later blurs the phase; still never a firm inversion of a box wired normally
        auto far = tw;
        for (size_t i = 0; i < far.freq.size(); ++i)
        {
            const double f = far.freq[i];
            const C d = std::polar (std::pow (10.0, far.db[i] / 20.0), far.phase[i] * pi180());
            const C r = d * 0.6 * std::exp (C (0.0, -2.0 * 3.14159265358979323846 * f * 0.004));
            const C sum = d + r;
            far.db[i] = 20.0 * std::log10 (std::abs (sum) + 1e-12);
            far.phase[i] = std::arg (sum) / pi180();
        }
        a = absolute (far, 0.4, true, false);
        check (a.verdict != Inverted, "Polarity: far away with a reflection, not called inverted", a.band.phaseDeg, 0.0);
    }

    // nothing but noise: no answer
    auto junk = top;
    unsigned st = 5;
    for (auto& p : junk.phase) { st = st * 1664525u + 1013904223u; p = (double) (st >> 8) / (double) (1u << 24) * 360.0 - 180.0; }
    a = absolute (junk, 0.4, true, false);
    check (a.verdict == Unsure, "Polarity: random phase gives check", a.band.spread, 1.0);
    std::printf ("  polarity: top %.0f deg, sub %.0f deg (spread %.2f)\n", absolute (top, 0.4, false, false).band.phaseDeg,
                 absolute (sub, 0.4, false, false).band.phaseDeg, absolute (sub, 0.4, false, false).band.spread);
}

// A sweep through a room with a known reverberation time: the impulse response from deconvolving the recording, and
// the RT60 worked out from it.
static void testSweepDeconvolution()
{
    const double fs = 48000.0;
    const double rt = 1.8;                         // longer than the live analysis window
    const int irLen = (int) (2.5 * fs);
    std::vector<double> room ((size_t) irLen);
    unsigned st = 3;
    for (int i = 0; i < irLen; ++i)
    {
        st = st * 1664525u + 1013904223u;
        const double r = ((double) (st >> 8) / (double) (1u << 24) - 0.5);
        room[(size_t) i] = r * std::exp (-6.9078 * i / (rt * fs));   // -60 dB at rt
    }
    room[0] = 1.0;
    // the reference: two repeats of a 4 s log sweep and 0.5 s of silence
    const int sweepLen = (int) (4.0 * fs), period = sweepLen + (int) (0.5 * fs), n = 2 * period;
    std::vector<float> x ((size_t) n, 0.0f), y ((size_t) n, 0.0f);
    const double f0 = 20.0, f1 = 20000.0, k = std::log (f1 / f0);
    for (int i = 0; i < n; ++i)
    {
        const int t = i % period;
        if (t < sweepLen) x[(size_t) i] = (float) (0.5 * std::sin (2.0 * 3.14159265358979 * f0 * 4.0 / k * (std::exp ((double) t / fs / 4.0 * k) - 1.0)));
    }
    // the mic: the sweep through the room, 3 ms later, with a little noise
    const int lag = (int) (0.003 * fs);
    std::vector<double> yd ((size_t) n, 0.0);
    {
        int len = 1;
        while (len < n + irLen) len <<= 1;
        la::Fft fft (len);
        std::vector<la::cplx> a ((size_t) len), b ((size_t) len);
        for (int i = 0; i < n; ++i) a[(size_t) i] = x[(size_t) i];
        for (int i = 0; i < irLen; ++i) b[(size_t) i] = room[(size_t) i];
        fft.forward (a.data());
        fft.forward (b.data());
        for (int i = 0; i < len; ++i) a[(size_t) i] *= b[(size_t) i];
        fft.inverse (a.data());
        for (int i = 0; i + lag < n; ++i) yd[(size_t) (i + lag)] = a[(size_t) i].real();
    }
    for (int i = 0; i < n; ++i) { st = st * 1664525u + 1013904223u; y[(size_t) i] = (float) (yd[(size_t) i] + 1e-5 * ((double) (st >> 8) / (double) (1u << 24) - 0.5)); }
    const auto ir = la::deconv::impulse (x.data(), y.data(), n, fs, 3.0);
    check (ir.size() >= (size_t) (2.9 * fs), "Sweep: a 3 s impulse response", (double) ir.size() / fs, 3.0);
    const auto r = la::acoustics::analyse (ir, fs);
    near (r.rtMid(), rt, 0.15, "Sweep: RT60 of a 1.8 s room from the sweep");
    std::printf ("  sweep: RT60 %.2f s (room %.2f s)\n", r.rtMid(), rt);
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
    testEqFit();
    testCrossoverPair();
    testRoomAcoustics();
    testFeedbackDetector();
    testPolarity();
    testSweepDeconvolution();

    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
