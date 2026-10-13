// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Venue plan: a simple sketch of the setup seen from above (not to scale). The speakers come from the Speakers step
// and the mics from the Mic setup table, each tagged and pointing the way it is aimed; areas, named mic positions
// and notes can be added. Drag to move, drag the small handle to turn, double-click to rename. Saved with the venue
// and exported as a picture.
#include <functional>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class PlanCanvas : public juce::Component
{
public:
    enum class Sel { None, Speaker, Mic, Item };
    std::function<void()> onSelectionChanged, onChanged;
    std::function<void (int item)> onEditItem;     // double-click on an area, marker or note

    explicit PlanCanvas (LiveAlignProcessor& p) : proc (p), plan (p.plan) { setWantsKeyboardFocus (true); }

    Sel selType() const { return selType_; }
    int selIndex() const { return selIndex_; }
    void select (Sel t, int i) { selType_ = t; selIndex_ = i; repaint(); if (onSelectionChanged) onSelectionChanged(); }

    int numSpeakers() const { return juce::jmin ((int) proc.system.sources.size(), VenuePlan::kMaxSpeakers); }
    int numMics() const { return proc.settings.micCount.load(); }

    // The drawing area, kept at 16:10 inside the component.
    juce::Rectangle<float> sheet() const
    {
        return getLocalBounds().toFloat().reduced (1.0f);
    }

    // Renders the drawing; `forExport` adds the title block and uses a white page.
    void render (juce::Graphics& g, juce::Rectangle<float> area, bool forExport) const
    {
        const auto& p = pal();
        const juce::Colour paper = forExport ? juce::Colours::white : p.bg.interpolatedWith (p.panel, 0.5f);
        const juce::Colour ink = forExport ? juce::Colour (0xff1c232c) : p.text;
        const juce::Colour inkDim = forExport ? juce::Colour (0xff5d6876) : p.textDim;
        const juce::Colour line = forExport ? juce::Colour (0xffc5ccd6) : p.border;

        g.setColour (paper);
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (line);
        g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.2f);

        auto draw = area.reduced (14.0f);
        auto title = draw.removeFromBottom (forExport ? 54.0f : 30.0f);
        drawArea = draw;

        // light dot grid so it reads as a drawing, without implying a scale
        g.setColour (line.withAlpha (0.5f));
        for (float gx = draw.getX(); gx <= draw.getRight(); gx += 24.0f)
            for (float gy = draw.getY(); gy <= draw.getBottom(); gy += 24.0f)
                g.fillRect (gx, gy, 1.2f, 1.2f);

        // areas first, then notes and markers, then speakers and mics on top
        for (int i = 0; i < (int) plan.items.size(); ++i)
        {
            const auto& it = plan.items[(size_t) i];
            if (it.kind != PlanItem::Area) continue;
            const auto r = itemRect (it);
            const bool sel = selType_ == Sel::Item && selIndex_ == i && ! forExport;
            g.setColour (inkDim.withAlpha (0.07f));
            g.fillRoundedRectangle (r, 6.0f);
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r, 6.0f);
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (sel ? 1.6f : 1.1f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (sel ? p.accent : inkDim.withAlpha (0.8f));
            g.fillPath (dashed);
            g.setColour (inkDim);
            g.setFont (font (12.0f, true));
            g.drawText (it.text.toUpperCase(), r.reduced (8.0f, 5.0f), juce::Justification::topLeft, true);
            if (sel) { g.setColour (p.accent); g.fillRect (resizeHandle (it)); }
        }
        for (int i = 0; i < (int) plan.items.size(); ++i)
        {
            const auto& it = plan.items[(size_t) i];
            if (it.kind == PlanItem::Area) continue;
            const bool sel = selType_ == Sel::Item && selIndex_ == i && ! forExport;
            const auto c = toPx (it.x, it.y);
            if (it.kind == PlanItem::Marker)
            {
                // a named mic position (one mic moved between places, an alignment spot ...)
                juce::Path d;
                d.addPolygon (c, 4, 7.5f, 0.0f);
                g.setColour (sel ? p.accent : inkDim);
                g.strokePath (d, juce::PathStrokeType (1.5f));
                g.setFont (font (11.5f));
                g.drawText (it.text, juce::Rectangle<float> (c.x + 11.0f, c.y - 9.0f, 160.0f, 18.0f), juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour (sel ? p.accent : ink);
                g.setFont (font (12.0f, false).italicised());
                g.drawFittedText (it.text, noteRect (it).toNearestInt(), juce::Justification::centred, 3);
            }
        }

        for (int i = 0; i < numSpeakers(); ++i) drawSpeaker (g, i, forExport, ink);
        for (int m = 0; m < numMics(); ++m) drawMic (g, m, forExport, ink);

        // title block
        g.setColour (line);
        g.drawHorizontalLine ((int) title.getY() + 4, title.getX(), title.getRight());
        title.removeFromTop (8.0f);
        const auto& sessions = proc.getSessions();
        const int cur = proc.currentSession();
        juce::String left = proc.venueName;
        if (cur < (int) sessions.size()) left << "   -   " << sessions[(size_t) cur].name;
        g.setColour (ink);
        g.setFont (font (forExport ? 15.0f : 12.5f, true));
        g.drawText (left, title, juce::Justification::topLeft, true);
        g.setColour (inkDim);
        g.setFont (font (11.0f));
        g.drawText ("Venue plan (not to scale)   " + LiveAlignProcessor::today(), title, juce::Justification::topRight, true);
        if (forExport)
        {
            g.drawText (legendText(), title.withTrimmedTop (22.0f), juce::Justification::topLeft, true);
        }
    }

    void paint (juce::Graphics& g) override { render (g, sheet(), false); }

    juce::Image exportImage (int width = 2000) const
    {
        auto s = sheet();
        if (s.getWidth() < 100.0f || s.getHeight() < 100.0f) s = { 0.0f, 0.0f, 1000.0f, 640.0f };   // not shown yet
        const int height = (int) ((float) width * s.getHeight() / juce::jmax (1.0f, s.getWidth()));
        juce::Image img (juce::Image::ARGB, width, height, true);
        juce::Graphics g (img);
        // draw at screen size, scaled up, so text and symbols keep their proportions
        const float scale = (float) width / juce::jmax (400.0f, s.getWidth());
        g.addTransform (juce::AffineTransform::scale (scale));
        const auto saved = drawArea;
        render (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width / scale, (float) height / scale), true);
        drawArea = saved;
        return img;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto pt = e.position;
        drag = Drag::None;
        // handles of the selection first
        if (selType_ == Sel::Item && selIndex_ < (int) plan.items.size() && plan.items[(size_t) selIndex_].kind == PlanItem::Area
            && resizeHandle (plan.items[(size_t) selIndex_]).expanded (4.0f).contains (pt))
        { drag = Drag::Resize; return; }
        if ((selType_ == Sel::Speaker || selType_ == Sel::Mic) && rotateHandle (selType_, selIndex_).getDistanceFrom (pt) < 9.0f)
        { drag = Drag::Rotate; return; }

        Sel t = Sel::None;
        int idx = -1;
        for (int m = numMics(); --m >= 0 && t == Sel::None;) if (toPx (plan.mics[(size_t) m].x, plan.mics[(size_t) m].y).getDistanceFrom (pt) < 16.0f) { t = Sel::Mic; idx = m; }
        for (int i = numSpeakers(); --i >= 0 && t == Sel::None;) if (toPx (plan.speakers[(size_t) i].x, plan.speakers[(size_t) i].y).getDistanceFrom (pt) < 20.0f) { t = Sel::Speaker; idx = i; }
        for (int i = (int) plan.items.size(); --i >= 0 && t == Sel::None;)
        {
            const auto& it = plan.items[(size_t) i];
            const bool hit = it.kind == PlanItem::Area ? itemRect (it).contains (pt)
                           : it.kind == PlanItem::Marker ? toPx (it.x, it.y).getDistanceFrom (pt) < 12.0f
                                                         : noteRect (it).contains (pt);
            if (hit && (it.kind != PlanItem::Area || t == Sel::None)) { t = Sel::Item; idx = i; }
        }
        // areas are underneath: prefer anything else at the same point
        if (t == Sel::Item && plan.items[(size_t) idx].kind == PlanItem::Area)
            for (int i = (int) plan.items.size(); --i >= 0;)
            {
                const auto& it = plan.items[(size_t) i];
                if (it.kind == PlanItem::Marker && toPx (it.x, it.y).getDistanceFrom (pt) < 12.0f) { idx = i; break; }
                if (it.kind == PlanItem::Note && noteRect (it).contains (pt)) { idx = i; break; }
            }
        select (t, idx);
        if (t != Sel::None)
        {
            drag = Drag::Move;
            const auto c = selectedCentre();
            grabOffset = { pt.x - c.x, pt.y - c.y };
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::None) return;
        const auto pt = e.position;
        if (drag == Drag::Move)
        {
            const auto n = toNorm ({ pt.x - grabOffset.x, pt.y - grabOffset.y });
            if (selType_ == Sel::Speaker) { auto& s = plan.speakers[(size_t) selIndex_]; s.x = n.x; s.y = n.y; }
            else if (selType_ == Sel::Mic) { auto& s = plan.mics[(size_t) selIndex_]; s.x = n.x; s.y = n.y; }
            else if (selType_ == Sel::Item) { auto& it = plan.items[(size_t) selIndex_]; it.x = n.x; it.y = n.y; }
        }
        else if (drag == Drag::Rotate)
        {
            const auto c = selectedCentre();
            float a = juce::radiansToDegrees (std::atan2 (pt.y - c.y, pt.x - c.x));
            if (! e.mods.isAltDown()) a = 15.0f * std::round (a / 15.0f);   // Alt: free
            symbol (selType_, selIndex_).angle = a;
        }
        else if (drag == Drag::Resize)
        {
            auto& it = plan.items[(size_t) selIndex_];
            const auto tl = toPx (it.x - it.w * 0.5f, it.y - it.h * 0.5f);
            const auto br = toNorm (pt);
            const auto tln = toNorm (tl);
            it.w = juce::jlimit (0.05f, 1.0f, br.x - tln.x);
            it.h = juce::jlimit (0.04f, 1.0f, br.y - tln.y);
            it.x = tln.x + it.w * 0.5f;
            it.y = tln.y + it.h * 0.5f;
        }
        plan.edited = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        // a mic dropped on a named position takes that position's name (and sits on it)
        if (drag == Drag::Move && selType_ == Sel::Mic)
        {
            auto& s = plan.mics[(size_t) selIndex_];
            const auto c = toPx (s.x, s.y);
            for (const auto& it : plan.items)
                if (it.kind == PlanItem::Marker && toPx (it.x, it.y).getDistanceFrom (c) < 18.0f)
                {
                    proc.setMicPosition (selIndex_, it.text);
                    s.x = it.x;
                    s.y = it.y;
                    repaint();
                    if (onSelectionChanged) onSelectionChanged();
                    break;
                }
        }
        if (drag != Drag::None && onChanged) onChanged();
        drag = Drag::None;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (selType_ == Sel::Item && onEditItem) onEditItem (selIndex_);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selType_ == Sel::Item)
        {
            removeSelectedItem();
            return true;
        }
        if ((k.getTextCharacter() == '[' || k.getTextCharacter() == ']') && (selType_ == Sel::Speaker || selType_ == Sel::Mic))
        {
            symbol (selType_, selIndex_).angle += k.getTextCharacter() == ']' ? 15.0f : -15.0f;
            plan.edited = true;
            repaint();
            if (onChanged) onChanged();
            return true;
        }
        return false;
    }

    void removeSelectedItem()
    {
        if (selType_ != Sel::Item || selIndex_ < 0 || selIndex_ >= (int) plan.items.size()) return;
        plan.items.erase (plan.items.begin() + selIndex_);
        plan.edited = true;
        select (Sel::None, -1);
        if (onChanged) onChanged();
    }

    PlanSymbol& symbol (Sel t, int i) { return t == Sel::Speaker ? plan.speakers[(size_t) i] : plan.mics[(size_t) i]; }

    juce::String legendText() const
    {
        return juce::String (juce::CharPointer_UTF8 ("Speakers: arrow = direction it faces.   Mics: arrow = aimed at the speaker (0\xc2\xb0 cal), "
                                                    "dot in circle = pointing up (90\xc2\xb0 cal).   \xe2\x97\x87 = named mic position."));
    }

