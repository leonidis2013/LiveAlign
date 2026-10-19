// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// What LiveAlign's PDF reports share: the page layout (chapters with a bold numbered heading, portrait or landscape
// pages, a footer with "page x of y", tables, pictures), the contents list on the cover, and charts drawn for print.
#include <functional>
#include "PdfWriter.h"
#include "../PluginProcessor.h"
#include "../UI/Graph.h"

namespace la::report
{
namespace detail
{
// One accent family (teal) for headings, rules, tile bars, table headers and graph highlights.
const juce::Colour ink (0xff1c232c), dim (0xff5d6876), faint (0xff97a1ae), rule (0xffd5dbe3), band (0xfff2f4f7),
                   accent (0xff0f9488), accentDark (0xff0b6e65), accentTint (0xffe3f3f1),
                   good (0xff23a056), warn (0xffcf9a10), bad (0xffd63232);

// Colours for traces and speakers on the report's charts, in order (the first is the accent).
inline juce::Colour seriesColour (int i)
{
    static const juce::uint32 cols[] = { 0xff0f9488, 0xff2563d6, 0xffc98a0a, 0xff7553e0, 0xffe0661a, 0xffd2478f, 0xff23a056, 0xff434d5a };
    return juce::Colour (cols[((i % 8) + 8) % 8]);
}

// A frequency as its standard nominal (1/3 octave) label: "31.5", "1.25k", "3.15k". Others are rounded.
inline juce::String freqText (double f)
{
    static const double nom[] = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                  2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
    static const char* txt[] = { "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500", "630", "800",
                                 "1k", "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k", "20k" };
    if (! std::isfinite (f) || f <= 0.0) return "-";
    for (size_t i = 0; i < sizeof (nom) / sizeof (nom[0]); ++i)
        if (std::abs (f / nom[i] - 1.0) < 0.03) return txt[i];
    if (f >= 1000.0) return juce::String (f / 1000.0, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k";
    return juce::String (juce::roundToInt (f));
}
// The same with its unit: "80 Hz", "1.25 kHz".
inline juce::String hzText (double f)
{
    const auto t = freqText (f);
    return t.endsWithChar ('k') ? t.dropLastCharacters (1) + " kHz" : t + " Hz";
}

// The plan picture without its empty margins (so it fills more of the page and its text prints larger), on white.
inline juce::Image cropToContent (const juce::Image& src)
{
    if (! src.isValid()) return src;
    const juce::Image img = src.convertedToFormat (juce::Image::ARGB);
    const int w = img.getWidth(), h = img.getHeight();
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    auto blank = [&bd] (int x, int y)
    {
        const auto c = bd.getPixelColour (x, y);
        return c.getAlpha() < 16 || (c.getRed() > 245 && c.getGreen() > 245 && c.getBlue() > 245);
    };
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y += 2)
        for (int x = 0; x < w; x += 2)
            if (! blank (x, y)) { x0 = std::min (x0, x); x1 = std::max (x1, x); y0 = std::min (y0, y); y1 = std::max (y1, y); }
    juce::Rectangle<int> area (0, 0, w, h);
    if (x1 > x0 && y1 > y0)
    {
        const int pad = juce::jmax (8, w / 80);
        area = juce::Rectangle<int>::leftTopRightBottom (x0 - pad, y0 - pad, x1 + pad, y1 + pad).getIntersection (area);
    }
    juce::Image out (juce::Image::RGB, area.getWidth(), area.getHeight(), true);
    juce::Graphics g (out);
    g.fillAll (juce::Colours::white);
    g.drawImageAt (img, -area.getX(), -area.getY());
    return out;
}

struct Layout
{
    Layout (pdf::Document& d, juce::String footer) : doc (d), footerLeft (std::move (footer)) {}
    pdf::Document& doc;
    juce::String footerLeft;
    float y = 0.0f;
    struct Chapter { juce::String title; int page; };
    std::vector<Chapter> chapters;
    // Drawn once, under the heading of the first portrait chapter (what the report is about: kept off the contents page).
    std::function<void()> intro;
    static constexpr float kMargin = 50.0f;

