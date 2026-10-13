#pragma once
// Right-hand column: sound level, input meters, delay finder and saved traces.
#include <functional>
#include "Graph.h"
#include "../Analyzer.h"
#include "../TraceStore.h"
#include "../Analysis/TargetCurves.h"

namespace la::ui
{
inline juce::String refSourceName (int refSource)
{
    if (refSource == 0) return "Off";
    if (refSource == 1) return "Generator";
    return "In " + juce::String (refSource - 1);
}

//==============================================================================
class SplPanel : public juce::Component
{
public:
    explicit SplPanel (Settings& s) : settings (s)
    {
        resetButton.setTooltip ("Restart the Leq and maximum readings");
        resetButton.onClick = [this] { settings.requestResetLeq = true; };
        addAndMakeVisible (resetButton);
        calButton.setTooltip ("Calibrate the sound level with an acoustic calibrator, and load the mic's calibration file");
        calButton.onClick = [this] { if (onCalibrate) onCalibrate(); };
        addAndMakeVisible (calButton);
    }

    void update (const Snapshot& snap) { data = &snap; repaint(); }
    void setMicName (const juce::String& n) { if (n != micName) { micName = n; repaint(); } }

    void resized() override
    {
        resetButton.setBounds (getWidth() - 84, 6, 74, 24);
        calButton.setBounds (getWidth() - 84 - 86, 6, 82, 24);
    }

    std::function<void()> onCalibrate;
    juce::Component& calibrateButton() { return calButton; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Sound level");
        // which mic the level comes from
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText (micName, juce::Rectangle<int> (12, 26, getWidth() - 24, 14), juce::Justification::centredLeft, true);
        if (data == nullptr) return;
        const bool spl = data->splIsSpl;
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (42);

        auto big = r.removeFromTop (44);
        g.setColour (p.text);
        g.setFont (monoFont (34.0f, true));
        g.drawText (dbText (data->splAS, 1, spl ? -50.0f : -150.0f), big.removeFromLeft (140), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (13.0f, true));
        g.drawText (spl ? "dBA  slow" : "dBFS A  slow", big, juce::Justification::centredLeft);

        struct Item { const char* name; float v; };
        const Item items[] = { { "A fast", data->splAF }, { "C slow", data->splCS }, { "Z slow", data->splZS },
                               { "LAeq", data->leqA }, { "LCeq", data->leqC }, { "LAF max", data->maxAF } };
        const int colW = r.getWidth() / 3;
        for (int i = 0; i < 6; ++i)
        {
            auto cell = juce::Rectangle<int> (r.getX() + (i % 3) * colW, r.getY() + (i / 3) * 36, colW, 34);
            g.setColour (p.textDim);
            g.setFont (font (10.5f, true));
            g.drawText (items[i].name, cell.removeFromTop (14), juce::Justification::centredLeft);
            g.setColour (p.text);
            g.setFont (monoFont (14.0f));
            g.drawText (dbText (items[i].v, 1, spl ? -50.0f : -150.0f), cell, juce::Justification::centredLeft);
        }

        g.setColour (spl ? p.textDim : p.warn);
        g.setFont (font (11.0f));
        const int mins = (int) (data->leqSeconds / 60.0);
        g.drawText (spl ? "Leq over " + juce::String (mins) + " min " + juce::String ((int) data->leqSeconds % 60) + " s"
                        : "Not calibrated: levels are dBFS. Use Calibrate above, or Sound level on Mic setup.",
                    getLocalBounds().reduced (12, 6).removeFromBottom (16), juce::Justification::centredLeft);
    }

private:
    juce::String micName;
    Settings& settings;
    const Snapshot* data = nullptr;
    juce::TextButton resetButton { "Reset Leq" }, calButton { "Calibrate" };
};

//==============================================================================
class InputPanel : public juce::Component
{
public:
    explicit InputPanel (Settings& s) : settings (s) {}

    void setMicLabel (const juce::String& l) { if (l != micLabel) { micLabel = l; repaint(); } }

