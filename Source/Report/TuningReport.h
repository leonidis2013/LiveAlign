// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The tuning report: a PDF of the venue's system setup for the open session. Cover and summary, the venue plan,
// the speakers and the settings for the processor, the crossover and time alignment, the measurement mics with
// their serial numbers and calibration, this session's checks, and the measured responses and traces.
#include "PdfWriter.h"
#include "../PluginProcessor.h"
#include "../UI/SpeakersPage.h"

namespace la::report
{
namespace detail
{
const juce::Colour ink (0xff1c232c), dim (0xff5d6876), faint (0xff97a1ae), rule (0xffd5dbe3), band (0xfff2f4f7),
                   accent (0xff0f9488), good (0xff23a056), warn (0xffcf9a10);

// Page layout with a running cursor, header and footer.
struct Layout
{
    pdf::Document& doc;
    juce::String footerLeft;
    float y = 0.0f;
    int page = 0;
    static constexpr float kMargin = 50.0f, kWidth = pdf::Document::kPageW - 2.0f * kMargin, kBottom = pdf::Document::kPageH - 60.0f;

    void newPage()
    {
        doc.newPage();
        ++page;
        doc.line (kMargin, 760.0f, kMargin + kWidth, 760.0f, 0.5f, rule);
        doc.text (kMargin, 774.0f, footerLeft, 8.0f, pdf::Document::Regular, dim);
        doc.textRight (kMargin + kWidth, 774.0f, "Page " + juce::String (page), 8.0f, pdf::Document::Regular, dim);
        y = 56.0f;
    }

    void need (float h) { if (y + h > kBottom) newPage(); }

    void heading (const juce::String& t)
    {
        need (60.0f);
        y += 10.0f;
        doc.rect (kMargin, y - 11.0f, 3.0f, 14.0f, accent);
        doc.text (kMargin + 10.0f, y, t, 14.0f, pdf::Document::Bold, ink);
        y += 18.0f;
    }

    void sub (const juce::String& t)
    {
        need (40.0f);
        y += 4.0f;
        doc.text (kMargin, y, t.toUpperCase(), 8.5f, pdf::Document::Bold, dim);
        y += 12.0f;
    }

    void para (const juce::String& t, float size = 9.5f, juce::Colour c = ink)
    {
        const auto lines = pdf::Document::wrap (t, kWidth, size);
        need ((float) lines.size() * size * 1.35f);
        y += doc.paragraph (kMargin, y, kWidth, t, size, pdf::Document::Regular, c) + 4.0f;
    }

    // A table: relative column widths, a header row and rows of text. Long cells wrap.
    void table (const std::vector<float>& widths, const juce::StringArray& head, const std::vector<juce::StringArray>& rows, float size = 8.5f)
    {
        float total = 0.0f;
        for (float w : widths) total += w;
        std::vector<float> xs, ws;
        float x = kMargin;
        for (float w : widths) { xs.push_back (x); ws.push_back (w / total * kWidth); x += w / total * kWidth; }
        auto drawHead = [&]
        {
            doc.rect (kMargin, y - 9.0f, kWidth, 14.0f, band);
            for (size_t c = 0; c < ws.size() && (int) c < head.size(); ++c)
                doc.text (xs[c] + 4.0f, y + 1.0f, head[(int) c].toUpperCase(), size - 1.0f, pdf::Document::Bold, dim);
            y += 16.0f;
        };
        need (40.0f);
        drawHead();
        for (const auto& r : rows)
        {
            size_t lines = 1;
            std::vector<juce::StringArray> cells;
            for (size_t c = 0; c < ws.size(); ++c)
            {
                cells.push_back (pdf::Document::wrap (c < (size_t) r.size() ? r[(int) c] : juce::String(), ws[c] - 8.0f, size));
                lines = std::max (lines, (size_t) cells.back().size());
            }
            const float h = (float) lines * size * 1.3f + 4.0f;
            if (y + h > kBottom) { newPage(); drawHead(); }
            for (size_t c = 0; c < ws.size(); ++c)
                for (int l = 0; l < cells[c].size(); ++l)
                    doc.text (xs[c] + 4.0f, y + (float) l * size * 1.3f, cells[c][l], size, pdf::Document::Regular, ink);
            y += h;
            doc.line (kMargin, y - size - 1.0f, kMargin + kWidth, y - size - 1.0f, 0.3f, rule);
        }
        y += 6.0f;
    }