private:
    enum class Drag { None, Move, Rotate, Resize };

    juce::Point<float> toPx (float x, float y) const { return { drawArea.getX() + x * drawArea.getWidth(), drawArea.getY() + y * drawArea.getHeight() }; }
    juce::Point<float> toNorm (juce::Point<float> p) const
    {
        return { juce::jlimit (0.0f, 1.0f, (p.x - drawArea.getX()) / juce::jmax (1.0f, drawArea.getWidth())),
                 juce::jlimit (0.0f, 1.0f, (p.y - drawArea.getY()) / juce::jmax (1.0f, drawArea.getHeight())) };
    }

    juce::Rectangle<float> itemRect (const PlanItem& it) const
    {
        const auto c = toPx (it.x, it.y);
        return juce::Rectangle<float> (it.w * drawArea.getWidth(), it.h * drawArea.getHeight()).withCentre (c);
    }
    juce::Rectangle<float> noteRect (const PlanItem& it) const { return juce::Rectangle<float> (170.0f, 40.0f).withCentre (toPx (it.x, it.y)); }
    juce::Rectangle<float> resizeHandle (const PlanItem& it) const { const auto r = itemRect (it); return { r.getRight() - 8.0f, r.getBottom() - 8.0f, 8.0f, 8.0f }; }

    juce::Point<float> selectedCentre() const
    {
        if (selType_ == Sel::Speaker) return toPx (plan.speakers[(size_t) selIndex_].x, plan.speakers[(size_t) selIndex_].y);
        if (selType_ == Sel::Mic) return toPx (plan.mics[(size_t) selIndex_].x, plan.mics[(size_t) selIndex_].y);
        if (selType_ == Sel::Item) return toPx (plan.items[(size_t) selIndex_].x, plan.items[(size_t) selIndex_].y);
        return {};
    }

    juce::Point<float> rotateHandle (Sel t, int i) const
    {
        const auto& s = t == Sel::Speaker ? plan.speakers[(size_t) i] : plan.mics[(size_t) i];
        const float r = t == Sel::Speaker ? 34.0f : 28.0f;
        const float a = juce::degreesToRadians (s.angle);
        return toPx (s.x, s.y) + juce::Point<float> (std::cos (a) * r, std::sin (a) * r);
    }

    void drawTag (juce::Graphics& g, juce::Point<float> c, const juce::String& line1, const juce::String& line2, juce::Colour col, juce::Colour ink, float angle) const
    {
        // the tag sits on the side away from where the symbol points
        const float a = juce::degreesToRadians (angle + 180.0f);
        const float w = 150.0f;
        auto pos = c + juce::Point<float> (std::cos (a) * 30.0f, std::sin (a) * 26.0f);
        juce::Rectangle<float> r (w, line2.isNotEmpty() ? 30.0f : 16.0f);
        r = r.withCentre (pos);
        if (std::cos (a) > 0.35f) r.setX (pos.x - 6.0f);
        else if (std::cos (a) < -0.35f) r.setX (pos.x - w + 6.0f);
        r = r.constrainedWithin (drawArea);
        const auto just = std::cos (a) > 0.35f ? juce::Justification::left : std::cos (a) < -0.35f ? juce::Justification::right : juce::Justification::centred;
        g.setColour (col);
        g.setFont (font (11.5f, true));
        g.drawText (line1, r.removeFromTop (15.0f), just, true);
        if (line2.isNotEmpty())
        {
            g.setColour (ink.withAlpha (0.75f));
            g.setFont (font (10.5f));
            g.drawText (line2, r, just, true);
        }
    }

    void drawSpeaker (juce::Graphics& g, int i, bool forExport, juce::Colour ink) const
    {
        const auto& s = plan.speakers[(size_t) i];
        if (! s.placed) return;
        const auto& src = proc.system.sources[(size_t) i];
        const auto col = forExport ? traceColour (i).darker (0.25f) : traceColour (i);
        const auto c = toPx (s.x, s.y);
        const float a = juce::degreesToRadians (s.angle);
        const bool sub = src.role == (int) SpeakerRole::Sub;
        // cabinet seen from above: the front (wider for a top, square for a sub) faces `angle`
        juce::Path body;
        if (sub) body.addRoundedRectangle (-13.0f, -13.0f, 26.0f, 26.0f, 3.0f);
        else
        {
            body.startNewSubPath (-10.0f, -8.0f);
            body.lineTo (10.0f, -14.0f);
            body.lineTo (10.0f, 14.0f);
            body.lineTo (-10.0f, 8.0f);
            body.closeSubPath();
        }
        const auto xf = juce::AffineTransform::rotation (a).translated (c);
        body.applyTransform (xf);
        g.setColour (col.withAlpha (0.25f));
        g.fillPath (body);
        g.setColour (col);
        g.strokePath (body, juce::PathStrokeType (1.8f));
        // direction arrow
        juce::Path arrow;
        arrow.startNewSubPath (14.0f, 0.0f);
        arrow.lineTo (26.0f, 0.0f);
        arrow.startNewSubPath (21.0f, -4.0f);
        arrow.lineTo (26.0f, 0.0f);
        arrow.lineTo (21.0f, 4.0f);
        arrow.applyTransform (xf);
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (sub)
        {
            g.setFont (font (9.0f, true));
            g.drawText ("SUB", juce::Rectangle<float> (30.0f, 12.0f).withCentre (c), juce::Justification::centred);
        }

        juce::String l1 = src.name.isNotEmpty() ? src.name : "Speaker " + juce::String (i + 1);
        if (src.count > 1) l1 << "  x" << src.count;
        juce::String l2 = speakerChannelShort (src.channel) + "  -  " + (src.output == 0 ? juce::String ("all outs") : "out " + juce::String (src.output));
        if (src.model.isNotEmpty()) l2 = src.model + "  -  " + l2;
        if (src.proc.delayMs > 0.005) l2 << "  -  " << juce::String (src.proc.delayMs, 2) << " ms";
        drawTag (g, c, l1, l2, col, ink, s.angle);

        if (! forExport && selType_ == Sel::Speaker && selIndex_ == i) drawSelection (g, c, 22.0f, rotateHandle (Sel::Speaker, i));
    }

    void drawMic (juce::Graphics& g, int m, bool forExport, juce::Colour ink) const
    {
        const auto& s = plan.mics[(size_t) m];
        if (! s.placed) return;
        const auto col = forExport ? micColour (m).darker (0.25f) : micColour (m);
        const auto c = toPx (s.x, s.y);
        const bool up = proc.ownAim (m) == 1;
        g.setColour (col.withAlpha (0.2f));
        g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c));
        g.setColour (col);
        g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c), 1.8f);
        if (up)
        {
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));   // pointing up, out of the page
        }
        else
        {
            // capsule pointing along `angle`
            juce::Path arrow;
            arrow.startNewSubPath (11.0f, 0.0f);
            arrow.lineTo (22.0f, 0.0f);
            arrow.startNewSubPath (17.0f, -4.0f);
            arrow.lineTo (22.0f, 0.0f);
            arrow.lineTo (17.0f, 4.0f);
            arrow.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (s.angle)).translated (c));
            g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setFont (monoFont (10.5f, true));
        if (up) g.drawText (juce::String (m + 1), juce::Rectangle<float> (14.0f, 12.0f).withCentre (c.translated (0.0f, -17.0f)), juce::Justification::centred);
        else g.drawText (juce::String (m + 1), juce::Rectangle<float> (14.0f, 12.0f).withCentre (c), juce::Justification::centred);

        juce::String l1 = proc.mics.nameOf (m);
        if (proc.mics.position[(size_t) m].isNotEmpty()) l1 << "  -  " << proc.mics.position[(size_t) m];
        juce::String l2 = "In " + juce::String (proc.settings.micInput[m].load() + 1);
        const auto ms = proc.micModelSerial (m);
        if (ms.isNotEmpty() && ms != proc.mics.nameOf (m)) l2 << "  -  " << ms;
        l2 << "  -  " << (up ? "pointing up" : "at the speaker");
        if (proc.calibrations (m).any()) l2 << " (" << calAngleName (proc.activeCalAngle (m)) << " cal)";
        drawTag (g, c, l1, l2, col, ink, up ? -90.0f : s.angle);

        if (! forExport && selType_ == Sel::Mic && selIndex_ == m)
            drawSelection (g, c, 17.0f, up ? juce::Point<float>() : rotateHandle (Sel::Mic, m));
    }

    void drawSelection (juce::Graphics& g, juce::Point<float> c, float r, juce::Point<float> handle) const
    {
        const auto& p = pal();
        g.setColour (p.accent);
        juce::Path ring, dashed;
        ring.addEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, ring, dashes, 2);
        g.fillPath (dashed);
        if (handle != juce::Point<float>())
        {
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (handle));
            g.setColour (p.onAccent);
            g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (handle), 1.0f);
        }
    }

    LiveAlignProcessor& proc;
    VenuePlan& plan;
    Sel selType_ = Sel::None;
    int selIndex_ = -1;
    Drag drag = Drag::None;
    juce::Point<float> grabOffset;
    mutable juce::Rectangle<float> drawArea;
};