    void setLevels (float micPeak, float micRmsDb, float refPeak, float refRmsDb)
    {
        const auto now = juce::Time::getMillisecondCounter();
        mic.update (micPeak, micRmsDb, now);
        ref.update (refPeak, refRmsDb, now);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "Inputs");
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (30);
        drawMeter (g, r.removeFromTop (26), micLabel, mic, micColour (settings.focusMic()));
        r.removeFromTop (6);
        drawMeter (g, r.removeFromTop (26), "Ref  " + refSourceName (settings.refSource.load()), ref, pal().inputCol);
    }

private:
    struct Meter
    {
        float rmsDb = -120.0f, peakDb = -120.0f, holdDb = -120.0f;
        juce::uint32 holdUntil = 0, clipUntil = 0;
        void update (float peak, float rms, juce::uint32 now)
        {
            const float pk = 20.0f * std::log10 (juce::jmax (1e-6f, peak));
            peakDb = juce::jmax (pk, peakDb - 1.5f);
            rmsDb = rms;
            if (pk >= holdDb || now > holdUntil) { holdDb = pk; holdUntil = now + 1500; }
            if (pk >= -0.5f) clipUntil = now + 2000;
        }
    };

    void drawMeter (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, const Meter& m, juce::Colour c)
    {
        const auto& p = pal();
        g.setColour (p.text);
        g.setFont (font (11.5f, true));
        g.drawText (label, r.removeFromLeft (92), juce::Justification::centredLeft);
        auto led = r.removeFromRight (16).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
        const bool clip = juce::Time::getMillisecondCounter() < m.clipUntil;
        g.setColour (clip ? p.bad : p.border);
        g.fillEllipse (led);
        r.removeFromRight (6);
        auto valueArea = r.removeFromRight (48);
        auto bar = r.toFloat().withSizeKeepingCentre ((float) r.getWidth(), 10.0f);
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (bar, 3.0f);
        auto pos = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour (c.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pos (m.rmsDb)), 3.0f);
        g.setColour (c.withAlpha (0.35f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pos (m.peakDb)), 3.0f);
        g.setColour (m.holdDb > -3.0f ? p.warn : p.text);
        g.fillRect (bar.getX() + bar.getWidth() * pos (m.holdDb) - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        g.setColour (p.textDim);
        g.setFont (monoFont (11.0f));
        g.drawText (dbText (m.rmsDb, 1, -99.0f), valueArea, juce::Justification::centredRight);
    }

    Settings& settings;
    Meter mic, ref;
    juce::String micLabel { "Mic 1" };
};

//==============================================================================
class DelayPanel : public juce::Component
{
public:
    explicit DelayPanel (Settings& s) : settings (s)
    {
        findButton.setTooltip ("Measure how late the mic hears the reference and compensate for it (key: D). "
                               "Needs the generator or program audio running for about 3 seconds.");
        findButton.onClick = [this] { settings.requestFindDelay = true; waiting = true; repaint(); };
        addAndMakeVisible (findButton);

        const double steps[] = { -1.0, -0.1, 0.1, 1.0 };
        const char* names[] = { "-1", "-0.1", "+0.1", "+1" };
        for (int i = 0; i < 4; ++i)
        {
            nudge[i].setButtonText (names[i]);
            nudge[i].setTooltip ("Nudge the delay compensation by " + juce::String (names[i]) + " ms");
            const double st = steps[i];
            nudge[i].onClick = [this, st] { settings.delayMs() = (float) juce::jlimit (0.0, 2000.0, settings.delayMsValue() + st); };
            addAndMakeVisible (nudge[i]);
        }
        zeroButton.setTooltip ("Remove the delay compensation");
        zeroButton.onClick = [this] { settings.delayMs() = 0.0f; };
        addAndMakeVisible (zeroButton);
    }

    void setReport (const DelayReport& r)
    {
        if (r.serial != report.serial) waiting = false;
        report = r;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (30);
        findButton.setBounds (r.removeFromTop (28));
        r.removeFromTop (44);
        auto row = r.removeFromTop (24);
        const int w = (row.getWidth() - 4 * 4) / 5;
        for (auto& b : nudge) { b.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4); }
        zeroButton.setBounds (row);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Delay compensation");
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (62);