    void picture (const juce::Image& img, float maxH)
    {
        if (! img.isValid()) return;
        float w = kWidth, h = w * (float) img.getHeight() / (float) img.getWidth();
        if (h > maxH) { h = maxH; w = h * (float) img.getWidth() / (float) img.getHeight(); }
        need (h + 10.0f);
        doc.image (img, kMargin + (kWidth - w) * 0.5f, y, w, h);
        y += h + 10.0f;
    }

    void keyValues (const std::vector<std::pair<juce::String, juce::String>>& kv)
    {
        for (const auto& p : kv)
        {
            need (14.0f);
            doc.text (kMargin, y, p.first, 9.0f, pdf::Document::Bold, dim);
            y += doc.paragraph (kMargin + 130.0f, y, kWidth - 130.0f, p.second, 9.0f, pdf::Document::Regular, ink) + 2.0f;
        }
        y += 4.0f;
    }
};

// Traces of one kind drawn on a light chart (log frequency, dB), with the target curve when one is chosen.
inline juce::Image traceChart (const std::vector<const Trace*>& traces, bool transfer, int target, int w = 1600, int h = 760)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colours::white);
    const auto plot = juce::Rectangle<float> (80.0f, 30.0f, (float) w - 110.0f, (float) h - 110.0f);
    ui::FreqAxis ax { 20.0, 20000.0, plot };

    float lo = 1e9f, hi = -1e9f;
    for (const auto* t : traces)
        for (size_t i = 0; i < t->data.freq.size(); ++i)
            if (t->data.freq[i] >= 20.0 && t->data.freq[i] <= 20000.0 && std::isfinite (t->data.db[i])) { lo = juce::jmin (lo, (float) t->data.db[i]); hi = juce::jmax (hi, (float) t->data.db[i]); }
    if (lo > hi) { lo = -20.0f; hi = 10.0f; }
    const float top = std::ceil ((hi + 3.0f) / 6.0f) * 6.0f;
    const float range = juce::jlimit (24.0f, 72.0f, std::ceil ((top - lo + 3.0f) / 6.0f) * 6.0f);
    auto yAt = [&] (float db) { return plot.getY() + (top - db) / range * plot.getHeight(); };