//==============================================================================
class PlanPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChanged, onExport, onReport;
    std::function<void (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)>)> askText;

    explicit PlanPage (LiveAlignProcessor& p) : proc (p), canvas (p)
    {
        addAndMakeVisible (canvas);
        canvas.onSelectionChanged = [this] { refreshSelection(); };
        canvas.onChanged = [this] { changed(); };
        canvas.onEditItem = [this] (int i) { renameItem (i); };

        auto setup = [this] (juce::TextButton& b, const juce::String& tip) { b.setTooltip (tip); addAndMakeVisible (b); };
        setup (addAreaButton, "Add an area: stage, FOH, seating, balcony, choir ...");
        setup (addMarkerButton, "Add a named mic position: where a mic was moved to, or a time alignment spot");
        setup (addNoteButton, "Add a note on the drawing");
        setup (exportButton, "Save the plan as a picture (PNG) to print or share");
        setup (renameButton, "Rename the selected area, position or note (or double-click it)");
        setup (deleteButton, "Remove the selected area, position or note (Delete key)");
        setup (rotLeftButton, "Turn 15 degrees (key: [ )");
        setup (rotRightButton, "Turn 15 degrees (key: ] )");
        setup (resetButton, "Put any speakers and mics that are missing back on the drawing");
        setup (reportButton, "A PDF of this session's tuning: plan, speakers and processor settings, mics with serials, checks and measurements");
        reportButton.onClick = [this] { if (onReport) onReport(); };

        addAreaButton.onClick = [this]
        {
            juce::PopupMenu m;
            int id = 1;
            for (auto* n : { "Stage", "FOH", "Seating", "Balcony", "Choir", "Platform", "Overflow", "Lobby" }) m.addItem (id++, n);
            m.addItem (99, "Other...");
            juce::Component::SafePointer<PlanPage> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addAreaButton), [safe] (int r)
            {
                if (safe == nullptr || r == 0) return;
                static const char* names[] = { "Stage", "FOH", "Seating", "Balcony", "Choir", "Platform", "Overflow", "Lobby" };
                if (r == 99) safe->askName ("New area", "Area", [safe] (const juce::String& t) { if (safe != nullptr) safe->addItem (PlanItem::Area, t); });
                else safe->addItem (PlanItem::Area, names[r - 1]);
            });
        };
        addMarkerButton.onClick = [this]
        {
            juce::PopupMenu m;
            for (int i = 0; i < proc.positions.size(); ++i) m.addItem (i + 1, proc.positions[i]);
            for (int a = 0; a < (int) proc.system.alignments.size(); ++a)
            {
                const auto& sp = proc.system.alignments[(size_t) a].spot;
                if (sp.isNotEmpty() && ! proc.positions.contains (sp)) m.addItem (500 + a, sp + "  (alignment spot)");
            }
            m.addSeparator();
            m.addItem (999, "New position...");
            juce::Component::SafePointer<PlanPage> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addMarkerButton), [safe] (int r)
            {
                if (safe == nullptr || r == 0) return;
                auto& pr = safe->proc;
                if (r == 999)
                    safe->askName ("New mic position", "Row 5", [safe] (const juce::String& t)
                    {
                        if (safe == nullptr) return;
                        if (! safe->proc.positions.contains (t)) safe->proc.positions.add (t);
                        safe->addItem (PlanItem::Marker, t);
                    });
                else if (r >= 500 && r - 500 < (int) pr.system.alignments.size()) safe->addItem (PlanItem::Marker, pr.system.alignments[(size_t) (r - 500)].spot);
                else if (r - 1 < pr.positions.size()) safe->addItem (PlanItem::Marker, pr.positions[r - 1]);
            });
        };
        addNoteButton.onClick = [this] { askName ("New note", "Note", [this] (const juce::String& t) { addItem (PlanItem::Note, t); }); };
        exportButton.onClick = [this] { if (onExport) onExport(); };
        renameButton.onClick = [this] { if (canvas.selType() == PlanCanvas::Sel::Item) renameItem (canvas.selIndex()); };
        deleteButton.onClick = [this] { canvas.removeSelectedItem(); };
        rotLeftButton.onClick = [this] { turn (-15.0f); };
        rotRightButton.onClick = [this] { turn (15.0f); };
        resetButton.onClick = [this] { proc.plan.placeMissing (canvas.numSpeakers(), canvas.numMics()); changed(); };

        positionBox.setEditableText (true);
        positionBox.setTextWhenNothingSelected ("Position");
        positionBox.setTooltip ("Where this mic is (the same as Position on Mic setup). Dropping a mic on a named position also sets it.");
        positionBox.onChange = [this]
        {
            if (canvas.selType() != PlanCanvas::Sel::Mic) return;
            proc.setMicPosition (canvas.selIndex(), positionBox.getText());
            changed();
        };
        addAndMakeVisible (positionBox);
        aimBox.addItem (calAngleName (Cal0) + "  at the speaker", 1);
        aimBox.addItem (calAngleName (Cal90) + "  pointing up", 2);
        aimBox.setTooltip ("How this mic is aimed (the same setting as Aim on Mic setup)");
        aimBox.onChange = [this]
        {
            if (canvas.selType() == PlanCanvas::Sel::Mic) { proc.setMicAim (canvas.selIndex(), aimBox.getSelectedItemIndex()); changed(); }
        };
        addAndMakeVisible (aimBox);

        refresh();
        startTimerHz (2);
    }

    PlanCanvas& getCanvas() { return canvas; }

    // After a venue is opened, or speakers or mics changed.
    void refresh()
    {
        proc.plan.placeMissing (canvas.numSpeakers(), canvas.numMics());
        refreshSelection();
        canvas.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, side.toFloat(), "Plan");
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        g.drawFittedText ("A sketch of the setup, not to scale. Speakers and mics come from the Speakers and Mic setup steps. "
                          "Drag to move; drag the small dot to turn (hold Alt for any angle); double-click to rename.",
                          helpArea, juce::Justification::topLeft, 5);

        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("ADD", addLabel, juce::Justification::centredLeft);
        g.drawText ("SELECTED", selLabel, juce::Justification::centredLeft);
        g.setColour (p.text);
        g.setFont (font (12.5f, true));
        g.drawFittedText (selectionText, selArea, juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        side = r.removeFromRight (280);
        r.removeFromRight (10);
        canvas.setBounds (r);

        auto c = side.reduced (14, 0).withTrimmedTop (36);
        helpArea = c.removeFromTop (78);
        c.removeFromTop (8);
        addLabel = c.removeFromTop (18);
        c.removeFromTop (4);
        addAreaButton.setBounds (c.removeFromTop (30));
        c.removeFromTop (6);
        addMarkerButton.setBounds (c.removeFromTop (30));
        c.removeFromTop (6);
        addNoteButton.setBounds (c.removeFromTop (30));
        c.removeFromTop (18);
        selLabel = c.removeFromTop (18);
        c.removeFromTop (4);
        selArea = c.removeFromTop (40);
        c.removeFromTop (4);
        {
            auto row = c.removeFromTop (30);
            rotLeftButton.setBounds (row.removeFromLeft ((row.getWidth() - 8) / 2));
            row.removeFromLeft (8);
            rotRightButton.setBounds (row);
        }
        c.removeFromTop (6);
        aimBox.setBounds (c.removeFromTop (30));
        c.removeFromTop (6);
        positionBox.setBounds (c.removeFromTop (30));
        const auto itemRow = rotLeftButton.getBounds().getUnion (rotRightButton.getBounds());
        renameButton.setBounds (itemRow.withWidth ((itemRow.getWidth() - 8) / 2));
        deleteButton.setBounds (itemRow.withTrimmedLeft ((itemRow.getWidth() + 8) / 2));

        auto bottom = side.reduced (14, 14).removeFromBottom (30);
        reportButton.setBounds (bottom);
        exportButton.setBounds (bottom.translated (0, -38));
        resetButton.setBounds (bottom.translated (0, -76));
    }

