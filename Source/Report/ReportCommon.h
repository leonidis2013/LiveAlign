// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// What LiveAlign's PDF reports share: the page layout (chapters with a bold numbered heading, portrait or landscape
// pages, a footer with "page x of y", tables, pictures), the contents list on the cover, and charts drawn for print.
#include "PdfWriter.h"
#include "../PluginProcessor.h"
#include "../UI/Graph.h"

namespace la::report
{
namespace detail
{
const juce::Colour ink (0xff1c232c), dim (0xff5d6876), faint (0xff97a1ae), rule (0xffd5dbe3), band (0xfff2f4f7),
                   accent (0xff0f9488), good (0xff23a056), warn (0xffcf9a10), bad (0xffd63232);

struct Layout
{
    Layout (pdf::Document& d, juce::String footer) : doc (d), footerLeft (std::move (footer)) {}
    pdf::Document& doc;
    juce::String footerLeft;
    float y = 0.0f;
    struct Chapter { juce::String title; int page; };
    std::vector<Chapter> chapters;
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
        doc.line (kMargin, y, kMargin + width(), y, 1.2f, ink);
        y += 22.0f;
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
        auto drawHead = [&]
        {
            doc.rect (kMargin, y - 9.0f, width(), 14.0f, band);
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
            if (y + h > bottom()) { newPage (doc.pageW() > doc.pageH()); drawHead(); }
            for (size_t c = 0; c < ws.size(); ++c)
                for (int l = 0; l < cells[c].size(); ++l)
                    doc.text (xs[c] + 4.0f, y + (float) l * size * 1.3f, cells[c][l], size, pdf::Document::Regular, ink);
            y += h;
            doc.line (kMargin, y - size - 1.0f, kMargin + width(), y - size - 1.0f, 0.3f, rule);
        }
        y += 6.0f;
    }

    void picture (const juce::Image& img, float maxH)
    {
        if (! img.isValid()) return;
        float w = width(), h = w * (float) img.getHeight() / (float) img.getWidth();
        if (h > maxH) { h = maxH; w = h * (float) img.getWidth() / (float) img.getHeight(); }
        need (h + 10.0f);
        doc.image (img, kMargin + (width() - w) * 0.5f, y, w, h);
        y += h + 10.0f;
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
        doc.text (kMargin, yy, "CONTENTS", 9.0f, pdf::Document::Bold, dim);
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

// The cover: a coloured band with the report's kind, the venue and the session. Returns where the text below starts.
inline float cover (Layout& L, const juce::String& kind, const juce::String& title, const juce::String& subtitle)
{
    auto& doc = L.doc;
    L.newPage();
    doc.rect (0.0f, 0.0f, doc.pageW(), 190.0f, accent);
    doc.rect (0.0f, 190.0f, doc.pageW(), 5.0f, ink);
    doc.text (Layout::kMargin, 64.0f, "LIVEALIGN", 10.0f, pdf::Document::Bold, juce::Colours::white.withAlpha (0.8f));
    doc.text (Layout::kMargin, 92.0f, kind, 13.0f, pdf::Document::Bold, juce::Colours::white.withAlpha (0.92f));
    doc.text (Layout::kMargin, 128.0f, title, 28.0f, pdf::Document::Bold, juce::Colours::white);
    doc.text (Layout::kMargin, 156.0f, subtitle, 12.0f, pdf::Document::Regular, juce::Colours::white);
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