        const double ms = settings.delayMsValue();
        g.setColour (p.text);
        g.setFont (monoFont (20.0f, true));
        g.drawText (juce::String (ms, 2) + " ms", r.removeFromTop (28).removeFromLeft (140), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (12.0f));
        g.drawText (settings.distanceText (ms) + "  at " + settings.temperatureText(),
                    getLocalBounds().reduced (12, 0).withTrimmedTop (62).withHeight (28).withTrimmedLeft (140), juce::Justification::centredLeft);

        juce::String status;
        juce::Colour col = p.textDim;
        if (waiting) status = "Listening...";
        else if (report.serial == 0) status = "Press Find with the signal running.";
        else
        {
            const int m = settings.focusMic();
            int found = 0;
            for (int i = 0; i < report.micCount; ++i) found += report.valid[(size_t) i] ? 1 : 0;
            if (m >= report.micCount || ! report.valid[(size_t) m])
            {
                status = "No clear arrival on this mic. Is the signal on?";
                col = p.warn;
            }
            else
            {
                status = "Found " + juce::String (report.ms[(size_t) m], 2) + " ms (" + juce::String (juce::roundToInt (report.confidenceDb[(size_t) m])) + " dB clear)";
                if (report.inverted[(size_t) m]) { status << "  POLARITY INVERTED"; col = p.bad; }
                else col = p.good;
            }
            if (report.micCount > 1) status << "   " << found << "/" << report.micCount << " mics";
        }
        g.setColour (col);
        g.setFont (font (11.5f));
        g.drawText (status, getLocalBounds().reduced (12, 6).removeFromBottom (18), juce::Justification::centredLeft);
    }

private:
    Settings& settings;
    DelayReport report;
    bool waiting = false;
    juce::TextButton findButton { "Find delay" }, zeroButton { "Zero" };
    juce::TextButton nudge[4];
};

//==============================================================================
class TracesPanel : public juce::Component
{
public:
    std::function<void()> onCapture, onImport;
    std::function<void (int)> onRename, onExport, onDelete;

    TracesPanel (TraceStore& t, Settings& s) : traces (t), settings (s)
    {
        for (int i = 0; i < targets::count(); ++i) targetBox.addItem (targets::list()[(size_t) i].name, i + 1);
        targetBox.setSelectedItemIndex (settings.targetCurve.load(), juce::dontSendNotification);
        targetBox.onChange = [this]
        {
            settings.targetCurve = targetBox.getSelectedItemIndex();
            targetBox.setTooltip (targets::list()[(size_t) juce::jmax (0, targetBox.getSelectedItemIndex())].description);
        };
        targetBox.setTooltip (targets::list()[(size_t) settings.targetCurve.load()].description);
        addAndMakeVisible (targetBox);

        captureButton.setTooltip ("Save the current transfer function or RTA as a trace (key: C)");
        importButton.setTooltip ("Load a trace or speaker response from an FRD, TXT or CSV file");
        captureButton.onClick = [this] { if (onCapture) onCapture(); };
        importButton.onClick = [this] { if (onImport) onImport(); };
        addAndMakeVisible (captureButton);
        addAndMakeVisible (importButton);
    }