private:
    void timerCallback() override
    {
        // speakers or mics added elsewhere
        const int sig = canvas.numSpeakers() * 16 + canvas.numMics();
        if (sig != lastSig) { lastSig = sig; refresh(); }
        else if (isVisible()) canvas.repaint();
    }

    void changed()
    {
        proc.plan.edited = true;
        refreshSelection();
        canvas.repaint();
        if (onChanged) onChanged();
    }

    void askName (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
    {
        if (askText) askText (title, initial, std::move (done));
    }

    void addItem (int kind, const juce::String& text)
    {
        PlanItem it;
        it.kind = kind;
        it.text = text;
        it.x = 0.5f; it.y = 0.45f;
        if (kind == PlanItem::Area) { it.w = 0.25f; it.h = 0.14f; }
        proc.plan.items.push_back (it);
        canvas.select (PlanCanvas::Sel::Item, (int) proc.plan.items.size() - 1);
        changed();
    }

    void renameItem (int i)
    {
        if (i < 0 || i >= (int) proc.plan.items.size()) return;
        askName ("Rename", proc.plan.items[(size_t) i].text, [this, i] (const juce::String& t)
        {
            if (i < (int) proc.plan.items.size()) { proc.plan.items[(size_t) i].text = t; changed(); }
        });
    }

    void turn (float deg)
    {
        const auto t = canvas.selType();
        if (t != PlanCanvas::Sel::Speaker && t != PlanCanvas::Sel::Mic) return;
        canvas.symbol (t, canvas.selIndex()).angle += deg;
        changed();
    }

    void refreshSelection()
    {
        const auto t = canvas.selType();
        const int i = canvas.selIndex();
        const bool sym = t == PlanCanvas::Sel::Speaker || t == PlanCanvas::Sel::Mic;
        const bool item = t == PlanCanvas::Sel::Item && i >= 0 && i < (int) proc.plan.items.size();
        rotLeftButton.setVisible (sym);
        rotRightButton.setVisible (sym);
        aimBox.setVisible (t == PlanCanvas::Sel::Mic);
        positionBox.setVisible (t == PlanCanvas::Sel::Mic);
        if (t == PlanCanvas::Sel::Mic && ! positionBox.hasKeyboardFocus (true))
        {
            positionBox.clear (juce::dontSendNotification);
            for (int k = 0; k < proc.positions.size(); ++k) positionBox.addItem (proc.positions[k], k + 1);
            positionBox.setText (proc.mics.position[(size_t) i], juce::dontSendNotification);
        }
        renameButton.setVisible (item);
        deleteButton.setVisible (item);
        if (t == PlanCanvas::Sel::Mic) aimBox.setSelectedItemIndex (proc.ownAim (i), juce::dontSendNotification);

        if (t == PlanCanvas::Sel::Speaker && i < (int) proc.system.sources.size())
        {
            const auto& s = proc.system.sources[(size_t) i];
            selectionText = (s.name.isNotEmpty() ? s.name : "Speaker " + juce::String (i + 1)) + "\n" + speakerRoleNames()[s.role] + ", "
                            + speakerChannelNames()[s.channel];
        }
        else if (t == PlanCanvas::Sel::Mic) selectionText = proc.mics.nameOf (i) + (proc.mics.position[(size_t) i].isNotEmpty() ? "\n" + proc.mics.position[(size_t) i] : juce::String());
        else if (item)
        {
            static const char* kinds[] = { "Area", "Mic position", "Note" };
            selectionText = juce::String (kinds[proc.plan.items[(size_t) i].kind]) + "\n" + proc.plan.items[(size_t) i].text;
        }
        else selectionText = "Click something on the plan.";
        repaint (selArea);
    }

    LiveAlignProcessor& proc;
    PlanCanvas canvas;
    juce::TextButton addAreaButton { "Area..." }, addMarkerButton { "Mic position..." }, addNoteButton { "Note..." };
    juce::TextButton reportButton { "Tuning report (PDF)..." }, exportButton { "Save as picture..." }, resetButton { "Place missing speakers and mics" };
    juce::TextButton renameButton { "Rename" }, deleteButton { "Remove" };
    juce::TextButton rotLeftButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x9f\xb2  Turn left")) }, rotRightButton { juce::String (juce::CharPointer_UTF8 ("Turn right  \xe2\x9f\xb3")) };
    juce::ComboBox aimBox, positionBox;
    juce::Rectangle<int> side, helpArea, addLabel, selLabel, selArea;
    juce::String selectionText;
    int lastSig = -1;
};
} // namespace la::ui