    float width() const { return doc.pageW() - 2.0f * kMargin; }
    float bottom() const { return doc.pageH() - 60.0f; }

    void newPage (bool landscape = false)
    {
        doc.newPage (landscape);
        y = 56.0f;
    }

    // A chapter starts a page with its number in a bold block and its title.
    void chapter (const juce::String& title, bool landscape = false)
    {
        newPage (landscape);
        chapters.push_back ({ title, doc.pageCount() });
        const auto num = juce::String ((int) chapters.size()).paddedLeft ('0', 2);
        doc.rect (kMargin, y - 16.0f, 44.0f, 36.0f, accent);
        doc.text (kMargin + 22.0f - pdf::Document::textWidth (num, 20.0f, pdf::Document::Bold) * 0.5f, y + 9.0f, num, 20.0f, pdf::Document::Bold, juce::Colours::white);
        doc.text (kMargin + 58.0f, y + 9.0f, title, 20.0f, pdf::Document::Bold, ink);
        y += 30.0f;
        doc.line (kMargin, y, kMargin + width(), y, 1.2f, accent);
        y += 22.0f;
        if (intro && ! landscape)
        {
            const auto f = std::move (intro);
            intro = nullptr;
            f();
        }
    }

    void need (float h) { if (y + h > bottom()) newPage (doc.pageW() > doc.pageH()); }

    void sub (const juce::String& t)
    {
        need (40.0f);
        y += 6.0f;
        doc.rect (kMargin, y - 9.0f, 3.0f, 11.0f, accent);
        doc.text (kMargin + 8.0f, y, t.toUpperCase(), 9.0f, pdf::Document::Bold, ink);
        y += 14.0f;
    }

    void para (const juce::String& t, float size = 9.5f, juce::Colour c = ink)
    {
        const auto lines = pdf::Document::wrap (t, width(), size);
        need ((float) lines.size() * size * 1.35f);
        y += doc.paragraph (kMargin, y, width(), t, size, pdf::Document::Regular, c) + 4.0f;
    }

    // A table: relative column widths, a header row and rows of text. Long cells wrap.
    void table (const std::vector<float>& widths, const juce::StringArray& head, const std::vector<juce::StringArray>& rows, float size = 8.5f)
    {
        float total = 0.0f;
        for (float w : widths) total += w;
        std::vector<float> xs, ws;
        float x = kMargin;
        for (float w : widths) { xs.push_back (x); ws.push_back (w / total * width()); x += w / total * width(); }
        // the header wraps within its column, so a narrow column's title never runs into the next one
        auto drawHead = [&]
        {
            const float hs = size - 1.0f, lh = hs * 1.25f;
            std::vector<juce::StringArray> hc;
            size_t hl = 1;
            for (size_t c = 0; c < ws.size(); ++c)
            {
                hc.push_back (pdf::Document::wrap ((int) c < head.size() ? head[(int) c].toUpperCase() : juce::String(), ws[c] - 6.0f, hs, pdf::Document::Bold));
                hl = std::max (hl, (size_t) hc.back().size());
            }
            const float extra = (float) (hl - 1) * lh;
            doc.rect (kMargin, y - 9.0f, width(), 14.0f + extra, accentTint);
            for (size_t c = 0; c < ws.size(); ++c)
                for (int l = 0; l < hc[c].size(); ++l)
                    doc.text (xs[c] + 4.0f, y + 1.0f + (float) l * lh, hc[c][l], hs, pdf::Document::Bold, accentDark);
            y += 16.0f + extra;
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
            if (y + h > bottom()) { newPage (doc.pageW() > doc.pageH()); drawHead(); }
            for (size_t c = 0; c < ws.size(); ++c)
                for (int l = 0; l < cells[c].size(); ++l)
                    doc.text (xs[c] + 4.0f, y + (float) l * size * 1.3f, cells[c][l], size, pdf::Document::Regular, ink);
            y += h;
            doc.line (kMargin, y - size - 1.0f, kMargin + width(), y - size - 1.0f, 0.3f, rule);
        }
        y += 6.0f;
    }