    void refresh() { if (traces.getVersion() != seenVersion) { seenVersion = traces.getVersion(); repaint(); } }
    void forceRepaint() { repaint(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (30).removeFromTop (26);
        const int w = (r.getWidth() - 6) / 2;
        captureButton.setBounds (r.removeFromLeft (w));
        r.removeFromLeft (6);
        importButton.setBounds (r);
        targetBox.setBounds (getLocalBounds().reduced (12, 0).withTrimmedTop (62).removeFromTop (26).withTrimmedLeft (54));
    }

    void syncTarget() { targetBox.setSelectedItemIndex (settings.targetCurve.load(), juce::dontSendNotification); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Traces");
        g.setColour (p.text);
        g.setFont (font (12.0f));
        g.drawText ("Target", getLocalBounds().reduced (12, 0).withTrimmedTop (62).removeFromTop (26).withWidth (50),
                    juce::Justification::centredLeft);
        const auto list = listArea();
        const auto& all = traces.all();
        if (all.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawFittedText ("No traces yet. Capture saves what is on screen so you can compare it with later changes.",
                              list.reduced (4, 6), juce::Justification::topLeft, 3);
            return;
        }

        scroll = juce::jlimit (0, juce::jmax (0, (int) all.size() - visibleRows()), scroll);
        for (int row = 0; row < visibleRows(); ++row)
        {
            const int i = scroll + row;
            if (i >= (int) all.size()) break;
            const auto& t = all[(size_t) i];
            auto r = rowBounds (row);
            if (i == hoverRow)
            {
                g.setColour (p.panelRaised);
                g.fillRoundedRectangle (r.toFloat(), 5.0f);
            }
            auto sw = r.removeFromLeft (26).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            const auto col = traceColour (t.colour);
            if (t.visible) { g.setColour (col); g.fillRoundedRectangle (sw, 3.0f); }
            g.setColour (col);
            g.drawRoundedRectangle (sw, 3.0f, 1.5f);

            auto menu = r.removeFromRight (24);
            g.setColour (p.textDim);
            g.setFont (font (14.0f, true));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x8b\xaf")), menu, juce::Justification::centred);

            auto tag = r.removeFromRight (34);
            g.setFont (font (9.5f, true));
            g.drawText (t.kind == Trace::Rta ? "RTA" : "TF", tag, juce::Justification::centred);

            g.setColour (t.visible ? p.text : p.textDim);
            g.setFont (font (12.0f));
            g.drawText (t.name, r.reduced (2, 0), juce::Justification::centredLeft, true);
        }

        if ((int) all.size() > visibleRows())
        {
            g.setColour (p.textDim);
            g.setFont (font (10.5f));
            g.drawText (juce::String (scroll + 1) + "-" + juce::String (juce::jmin ((int) all.size(), scroll + visibleRows())) + " of "
                            + juce::String (all.size()) + "  (scroll for more)",
                        list.withTop (list.getBottom() - 16), juce::Justification::centredRight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.getPosition());
        if (r != hoverRow) { hoverRow = r; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scroll -= w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        if (! traces.valid (i)) return;
        const int row = i - scroll;
        auto r = rowBounds (row);
        if (e.x < r.getX() + 26)
        {
            traces.setVisible (i, ! traces.all()[(size_t) i].visible);
            repaint();
            return;
        }
        if (e.x > r.getRight() - 24 || e.mods.isPopupMenu())
            showMenu (i);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        if (traces.valid (i) && onRename) onRename (i);
    }

private:
    void showMenu (int i)
    {
        juce::PopupMenu m;
        m.addItem (1, traces.all()[(size_t) i].visible ? "Hide" : "Show");
        m.addItem (2, "Rename...");
        m.addItem (3, "Export as FRD...");
        m.addSeparator();
        m.addItem (4, "Delete");
        juce::Component::SafePointer<TracesPanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, i] (int result)
        {
            if (safe == nullptr || ! safe->traces.valid (i)) return;
            switch (result)
            {
                case 1: safe->traces.setVisible (i, ! safe->traces.all()[(size_t) i].visible); safe->repaint(); break;
                case 2: if (safe->onRename) safe->onRename (i); break;
                case 3: if (safe->onExport) safe->onExport (i); break;
                case 4: if (safe->onDelete) safe->onDelete (i); break;
                default: break;
            }
        });
    }

    juce::Rectangle<int> listArea() const { return getLocalBounds().reduced (8, 0).withTrimmedTop (96).withTrimmedBottom (8); }
    int visibleRows() const { return juce::jmax (1, listArea().getHeight() / kRowH); }
    juce::Rectangle<int> rowBounds (int row) const { return listArea().withHeight (kRowH).translated (0, row * kRowH); }

    int rowAt (juce::Point<int> pt) const
    {
        if (! listArea().contains (pt)) return -1;
        const int row = (pt.y - listArea().getY()) / kRowH;
        const int i = scroll + row;
        return traces.valid (i) && row < visibleRows() ? i : -1;
    }

    static constexpr int kRowH = 26;
    TraceStore& traces;
    Settings& settings;
    juce::TextButton captureButton { "Capture" }, importButton { "Import..." };
    juce::ComboBox targetBox;
    int seenVersion = -1, hoverRow = -1, scroll = 0;
};
} // namespace la::ui