    g.setColour (juce::Colour (0xffe3e8ee));
    for (int decade = 10; decade <= 10000; decade *= 10)
        for (int m = 2; m <= 9; ++m) if (decade * m >= 20 && decade * m <= 20000) g.drawVerticalLine ((int) ax.x (decade * m), plot.getY(), plot.getBottom());
    g.setFont (juce::FontOptions (22.0f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        g.setColour (juce::Colour (0xffc5ccd6));
        g.drawVerticalLine ((int) ax.x (f), plot.getY(), plot.getBottom());
        g.setColour (detail::dim);
        g.drawText (ui::freqLabel (f), juce::Rectangle<float> (ax.x (f) - 40.0f, plot.getBottom() + 8.0f, 80.0f, 26.0f), juce::Justification::centred);
    }
    for (float db = top; db >= top - range - 0.1f; db -= 6.0f)
    {
        g.setColour (juce::Colour (0xffe3e8ee));
        g.drawHorizontalLine ((int) yAt (db), plot.getX(), plot.getRight());
        g.setColour (detail::dim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (10.0f, yAt (db) - 12.0f, 62.0f, 24.0f), juce::Justification::centredRight);
    }
    g.setColour (detail::dim);
    g.drawText (transfer ? "dB (transfer function)" : "dB (RTA)", juce::Rectangle<float> (plot.getX(), (float) h - 40.0f, 400.0f, 26.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffc5ccd6));
    g.drawRect (plot, 1.5f);

    if (transfer && target > 0 && ! traces.empty())
    {
        // the target lined up with the first trace's 500 Hz - 2 kHz average, as on screen
        float level = 0.0f;
        std::vector<float> v;
        for (double d : traces.front()->data.db) v.push_back ((float) d);
        ui::bandAverage (traces.front()->data.freq, v, 500.0, 2000.0, level);
        juce::Path line;
        for (int i = 0; i <= 200; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 200.0);
            const float y = yAt (level + (float) targets::db (target, f));
            if (i == 0) line.startNewSubPath (ax.x (f), y); else line.lineTo (ax.x (f), y);
        }
        juce::Path dashed;
        const float dashes[] = { 16.0f, 10.0f };
        juce::PathStrokeType (3.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (detail::good);
        g.fillPath (dashed);
    }

    int k = 0;
    for (const auto* t : traces)
    {
        juce::Path p;
        bool pen = false;
        for (size_t i = 0; i < t->data.freq.size(); ++i)
        {
            const double f = t->data.freq[i];
            if (f < 20.0 || f > 20000.0 || ! std::isfinite (t->data.db[i])) { pen = false; continue; }
            const float x = ax.x (f), y = juce::jlimit (plot.getY(), plot.getBottom(), yAt ((float) t->data.db[i]));
            if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
        }
        static const juce::uint32 cols[] = { 0xff0f9488, 0xff2563d6, 0xffc98a0a, 0xff7553e0, 0xffe0661a, 0xffd2478f, 0xff23a056, 0xff434d5a };
        g.setColour (juce::Colour (cols[k % 8]));
        g.strokePath (p, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        ++k;
    }
    return img;
}
} // namespace detail

inline juce::String filterText (const xo::Filter& f, const char* kind)
{
    if (! f.active()) return "-";
    return juce::String (kind) + " " + xo::typeShort (f.type) + juce::String (f.slope) + " @ " + juce::String (juce::roundToInt (f.freq)) + " Hz";
}

// Writes the report. `plan` is the venue plan picture (white page). Returns an error message, or empty.
inline juce::String writeTuningReport (LiveAlignProcessor& p, const juce::File& file, const juce::Image& plan, const juce::String& author)
{
    using namespace detail;
    pdf::Document doc;
    const auto& sessions = p.getSessions();
    const int cur = p.currentSession();
    const auto sessionName = cur < (int) sessions.size() ? sessions[(size_t) cur].name : juce::String ("Session");
    Layout L { doc, p.venueName + "  -  " + sessionName + "  -  LiveAlign " + JucePlugin_VersionString };
    const auto& sys = p.system;
    const auto& st = p.settings;
    const int nMics = st.micCount.load();

    // ---------- cover ----------
    L.newPage();
    doc.rect (0.0f, 0.0f, pdf::Document::kPageW, 170.0f, accent);
    doc.text (L.kMargin, 70.0f, "SYSTEM TUNING REPORT", 11.0f, pdf::Document::Bold, juce::Colours::white.withAlpha (0.85f));
    doc.text (L.kMargin, 104.0f, p.venueName, 26.0f, pdf::Document::Bold, juce::Colours::white);
    doc.text (L.kMargin, 132.0f, sessionName + "   -   " + LiveAlignProcessor::today(), 12.0f, pdf::Document::Regular, juce::Colours::white);
    L.y = 210.0f;
    std::vector<std::pair<juce::String, juce::String>> kv;
    if (author.isNotEmpty()) kv.push_back ({ "Prepared by", author });
    kv.push_back ({ "Venue file", p.projectFile != juce::File() ? p.projectFile.getFileName() : juce::String ("not saved") });
    kv.push_back ({ "Speakers", p.speakers.configured ? "Set up " + p.speakers.configuredOn + (p.speakers.changed ? " (marked as changed since)" : "")
                                                      : juce::String ("Not marked as set up") });
    {
        int calibrated = 0;
        for (int m = 0; m < nMics; ++m) calibrated += st.micSplCalibrated[m].load() ? 1 : 0;
        kv.push_back ({ "Measurement mics", juce::String (nMics) + (nMics == 1 ? " mic, " : " mics, ") + juce::String (calibrated) + " calibrated for sound level" });
    }
    kv.push_back ({ "Analysis", juce::String (p.getSampleRate() > 0.0 ? p.getSampleRate() / 1000.0 : 48.0, 1) + " kHz, FFT "
                                    + juce::String (st.fftSize.load() / 1024) + "k, " + juce::String (st.averages.load()) + " averages, 1/"
                                    + juce::String (st.smoothing.load()) + " octave smoothing, minimum coherence "
                                    + juce::String (juce::roundToInt (st.coherenceBlank.load() * 100.0f)) + " %" });
    if (st.targetCurve.load() > 0) kv.push_back ({ "Target curve", targets::list()[(size_t) st.targetCurve.load()].name });
    L.keyValues (kv);
    if (p.venueNotes.isNotEmpty()) { L.sub ("Venue notes"); L.para (p.venueNotes); }

    L.sub ("Contents");
    L.para ("1  Venue plan        2  Speakers and processor settings        3  Measurement mics and checks        4  Measurements", 9.5f, dim);

    // ---------- plan ----------
    if (plan.isValid())
    {
        L.newPage();
        L.heading ("1  Venue plan");
        L.para ("Where the speakers and mics were for this session (a sketch, not to scale).", 9.0f, dim);
        L.picture (plan, 520.0f);
    }

    // ---------- speakers ----------
    L.newPage();
    L.heading ("2  Speakers and processor settings");
    {
        std::vector<juce::StringArray> rows;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto& s = sys.sources[(size_t) i];
            rows.push_back ({ s.name, s.model.isNotEmpty() ? s.model : juce::String ("-"), speakerRoleNames()[s.role], speakerChannelNames()[s.channel],
                              s.output == 0 ? juce::String ("all") : juce::String (s.output), juce::String (s.count),
                              s.hasResponse() ? (s.imported ? "imported" : "measured") : sys.master (i) != i ? "as " + sys.sources[(size_t) sys.master (i)].name : juce::String ("not measured") });
        }
        L.sub ("The system");
        L.table ({ 2.0f, 2.4f, 1.3f, 1.4f, 0.7f, 0.7f, 1.6f }, { "Speaker", "Model", "Role", "Channel", "Out", "Qty", "Response" }, rows);
    }
    {
        std::vector<juce::StringArray> rows;
        for (const auto& s : sys.sources)
            rows.push_back ({ s.name, filterText (s.proc.hp, "HPF"), filterText (s.proc.lp, "LPF"), juce::String (s.proc.delayMs, 2) + " ms",
                              s.proc.invert ? "inverted" : "normal", (s.proc.gainDb >= 0.0 ? "+" : "") + juce::String (s.proc.gainDb, 1) + " dB" });
        L.sub ("Settings for the speaker processor");
        L.table ({ 2.0f, 2.2f, 2.2f, 1.2f, 1.2f, 1.0f }, { "Speaker", "High-pass", "Low-pass", "Delay", "Polarity", "Gain" }, rows);
        L.para ("BW Butterworth, LR Linkwitz-Riley; slopes in dB/octave. Delay, polarity and gain are applied on each speaker's output.", 8.0f, dim);
    }
    if (sys.pairValid() && sys.sources[(size_t) sys.xoHigh].proc.hp.active())
    {
        const auto& f = sys.sources[(size_t) sys.xoHigh].proc.hp;
        L.sub ("Crossover");
        juce::String t = sys.sources[(size_t) sys.xoHigh].name + " / " + sys.sources[(size_t) sys.xoLow].name + ": " + xo::typeName (f.type) + " "
                         + juce::String (f.slope) + " dB/oct at " + juce::String (juce::roundToInt (f.freq)) + " Hz.";
        const auto& r = sys.lastResult;
        if (r.valid)
            t << " Found by LiveAlign: the two meet " << juce::roundToInt (r.efficiency * 100.0) << " % in phase through the crossover region, "
              << juce::roundToInt (r.reliability * 100.0) << " % of it measured reliably. Overlap " << juce::roundToInt (r.highUsableFrom) << " - "
              << juce::roundToInt (r.lowUsableTo) << " Hz.";
        L.para (t);
    }
    {
        std::vector<juce::StringArray> rows;
        for (const auto& a : sys.alignments)
        {
            if (a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) continue;
            const double need = a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs);
            rows.push_back ({ sys.sources[(size_t) a.align].name + " to " + sys.sources[(size_t) a.ref].name, a.spot,
                              a.measured() ? juce::String (a.alignArrivalMs - a.refArrivalMs, 2) + " ms" : juce::String ("-"),
                              a.measured() ? juce::String (need, 2) + " ms" + (a.precedenceMs > 0.0 ? " (" + juce::String (a.precedenceMs, 1) + " extra)" : juce::String()) : juce::String ("not measured"),
                              a.measured() && a.refLevelDb > -150.0f ? juce::String (a.alignLevelDb - a.refLevelDb, 1) + " dB" : juce::String ("-") });
        }
        if (! rows.empty())
        {
            L.sub ("Time alignment");
            L.table ({ 2.6f, 1.6f, 1.3f, 2.0f, 1.1f }, { "Speakers", "Where", "Arrival diff.", "Delay", "Level" }, rows);
        }
    }
    {
        // responses with the processing, on a light background
        const auto* saved = ui::currentPalette();
        ui::currentPalette() = &ui::lightPalette();
        ui::SpeakerGraph graph (sys);
        graph.setBounds (0, 0, 820, 450);
        graph.setWithProcessing (true);
        if (sys.pairValid() && sys.sources[(size_t) sys.xoHigh].proc.hp.active()) graph.setXoMarker (sys.sources[(size_t) sys.xoHigh].proc.hp.freq);
        graph.rebuild();
        const auto img = graph.createComponentSnapshot (graph.getLocalBounds(), true, 2.0f);
        ui::currentPalette() = saved;
        bool any = false;
        for (const auto& s : sys.sources) any = any || s.hasResponse();
        if (any)
        {
            L.sub ("Measured responses with the processing, and their predicted sum");
            L.picture (img, 330.0f);
        }
    }

    // ---------- mics ----------
    L.newPage();
    L.heading ("3  Measurement mics and checks");
    {
        std::vector<juce::StringArray> rows;
        for (int m = 0; m < nMics; ++m)
        {
            const auto ms = p.micModelSerial (m);
            const auto& c = p.calibrations (m);
            juce::String cal = c.any() ? c.names() : p.mics.noCalFile[(size_t) m] ? juce::String ("none (flat)") : juce::String ("not loaded");
            juce::String spl = p.mics.noCalibrator ? juce::String ("dBFS (no calibrator)")
                             : st.micSplCalibrated[m].load() ? juce::String (st.micSplOffset[m].load(), 2) + " dB" + (p.checks.splVerified[(size_t) m] ? ", checked this session" : ", not re-checked")
                                                             : juce::String ("not calibrated");
            rows.push_back ({ juce::String (m + 1) + "  " + p.mics.nameOf (m), ms.isNotEmpty() ? ms : juce::String ("-"),
                              p.mics.position[(size_t) m].isNotEmpty() ? p.mics.position[(size_t) m] : juce::String ("-"),
                              "In " + juce::String (st.micInput[m].load() + 1), cal,
                              p.mics.aim[(size_t) m] == 0 ? juce::String ("at speaker (0 deg)") : juce::String ("pointing up (90 deg)"), spl });
        }
        L.sub ("Mics");
        L.table ({ 1.6f, 1.7f, 1.1f, 0.6f, 2.4f, 1.3f, 1.6f }, { "Mic", "Model / serial", "Position", "Input", "Calibration files", "Aim", "Sound level" }, rows, 8.0f);
    }
    {
        const auto& c = p.checks;
        std::vector<std::pair<juce::String, juce::String>> checks;
        checks.push_back ({ "Reference", st.refSource.load() == 0 ? juce::String ("off") : st.refSource.load() == 1 ? juce::String ("generator (internal copy)")
                                                                                         : "Input " + juce::String (st.refSource.load() - 1) + " (loopback)" });
        checks.push_back ({ "Loopback check", c.loopbackSkipped ? juce::String ("skipped")
                                              : c.loopbackDone ? juce::String (c.loopbackLatencyMs, 2) + " ms round trip, polarity " + (c.loopbackInverted ? "INVERTED" : "normal")
                                                                     + ", " + juce::String (c.loopbackLevelDbfs, 1) + " dBFS"
                                                               : juce::String ("not done") });
        if (c.noiseDone)
        {
            juce::StringArray n;
            for (int m = 0; m < juce::jmin (nMics, c.noiseMics); ++m)
                n.add (p.mics.nameOf (m) + " " + juce::String (c.noiseA[(size_t) m], 1) + (c.noiseIsSpl[(size_t) m] ? " dBA" : " dBFS(A)"));
            checks.push_back ({ "Room noise floor", n.joinIntoString (",  ") });
        }
        else checks.push_back ({ "Room noise floor", "not measured" });
        L.sub ("This session's checks");
        L.keyValues (checks);
    }

    // ---------- measurements ----------
    std::vector<const Trace*> tf, rta;
    for (const auto& t : p.traces.all())
    {
        if (t.fromSession.isNotEmpty() || t.data.freq.size() < 4) continue;
        (t.kind == Trace::Transfer ? tf : rta).push_back (&t);
    }
    L.newPage();
    L.heading ("4  Measurements");
    if (tf.empty() && rta.empty()) L.para ("No traces were captured in this session. Capture them on Monitoring (Capture, key C).", 9.5f, dim);
    auto listTraces = [&] (const std::vector<const Trace*>& v)
    {
        static const juce::uint32 cols[] = { 0xff0f9488, 0xff2563d6, 0xffc98a0a, 0xff7553e0, 0xffe0661a, 0xffd2478f, 0xff23a056, 0xff434d5a };
        for (size_t i = 0; i < v.size(); ++i)
        {
            L.need (13.0f);
            doc.rect (L.kMargin, L.y - 7.0f, 14.0f, 4.0f, juce::Colour (cols[i % 8]));
            doc.text (L.kMargin + 22.0f, L.y, v[i]->name + (v[i]->created.isNotEmpty() ? "   (" + v[i]->created + ")" : juce::String()), 8.5f, pdf::Document::Regular, ink);
            L.y += 12.0f;
        }
        L.y += 6.0f;
    };
    if (! tf.empty())
    {
        L.sub ("Transfer functions" + juce::String (st.targetCurve.load() > 0 ? juce::String (" with the target (dashed)") : juce::String()));
        L.picture (traceChart (tf, true, st.targetCurve.load()), 300.0f);
        listTraces (tf);
    }
    if (! rta.empty())
    {
        L.sub ("RTA");
        L.picture (traceChart (rta, false, 0), 300.0f);
        listTraces (rta);
    }

    L.need (50.0f);
    L.y += 10.0f;
    L.para ("Prepared with LiveAlign " + juce::String (JucePlugin_VersionString) + " on " + LiveAlignProcessor::today()
            + (author.isNotEmpty() ? " by " + author : juce::String()) + ".", 8.5f, dim);

    if (! doc.write (file, "System tuning report - " + p.venueName, author)) return "Could not write " + file.getFullPathName();
    return {};
}
} // namespace la::report