    // A picture as wide as the text (or `wider` points more each side), no taller than maxH.
    void picture (const juce::Image& img, float maxH, float wider = 0.0f)
    {
        if (! img.isValid()) return;
        const float full = width() + 2.0f * wider;
        float w = full, h = w * (float) img.getHeight() / (float) img.getWidth();
        if (h > maxH) { h = maxH; w = h * (float) img.getWidth() / (float) img.getHeight(); }
        need (h + 10.0f);
        doc.image (img, kMargin - wider + (full - w) * 0.5f, y, w, h);
        y += h + 10.0f;
    }

    // The height a picture takes at the text width (no taller than maxH).
    float pictureHeight (const juce::Image& img, float maxH) const
    {
        if (! img.isValid() || img.getWidth() <= 0) return 0.0f;
        return std::min (maxH, width() * (float) img.getHeight() / (float) img.getWidth());
    }

    // A sub-heading with its picture, kept together on one page.
    void figure (const juce::String& title, const juce::Image& img, float maxH)
    {
        if (! img.isValid()) return;
        need (pictureHeight (img, maxH) + 32.0f);
        sub (title);
        picture (img, maxH);
    }

    void keyValues (const std::vector<std::pair<juce::String, juce::String>>& kv)
    {
        for (const auto& p : kv)
        {
            need (14.0f);
            doc.text (kMargin, y, p.first, 9.0f, pdf::Document::Bold, dim);
            y += doc.paragraph (kMargin + 140.0f, y, width() - 140.0f, p.second, 9.0f, pdf::Document::Regular, ink) + 2.0f;
        }
        y += 4.0f;
    }

    // Big numbers in boxes across the page: (label, value, unit).
    void tiles (const std::vector<std::array<juce::String, 3>>& t)
    {
        if (t.empty()) return;
        const float gap = 8.0f, w = (width() - gap * (float) (t.size() - 1)) / (float) t.size(), h = 48.0f;
        need (h + 10.0f);
        for (size_t i = 0; i < t.size(); ++i)
        {
            const float x = kMargin + (float) i * (w + gap);
            doc.rect (x, y - 10.0f, w, h, band, rule);
            doc.rect (x, y - 10.0f, w, 3.0f, accent);
            doc.text (x + 8.0f, y + 2.0f, t[i][0].toUpperCase(), 7.0f, pdf::Document::Bold, dim);
            doc.text (x + 8.0f, y + 26.0f, t[i][1], 17.0f, pdf::Document::Bold, ink);
            doc.text (x + 10.0f + pdf::Document::textWidth (t[i][1], 17.0f, pdf::Document::Bold), y + 26.0f, t[i][2], 8.5f, pdf::Document::Regular, dim);
        }
        y += h + 6.0f;
    }

    // The contents list (drawn on the cover once every chapter's page is known), at `atY` on page `page`.
    void contents (int page, float atY)
    {
        doc.selectPage (page);
        float yy = atY;
        doc.text (kMargin, yy, "CONTENTS", 9.0f, pdf::Document::Bold, accentDark);
        yy += 18.0f;
        for (size_t i = 0; i < chapters.size(); ++i)
        {
            const auto num = juce::String ((int) i + 1).paddedLeft ('0', 2);
            doc.rect (kMargin, yy - 13.0f, 26.0f, 20.0f, accent);
            doc.text (kMargin + 13.0f - pdf::Document::textWidth (num, 11.0f, pdf::Document::Bold) * 0.5f, yy + 1.5f, num, 11.0f, pdf::Document::Bold, juce::Colours::white);
            doc.text (kMargin + 38.0f, yy + 1.5f, chapters[i].title, 13.0f, pdf::Document::Bold, ink);
            const auto pg = juce::String (chapters[i].page);
            const float tx = kMargin + 38.0f + pdf::Document::textWidth (chapters[i].title, 13.0f, pdf::Document::Bold) + 8.0f;
            const float px = kMargin + width() - pdf::Document::textWidth (pg, 11.0f, pdf::Document::Bold);
            for (float dx = tx; dx < px - 8.0f; dx += 5.0f) doc.rect (dx, yy - 1.0f, 1.2f, 1.2f, faint);
            doc.textRight (kMargin + width(), yy + 1.5f, pg, 11.0f, pdf::Document::Bold, ink);
            yy += 28.0f;
        }
        doc.selectPage (-1);
    }

    // Footers on every page but the cover: the report's line on the left, "Page x of y" on the right.
    void footers()
    {
        const int n = doc.pageCount();
        for (int i = 1; i < n; ++i)
        {
            doc.selectPage (i);
            const float h = doc.pageH(), w = doc.pageW() - 2.0f * kMargin;
            doc.line (kMargin, h - 32.0f, kMargin + w, h - 32.0f, 0.5f, rule);
            doc.text (kMargin, h - 18.0f, footerLeft, 8.0f, pdf::Document::Regular, dim);
            doc.textRight (kMargin + w, h - 18.0f, "Page " + juce::String (i + 1) + " of " + juce::String (n), 8.0f, pdf::Document::Regular, dim);
        }
        doc.selectPage (-1);
    }
};

// The cover: a coloured band with the report's kind, the venue and the session. Returns where the text below starts
// (the contents list; what the report is about goes in Layout::intro, after the contents).
inline float cover (Layout& L, const juce::String& kind, const juce::String& title, const juce::String& subtitle)
{
    auto& doc = L.doc;
    L.newPage();
    doc.rect (0.0f, 0.0f, doc.pageW(), 190.0f, accent);
    doc.rect (0.0f, 190.0f, doc.pageW(), 5.0f, accentDark);
    doc.text (Layout::kMargin, 64.0f, "LIVEALIGN", 10.0f, pdf::Document::Bold, juce::Colours::white.withAlpha (0.8f));
    doc.text (Layout::kMargin, 92.0f, kind, 13.0f, pdf::Document::Bold, juce::Colours::white.withAlpha (0.92f));
    doc.text (Layout::kMargin, 128.0f, title, 28.0f, pdf::Document::Bold, juce::Colours::white);
    doc.text (Layout::kMargin, 156.0f, subtitle, 12.0f, pdf::Document::Regular, juce::Colours::white);
    // your logo, top right of the band (Settings, Reports and files), on a white tile so any logo reads
    const juce::File logoFile (UserPrefs::get ("reportLogo"));
    if (logoFile.existsAsFile())
    {
        const auto logo = juce::ImageFileFormat::loadFrom (logoFile);
        if (logo.isValid() && logo.getWidth() > 0 && logo.getHeight() > 0)
        {
            const float maxW = 150.0f, maxH = 80.0f;
            const float sc = std::min (maxW / (float) logo.getWidth(), maxH / (float) logo.getHeight());
            const float w = (float) logo.getWidth() * sc, h = (float) logo.getHeight() * sc;
            const float x = doc.pageW() - Layout::kMargin - w, y = 52.0f;
            juce::Image flat (juce::Image::RGB, logo.getWidth(), logo.getHeight(), true);
            {
                juce::Graphics g (flat);
                g.fillAll (juce::Colours::white);
                g.drawImageAt (logo, 0, 0);
            }
            doc.rect (x - 8.0f, y - 8.0f, w + 16.0f, h + 16.0f, juce::Colours::white);
            doc.image (flat, x, y, w, h);
        }
    }
    return 232.0f;
}

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
        g.drawText (freqText (f), juce::Rectangle<float> (ax.x (f) - 40.0f, plot.getBottom() + 8.0f, 80.0f, 26.0f), juce::Justification::centred);
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
        g.setColour (seriesColour (k));
        g.strokePath (p, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        ++k;
    }
    return img;
}

// The speakers' responses with their processing, drawn for print: level on top, phase below (shown only where each
// speaker plays and was measured reliably, wrapped to +/-180 degrees without joining across the wrap), the top + sub
// sum, the crossover, and a legend under the chart so nothing overlaps. `asFound` adds each speaker's first
// measurement with the processing it had then (dashed).
inline juce::Image speakerChart (const SpeakerSystem& sys, bool asFound, double xoFreq, int w = 1640, int h = 1040)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colours::white);
    const auto grid = xo::analysisGrid();
    const size_t n = grid.size();

    struct Curve { int index; std::vector<float> db, ph; };
    std::vector<Curve> curves, found;
    std::vector<cplx> pairSum (n, cplx (0.0, 0.0));
    int inPair = 0;
    for (int i = 0; i < (int) sys.sources.size(); ++i)
    {
        const auto& s = sys.sources[(size_t) i];
        if (! s.hasResponse() || ! s.show) continue;
        const auto r = sys.processedResponse (i, grid);
        if (r.empty() || r.h.size() != n) continue;
        Curve c { i, std::vector<float> (n, NAN), std::vector<float> (n, NAN) };
        float peak = -300.0f;
        for (size_t k = 0; k < n; ++k)
        {
            const double m = std::abs (r.h[k]);
            if (m < 1e-5) continue;
            c.db[k] = (float) (20.0 * std::log10 (m));
            peak = std::max (peak, c.db[k]);
        }
        for (size_t k = 0; k < n; ++k)
            if (std::isfinite (c.db[k]) && c.db[k] > peak - 24.0f && r.cohAt (k) >= 0.5)
                c.ph[k] = (float) (std::arg (r.h[k]) * 180.0 / kPi);
        curves.push_back (std::move (c));
        const int m = sys.master (i);
        if (sys.realPair() && (i == sys.xoHigh || i == sys.xoLow || m == sys.xoHigh || m == sys.xoLow)
            && (m == i || ! sys.sources[(size_t) m].show))
        {
            for (size_t k = 0; k < n; ++k) pairSum[k] += r.h[k];
            ++inPair;
        }
    }
    if (asFound)
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto& s = sys.sources[(size_t) i];
            if (! s.show || s.asFound.freq.size() < 4) continue;
            Curve c { i, std::vector<float> (n, NAN), {} };
            for (size_t k = 0; k < n; ++k)
            {
                if (grid[k] < s.asFound.freq.front() || grid[k] > s.asFound.freq.back()) continue;
                const double v = s.asFound.dbAt (grid[k]) + 20.0 * std::log10 (std::abs (s.asFoundProc.at (grid[k])) + 1e-12);
                if (std::isfinite (v) && v > -200.0) c.db[k] = (float) v;
            }
            found.push_back (std::move (c));
        }
    std::vector<float> sumDb (n, NAN);
    const bool hasSum = inPair > 1;
    if (hasSum)
        for (size_t k = 0; k < n; ++k)
            if (std::abs (pairSum[k]) > 1e-5) sumDb[k] = (float) (20.0 * std::log10 (std::abs (pairSum[k])));

    float hi = -200.0f;
    for (const auto* set : { &curves, &found }) for (const auto& c : *set) for (float v : c.db) if (std::isfinite (v)) hi = std::max (hi, v);
    for (float v : sumDb) if (std::isfinite (v)) hi = std::max (hi, v);
    const float top = hi > -199.0f ? std::ceil ((hi + 3.0f) / 6.0f) * 6.0f : 6.0f, range = 48.0f;

    // the panes, then the frequency labels and the legend below
    const auto plot = juce::Rectangle<float> (96.0f, 40.0f, (float) w - 126.0f, (float) h - 40.0f - 150.0f);
    auto rest = plot;
    const auto mag = rest.removeFromTop (plot.getHeight() * 0.64f);
    rest.removeFromTop (22.0f);
    const auto ph = rest;
    ui::FreqAxis ax { 20.0, 20000.0, plot };
    auto yDb = [&] (float db) { return mag.getY() + (top - db) / range * mag.getHeight(); };
    auto yPh = [&] (float deg) { return ph.getY() + (180.0f - deg) / 360.0f * ph.getHeight(); };
    const juce::Colour minor (0xffeef1f5), major (0xffd5dbe3);

    g.setFont (juce::FontOptions (24.0f));
    for (const auto& pane : { mag, ph })
    {
        g.setColour (minor);
        for (int decade = 10; decade <= 10000; decade *= 10)
            for (int k = 2; k <= 9; ++k) if (decade * k >= 20 && decade * k <= 20000) g.drawVerticalLine ((int) ax.x (decade * k), pane.getY(), pane.getBottom());
        g.setColour (major);
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 }) g.drawVerticalLine ((int) ax.x (f), pane.getY(), pane.getBottom());
    }
    g.setColour (dim);
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        g.drawText (freqText (f), juce::Rectangle<float> (ax.x (f) - 40.0f, ph.getBottom() + 8.0f, 80.0f, 26.0f), juce::Justification::centred);
    for (float db = top; db >= top - range - 0.1f; db -= 6.0f)
    {
        g.setColour (minor);
        g.drawHorizontalLine ((int) yDb (db), mag.getX(), mag.getRight());
        const int step = (int) std::lround (top - db);
        if (step % 12 != 0) continue;   // a label every 12 dB
        g.setColour (dim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (10.0f, yDb (db) - 12.0f, 76.0f, 24.0f), juce::Justification::centredRight);
    }
    for (int deg : { 180, 90, 0, -90, -180 })
    {
        g.setColour (deg == 0 ? major : minor);
        g.drawHorizontalLine ((int) yPh ((float) deg), ph.getX(), ph.getRight());
        if (deg == 90 || deg == -90) continue;   // labels at 180, 0 and -180 only
        g.setColour (dim);
        g.drawText (juce::String (deg) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
                    juce::Rectangle<float> (10.0f, juce::jlimit (ph.getY() - 4.0f, ph.getBottom() - 20.0f, yPh ((float) deg) - 12.0f), 76.0f, 24.0f),
                    juce::Justification::centredRight);
    }
    g.setColour (dim);
    g.drawText ("dB", juce::Rectangle<float> (10.0f, 4.0f, 76.0f, 28.0f), juce::Justification::centredRight);
    g.drawText ("Phase", juce::Rectangle<float> (ph.getX() + 8.0f, ph.getY() + 4.0f, 200.0f, 24.0f), juce::Justification::centredLeft);
    g.setColour (major);
    g.drawRect (mag, 1.5f);
    g.drawRect (ph, 1.5f);

    auto magPath = [&] (const std::vector<float>& v)
    {
        juce::Path p;
        bool pen = false;
        for (size_t k = 0; k < n && k < v.size(); ++k)
        {
            if (! std::isfinite (v[k])) { pen = false; continue; }
            const float x = ax.x (grid[k]), y = juce::jlimit (mag.getY(), mag.getBottom(), yDb (v[k]));
            if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
        }
        return p;
    };
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (mag.toNearestInt());
        for (const auto& c : found)
        {
            juce::Path d;
            const float dashes[] = { 14.0f, 10.0f };
            juce::PathStrokeType (2.4f).createDashedStroke (d, magPath (c.db), dashes, 2);
            g.setColour (seriesColour (c.index).withAlpha (0.75f));
            g.fillPath (d);
        }
        for (const auto& c : curves)
        {
            g.setColour (seriesColour (c.index));
            g.strokePath (magPath (c.db), juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (hasSum)
        {
            g.setColour (ink);
            g.strokePath (magPath (sumDb), juce::PathStrokeType (4.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (ph.toNearestInt());
        for (const auto& c : curves)
        {
            juce::Path p;
            bool pen = false;
            float last = 0.0f;
            for (size_t k = 0; k < n; ++k)
            {
                const float v = c.ph[k];
                if (! std::isfinite (v)) { pen = false; continue; }
                const float x = ax.x (grid[k]), y = yPh (v);
                if (! pen || std::abs (v - last) > 180.0f) p.startNewSubPath (x, y);   // a wrap: start again, don't draw across
                else p.lineTo (x, y);
                pen = true;
                last = v;
            }
            g.setColour (seriesColour (c.index).withAlpha (0.9f));
            g.strokePath (p, juce::PathStrokeType (2.4f));
        }
    }
    if (xoFreq > 20.0 && xoFreq < 20000.0)
    {
        const float x = ax.x (xoFreq);
        juce::Path line, dashed;
        line.startNewSubPath (x, mag.getY());
        line.lineTo (x, ph.getBottom());
        const float dashes[] = { 10.0f, 8.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (accentDark);
        g.fillPath (dashed);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        const auto t = "XO " + hzText (xoFreq);
        const bool left = x > mag.getRight() - 200.0f;
        g.drawText (t, juce::Rectangle<float> (left ? x - 196.0f : x + 8.0f, mag.getY() + 6.0f, 188.0f, 28.0f),
                    left ? juce::Justification::centredRight : juce::Justification::centredLeft);
    }

    // legend: a row under the frequency labels, wrapping to a second row when needed
    std::vector<std::pair<juce::String, juce::Colour>> keys;
    for (const auto& c : curves) keys.push_back ({ sys.nameOf (c.index), seriesColour (c.index) });
    if (hasSum) keys.push_back ({ "Top + sub sum", ink });
    if (! found.empty()) keys.push_back ({ "As found (dashed)", dim });
    g.setFont (juce::FontOptions (24.0f));
    float lx = plot.getX(), ly = ph.getBottom() + 52.0f;
    for (const auto& [name, col] : keys)
    {
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 64.0f;
        if (lx + tw > plot.getRight() && lx > plot.getX()) { lx = plot.getX(); ly += 36.0f; }
        g.setColour (col);
        g.fillRoundedRectangle (lx, ly + 10.0f, 30.0f, 6.0f, 3.0f);
        g.setColour (ink);
        g.drawText (name, juce::Rectangle<float> (lx + 38.0f, ly, tw - 38.0f, 26.0f), juce::Justification::centredLeft);
        lx += tw;
    }
    if (curves.empty())
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (28.0f));
        g.drawText ("No responses.", mag, juce::Justification::centred);
    }
    return img;
}

// A component (a graph from the app) drawn on a light background, for print.
template <typename Comp, typename Setup>
inline juce::Image snapshotLight (Comp& c, int w, int h, Setup setup)
{
    const auto* saved = ui::currentPalette();
    ui::currentPalette() = &ui::lightPalette();
    c.setBounds (0, 0, w, h);
    setup (c);
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    ui::currentPalette() = saved;
    return img;
}
} // namespace detail

inline juce::String filterText (const xo::Filter& f, const char* kind)
{
    if (! f.active()) return "-";
    return juce::String (kind) + " " + xo::typeShort (f.type) + juce::String (f.slope) + " @ " + juce::String (juce::roundToInt (f.freq)) + " Hz";
}

inline juce::String processingText (const xo::Processing& pr)
{
    juce::StringArray parts;
    if (pr.hp.active()) parts.add (filterText (pr.hp, "HPF"));
    if (pr.lp.active()) parts.add (filterText (pr.lp, "LPF"));
    parts.add (juce::String (pr.delayMs, 2) + " ms");
    parts.add ((pr.gainDb >= 0.0 ? "+" : "") + juce::String (pr.gainDb, 1) + " dB");
    if (pr.invert) parts.add ("inverted");
    if (! pr.eq.empty()) parts.add ("EQ " + juce::String ((int) pr.eq.size()) + (pr.eq.size() == 1 ? " band" : " bands"));
    return parts.joinIntoString (", ");
}

// Which sections the person left out (a comma list in the preferences).
inline bool included (const juce::StringArray& skip, const char* id) { return ! skip.contains (id); }
} // namespace la::report
