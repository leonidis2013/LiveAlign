// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Venue plan: a simple sketch of the setup seen from above (not to scale). The speakers come from the Speakers tab,
// tagged and pointing the way they face; areas, measurement points and notes are added here. Each point is for
// tuning, monitoring or both, and shows the mic linked to it (the mics themselves are not drawn: a mic is wherever
// its point is). Drag to move, drag the small handle to turn a speaker, double-click to rename. Saved with the venue
// and exported as a picture.
#include <functional>
#include <iterator>
#include <vector>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class PlanCanvas : public juce::Component
{
public:
    enum class Sel { None, Speaker, Mic, Item, MonitorMic };
    std::function<void()> onSelectionChanged, onChanged;
    std::function<void (int item)> onEditItem;     // double-click on an area, marker or note

    explicit PlanCanvas (LiveAlignProcessor& p) : proc (p), plan (p.plan) { setWantsKeyboardFocus (true); }

    Sel selType() const { return selType_; }
    int selIndex() const { return selIndex_; }
    void select (Sel t, int i) { selType_ = t; selIndex_ = i; repaint(); if (onSelectionChanged) onSelectionChanged(); }

    int numSpeakers() const { return juce::jmin ((int) proc.system.sources.size(), VenuePlan::kMaxSpeakers); }

    // Guided measuring: the plan shows only where the mics go (the point now pulsing, with the mic and its aim), what
    // is done and what is still to do. The point now can be dragged to where it really is. Nothing else is editable.
    struct Guide
    {
        bool active = false;
        std::vector<GuideSpot> spots;
        float pulse = 0.0f;   // 0..1, animated by the owner
    };
    Guide guide;
    int numMics() const { return 0; }   // mics are shown on their points, not as symbols

    // Is this point shown on the layer (tuning points, or monitoring points)?
    bool onLayer (const juce::String& name) const
    {
        const int u = proc.pointUse (name);
        return u == LiveAlignProcessor::UseBoth || (layer == 0 ? u == LiveAlignProcessor::UseTuning : u == LiveAlignProcessor::UseMonitoring);
    }
    // What is at a point, for its label: the tuning mic (tuning layer) or the monitoring mic (monitoring layer).
    juce::String micLabelAt (const juce::String& name, int& micIndex) const
    {
        micIndex = -1;
        if (layer == 1) return proc.monitorMic.position == name ? proc.monitorMic.name : juce::String();
        micIndex = proc.micAtPoint (name);
        return micIndex >= 0 ? proc.mics.nameOf (micIndex) : juce::String();
    }

    // The plan has two layers over the same speakers and room: the tuning mics, or the monitoring mic.
    void setLayer (int l) { layer = juce::jlimit (0, 1, l); if (selType_ == Sel::Mic || selType_ == Sel::MonitorMic) select (Sel::None, -1); repaint(); }
    int getLayer() const { return layer; }
    // How many points are on the layer shown.
    int pointsOnLayer() const
    {
        int n = 0;
        for (const auto& it : plan.items) if (it.kind == PlanItem::Marker && onLayer (it.text)) ++n;
        return n;
    }

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
        tagBoxes.clear();   // the callouts drawn so far (they step aside from each other)
        tagPaper = forExport ? juce::Colours::white : p.panel;

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
            if (it.seating != PlanItem::Plain) drawChairs (g, r.withTrimmedTop (20.0f).reduced (8.0f, 4.0f), it.seating, inkDim);
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
            if (guide.active && it.kind == PlanItem::Marker
                && std::any_of (guide.spots.begin(), guide.spots.end(), [i] (const GuideSpot& sp) { return sp.marker == i; }))
                continue;   // drawn as a guided point
            if (it.kind == PlanItem::Marker)
            {
                // a measurement point, with the mic linked to it
                if (! guide.active && ! onLayer (it.text)) continue;
                int mi = -1;
                const auto mic = guide.active ? juce::String() : micLabelAt (it.text, mi);
                const auto micCol = layer == 1 ? (forExport ? juce::Colour (0xffc2410c) : juce::Colour (0xfff97316))
                                               : (forExport ? micColour (juce::jmax (0, mi)).darker (0.25f) : micColour (juce::jmax (0, mi)));
                if (mic.isNotEmpty())
                {
                    g.setColour (micCol.withAlpha (0.22f));
                    g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c));
                    g.setColour (micCol);
                    g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c), 1.6f);
                }
                juce::Path d;
                d.addPolygon (c, 4, 7.5f, 0.0f);
                g.setColour (sel ? p.accent : mic.isNotEmpty() ? micCol : inkDim);
                g.strokePath (d, juce::PathStrokeType (1.5f));
                auto lr = juce::Rectangle<float> (c.x + (mic.isNotEmpty() ? 15.0f : 12.0f), c.y - 9.0f, 220.0f, 18.0f);
                g.setFont (font (11.5f, mic.isNotEmpty()));
                g.setColour (sel ? p.accent : ink);
                const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), it.text);
                g.drawText (it.text, lr, juce::Justification::centredLeft, true);
                if (! guide.active)
                {
                    g.setFont (font (11.0f, mic.isNotEmpty()));
                    g.setColour (mic.isNotEmpty() ? micCol : inkDim.withAlpha (0.7f));
                    g.drawText (juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + (mic.isNotEmpty() ? mic : juce::String ("no mic")),
                                lr.withTrimmedLeft (w), juce::Justification::centredLeft, true);
                }
            }
            else
            {
                g.setColour (sel ? p.accent : ink);
                g.setFont (font (12.0f, false).italicised());
                g.drawFittedText (it.text, noteRect (it).toNearestInt(), juce::Justification::centred, 3);
            }
        }

        for (int i = 0; i < numSpeakers(); ++i) drawSpeaker (g, i, forExport, ink);
        if (guide.active) drawGuide (g);

        if (! forExport && ! guide.active)
        {
            // while dragging: the line of the point or speaker it is aligned with
            if (drag == Drag::Move && (alignX >= 0.0f || alignY >= 0.0f))
            {
                juce::Path l, d;
                if (alignX >= 0.0f) { const float x = toPx (alignX, 0.0f).x; l.startNewSubPath (x, draw.getY()); l.lineTo (x, draw.getBottom()); }
                if (alignY >= 0.0f) { const float y = toPx (0.0f, alignY).y; l.startNewSubPath (draw.getX(), y); l.lineTo (draw.getRight(), y); }
                const float dashes[] = { 5.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (d, l, dashes, 2);
                g.setColour (p.accent.withAlpha (0.7f));
                g.fillPath (d);
            }
            if (pointsOnLayer() == 0)
            {
                g.setColour (inkDim.withAlpha (0.75f));
                g.setFont (font (13.0f));
                g.drawFittedText ("No measurement points yet. Add them one by one, or Load a layout...",
                                  draw.withSizeKeepingCentre (juce::jmin (draw.getWidth() - 20.0f, 420.0f), 40.0f).toNearestInt(),
                                  juce::Justification::centred, 2);
            }
        }

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
        g.drawText (juce::String (layer == 0 ? "Venue plan, tuning points" : "Venue plan, monitoring points") + " (not to scale)   " + LiveAlignProcessor::today(),
                    title, juce::Justification::topRight, true);
        if (forExport)
        {
            g.drawText (legendText(), title.withTrimmedTop (22.0f), juce::Justification::topLeft, true);
        }
    }

    void paint (juce::Graphics& g) override { render (g, sheet(), false); }

    // `forLayer`: 0 tuning mics, 1 monitoring mic, -1 the layer shown.
    juce::Image exportImage (int width = 2000, int forLayer = -1) const
    {
        const int keepLayer = layer;
        if (forLayer >= 0) layer = forLayer;
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
        layer = keepLayer;
        return img;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (guide.active)
        {
            guideDrag = -1;
            for (int k = 0; k < (int) guide.spots.size(); ++k)
            {
                const auto& sp = guide.spots[(size_t) k];
                if (sp.state == GuideSpot::Now && sp.marker >= 0 && toPx (sp.x, sp.y).getDistanceFrom (e.position) < 20.0f) guideDrag = k;
            }
            return;
        }
        grabKeyboardFocus();
        const auto pt = e.position;
        drag = Drag::None;
        // handles of the selection first
        if (selType_ == Sel::Item && selIndex_ < (int) plan.items.size() && plan.items[(size_t) selIndex_].kind == PlanItem::Area
            && resizeHandle (plan.items[(size_t) selIndex_]).expanded (4.0f).contains (pt))
        { drag = Drag::Resize; return; }
        if ((selType_ == Sel::Speaker || selType_ == Sel::Mic || selType_ == Sel::MonitorMic) && rotateHandle (selType_, selIndex_).getDistanceFrom (pt) < 9.0f)
        { drag = Drag::Rotate; return; }

        Sel t = Sel::None;
        int idx = -1;
        for (int i = numSpeakers(); --i >= 0 && t == Sel::None;) if (toPx (plan.speakers[(size_t) i].x, plan.speakers[(size_t) i].y).getDistanceFrom (pt) < 20.0f) { t = Sel::Speaker; idx = i; }
        for (int i = (int) plan.items.size(); --i >= 0 && t == Sel::None;)
        {
            const auto& it = plan.items[(size_t) i];
            if (it.kind == PlanItem::Marker && ! onLayer (it.text)) continue;
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
                if (it.kind == PlanItem::Marker && onLayer (it.text) && toPx (it.x, it.y).getDistanceFrom (pt) < 12.0f) { idx = i; break; }
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
        if (guide.active)
        {
            if (guideDrag < 0 || guideDrag >= (int) guide.spots.size()) return;
            auto& sp = guide.spots[(size_t) guideDrag];
            const auto n = toNorm (e.position);
            sp.x = n.x;
            sp.y = n.y;
            if (sp.marker >= 0 && sp.marker < (int) plan.items.size()) { plan.items[(size_t) sp.marker].x = n.x; plan.items[(size_t) sp.marker].y = n.y; }
            repaint();
            return;
        }
        if (drag == Drag::None) return;
        const auto pt = e.position;
        const bool snap = ! (e.mods.isAltDown() || e.mods.isCommandDown());   // Alt or Cmd: free
        alignX = alignY = -1.0f;
        if (drag == Drag::Move)
        {
            auto n = toNorm ({ pt.x - grabOffset.x, pt.y - grabOffset.y });
            const bool isArea = selType_ == Sel::Item && plan.items[(size_t) selIndex_].kind == PlanItem::Area;
            if (snap && isArea)
            {
                // an area: its top-left corner on the grid
                const auto& it = plan.items[(size_t) selIndex_];
                n = { juce::jlimit (0.0f, 1.0f, snapToGrid (n.x - it.w * 0.5f) + it.w * 0.5f),
                      juce::jlimit (0.0f, 1.0f, snapToGrid (n.y - it.h * 0.5f) + it.h * 0.5f) };
            }
            else if (snap) n = snapPoint (n);
            if (selType_ == Sel::Speaker) { auto& s = plan.speakers[(size_t) selIndex_]; s.x = n.x; s.y = n.y; }
            else if (selType_ == Sel::Mic) { auto& s = plan.mics[(size_t) selIndex_]; s.x = n.x; s.y = n.y; }
            else if (selType_ == Sel::MonitorMic) { plan.monitorMic.x = n.x; plan.monitorMic.y = n.y; }
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
            auto br = toNorm (pt);
            if (snap) br = { snapToGrid (br.x), snapToGrid (br.y) };
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
        if (guide.active)
        {
            if (guideDrag >= 0)
            {
                plan.edited = true;
                if (onChanged) onChanged();
            }
            guideDrag = -1;
            return;
        }
        if (drag != Drag::None && onChanged) onChanged();
        drag = Drag::None;
        if (alignX >= 0.0f || alignY >= 0.0f) { alignX = alignY = -1.0f; repaint(); }
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (guide.active) return;
        if (selType_ == Sel::Item && onEditItem) onEditItem (selIndex_);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selType_ == Sel::Item)
        {
            removeSelectedItem();
            return true;
        }
        if ((k.getTextCharacter() == '[' || k.getTextCharacter() == ']') && (selType_ == Sel::Speaker || selType_ == Sel::Mic || selType_ == Sel::MonitorMic))
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
        if (plan.items[(size_t) selIndex_].kind == PlanItem::Marker)
            proc.removePosition (plan.items[(size_t) selIndex_].text);   // a marker is a position: it goes everywhere (and its measurements)
        else plan.items.erase (plan.items.begin() + selIndex_);
        plan.edited = true;
        select (Sel::None, -1);
        if (onChanged) onChanged();
    }

    PlanSymbol& symbol (Sel t, int i) { return t == Sel::Speaker ? plan.speakers[(size_t) i] : t == Sel::MonitorMic ? plan.monitorMic : plan.mics[(size_t) i]; }

    juce::String legendText() const
    {
        return juce::String (juce::CharPointer_UTF8 ("Speakers: arrow = direction it faces.   \xe2\x97\x87 = measurement point, ringed in the colour of the mic "
                                                    "linked to it."));
    }

private:
    enum class Drag { None, Move, Rotate, Resize };
    mutable std::vector<juce::Rectangle<float>> tagBoxes;
    mutable juce::Colour tagPaper;

    juce::Point<float> toPx (float x, float y) const { return { drawArea.getX() + x * drawArea.getWidth(), drawArea.getY() + y * drawArea.getHeight() }; }
    juce::Point<float> toNorm (juce::Point<float> p) const
    {
        return { juce::jlimit (0.0f, 1.0f, (p.x - drawArea.getX()) / juce::jmax (1.0f, drawArea.getWidth())),
                 juce::jlimit (0.0f, 1.0f, (p.y - drawArea.getY()) / juce::jmax (1.0f, drawArea.getHeight())) };
    }

    // Snapping while dragging: to a grid of 1/40 of the drawing, and to the line of another point or speaker when
    // close to it (shown by a dashed guide).
    static constexpr float kGrid = 1.0f / 40.0f, kAlign = 0.012f;
    static float snapToGrid (float v) { return juce::jlimit (0.0f, 1.0f, kGrid * std::round (v / kGrid)); }

    juce::Point<float> snapPoint (juce::Point<float> n) const
    {
        float bestX = kAlign, bestY = kAlign, sx = -1.0f, sy = -1.0f;
        auto consider = [&] (float x, float y)
        {
            if (std::abs (x - n.x) < bestX) { bestX = std::abs (x - n.x); sx = x; }
            if (std::abs (y - n.y) < bestY) { bestY = std::abs (y - n.y); sy = y; }
        };
        for (int i = 0; i < (int) plan.items.size(); ++i)
        {
            const auto& it = plan.items[(size_t) i];
            if (it.kind != PlanItem::Marker || ! onLayer (it.text) || (selType_ == Sel::Item && selIndex_ == i)) continue;
            consider (it.x, it.y);
        }
        for (int i = 0; i < numSpeakers(); ++i)
        {
            const auto& s = plan.speakers[(size_t) i];
            if (s.placed && ! (selType_ == Sel::Speaker && selIndex_ == i)) consider (s.x, s.y);
        }
        alignX = sx;
        alignY = sy;
        return { sx >= 0.0f ? sx : snapToGrid (n.x), sy >= 0.0f ? sy : snapToGrid (n.y) };
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
        if (selType_ == Sel::MonitorMic) return toPx (plan.monitorMic.x, plan.monitorMic.y);
        if (selType_ == Sel::Item) return toPx (plan.items[(size_t) selIndex_].x, plan.items[(size_t) selIndex_].y);
        return {};
    }

    juce::Point<float> rotateHandle (Sel t, int i) const
    {
        const auto& s = t == Sel::Speaker ? plan.speakers[(size_t) i] : t == Sel::MonitorMic ? plan.monitorMic : plan.mics[(size_t) i];
        const float r = t == Sel::Speaker ? 34.0f : 28.0f;
        const float a = juce::degreesToRadians (s.angle);
        return toPx (s.x, s.y) + juce::Point<float> (std::cos (a) * r, std::sin (a) * r);
    }

    // A callout: a dot on the symbol, a leader line out (diagonally, then level) to a box with the name and the
    // details. It goes toward the nearer side of the drawing and up or down by the half it is in, and steps aside
    // from the callouts already drawn so they never overlap.
    void drawTag (juce::Graphics& g, juce::Point<float> c, const juce::String& line1, const juce::String& line2, juce::Colour col, juce::Colour ink, float) const
    {
        const auto f1 = font (11.5f, true), f2 = font (10.5f);
        const float tw = juce::jmax (juce::GlyphArrangement::getStringWidth (f1, line1), line2.isNotEmpty() ? juce::GlyphArrangement::getStringWidth (f2, line2) : 0.0f);
        const float w = juce::jlimit (60.0f, 230.0f, tw + 18.0f), h = line2.isNotEmpty() ? 34.0f : 20.0f;
        const bool right = c.x >= drawArea.getCentreX();       // out toward the nearer edge
        const bool up = c.y < drawArea.getCentreY();
        const float sx = right ? 1.0f : -1.0f;
        float dy = up ? -26.0f : 26.0f;
        juce::Rectangle<float> box;
        juce::Point<float> elbow, end;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            elbow = c + juce::Point<float> (sx * 30.0f, dy);
            end = elbow + juce::Point<float> (sx * 14.0f, 0.0f);
            box = juce::Rectangle<float> (w, h).withPosition (right ? end.x : end.x - w, end.y - h * 0.5f).constrainedWithin (drawArea.reduced (2.0f));
            bool clash = false;
            for (const auto& o : tagBoxes) clash = clash || o.intersects (box.expanded (3.0f));
            if (! clash) break;
            dy += (up ? -1.0f : 1.0f) * (h + 6.0f);   // further out, away from the one in the way
        }
        tagBoxes.push_back (box);
        // the leader: from the symbol's edge to the box's near side, at the box's middle
        const float edgeX = right ? box.getX() : box.getRight();
        end = { edgeX, box.getCentreY() };
        elbow = { end.x - sx * 14.0f, end.y };
        const auto dir = (elbow - c);
        const float len = std::max (1.0f, dir.getDistanceFromOrigin());
        const auto start = c + dir * (16.0f / len);
        juce::Path leader;
        leader.startNewSubPath (start);
        leader.lineTo (elbow);
        leader.lineTo (end);
        g.setColour (col.withAlpha (0.8f));
        g.strokePath (leader, juce::PathStrokeType (1.2f));
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (start), 1.4f);
        // the box
        g.setColour (tagPaper.withAlpha (0.94f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (col.withAlpha (0.7f));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        g.fillRect (right ? box.getX() : box.getRight() - 3.0f, box.getY(), 3.0f, box.getHeight());   // the accent bar on the leader's side
        auto r = box.reduced (9.0f, 3.0f);
        g.setColour (col);
        g.setFont (f1);
        g.drawText (line1, r.removeFromTop (line2.isNotEmpty() ? 15.0f : r.getHeight()), juce::Justification::centredLeft, true);
        if (line2.isNotEmpty())
        {
            g.setColour (ink.withAlpha (0.75f));
            g.setFont (f2);
            g.drawText (line2, r, juce::Justification::centredLeft, true);
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

    void drawMonitorMic (juce::Graphics& g, bool forExport, juce::Colour ink) const
    {
        const auto& s = plan.monitorMic;
        if (! s.placed) return;
        const auto& mm = proc.monitorMic;
        const auto col = forExport ? juce::Colour (0xffc2410c) : juce::Colour (0xfff97316);
        const auto c = toPx (s.x, s.y);
        const bool up = mm.aim == Cal90 || mm.aim == Cal180;
        g.setColour (col.withAlpha (0.2f));
        g.fillEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (c));
        g.setColour (col);
        g.drawEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (c), 2.0f);
        if (up) g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
        else
        {
            juce::Path arrow;
            arrow.startNewSubPath (12.0f, 0.0f);
            arrow.lineTo (23.0f, 0.0f);
            arrow.startNewSubPath (18.0f, -4.0f);
            arrow.lineTo (23.0f, 0.0f);
            arrow.lineTo (18.0f, 4.0f);
            arrow.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (s.angle)).translated (c));
            g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setFont (font (9.5f, true));
        g.drawText ("M", juce::Rectangle<float> (14.0f, 12.0f).withCentre (c.translated (0.0f, up ? -18.0f : 0.0f)), juce::Justification::centred);
        juce::String l1 = mm.name + (mm.position.isNotEmpty() ? "  -  " + mm.position : juce::String());
        juce::String l2 = "Monitoring  -  In " + juce::String (mm.input + 1) + "  -  " + (up ? "pointing up" : "at the speaker");
        if (const auto* e = proc.locker.find (mm.lockerId)) if (e->modelSerial().isNotEmpty()) l2 << "  -  " << e->modelSerial();
        drawTag (g, c, l1, l2, col, ink, up ? -90.0f : s.angle);
        if (! forExport && selType_ == Sel::MonitorMic) drawSelection (g, c, 18.0f, up ? juce::Point<float>() : rotateHandle (Sel::MonitorMic, 0));
    }

    // Chairs seen from above, to show where in the seating a mic is.
    static void drawChairs (juce::Graphics& g, juce::Rectangle<float> r, int style, juce::Colour ink)
    {
        if (r.getWidth() < 30.0f || r.getHeight() < 16.0f) return;
        g.setColour (ink.withAlpha (0.2f));
        const float cw = 8.0f, ch = 6.0f, pitchX = 13.0f, pitchY = 18.0f;
        auto chair = [&] (float x, float y, float angle)
        {
            juce::Path p;
            p.addRoundedRectangle (-cw * 0.5f, -ch * 0.5f, cw, ch, 1.5f);
            p.applyTransform (juce::AffineTransform::rotation (angle).translated (x, y));
            g.fillPath (p);
        };
        if (style == PlanItem::Curved)
        {
            // rows on arcs around a point above the area (the stage), widest at the back
            const float cx = r.getCentreX(), cy = r.getY() - r.getHeight() * 0.6f;
            for (float rad = (r.getY() - cy) + 6.0f; rad < (r.getBottom() - cy); rad += pitchY)
            {
                const float span = std::asin (juce::jlimit (0.0f, 1.0f, (r.getWidth() * 0.5f - 4.0f) / rad));
                const float aisle = 12.0f / rad;   // a centre aisle
                for (float a = -span; a <= span; a += pitchX / rad)
                {
                    if (std::abs (a) < aisle) continue;
                    const float x = cx + rad * std::sin (a), y = cy + rad * std::cos (a);
                    if (r.contains (x, y)) chair (x, y, -a);
                }
            }
            return;
        }
        if (style == PlanItem::Blocks)
        {
            // four square blocks with a cross aisle
            const float aisleW = 18.0f;
            const auto top = r.removeFromTop ((r.getHeight() - aisleW) * 0.5f);
            r.removeFromTop (aisleW);
            for (auto half : { top, r })
            {
                auto left = half.removeFromLeft ((half.getWidth() - aisleW) * 0.5f);
                half.removeFromLeft (aisleW);
                for (auto block : { left, half })
                    for (float y = block.getY() + ch; y <= block.getBottom() - ch * 0.5f; y += pitchY)
                        for (float x = block.getX() + cw; x <= block.getRight() - cw * 0.5f; x += pitchX)
                            chair (x, y, 0.0f);
            }
            return;
        }
        // rows with a centre aisle
        const float aisleW = 18.0f;
        auto left = r.removeFromLeft ((r.getWidth() - aisleW) * 0.5f);
        r.removeFromLeft (aisleW);
        for (auto side : { left, r })
            for (float y = side.getY() + ch; y <= side.getBottom() - ch * 0.5f; y += pitchY)
                for (float x = side.getX() + cw; x <= side.getRight() - cw * 0.5f; x += pitchX)
                    chair (x, y, 0.0f);
    }

    // The guided points: to do (hollow), done (ticked), and the one(s) now: pulsing, with the mic and its aim.
    void drawGuide (juce::Graphics& g) const
    {
        const auto& p = pal();
        auto tick = [&g] (juce::Point<float> c, float s, juce::Colour col)
        {
            juce::Path t;
            t.startNewSubPath (c.x - s * 0.45f, c.y + s * 0.02f);
            t.lineTo (c.x - s * 0.12f, c.y + s * 0.32f);
            t.lineTo (c.x + s * 0.48f, c.y - s * 0.32f);
            g.setColour (col);
            g.strokePath (t, juce::PathStrokeType (s * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        // the others first, then the ones now on top
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& sp : guide.spots)
            {
                const bool now = sp.state == GuideSpot::Now || sp.state == GuideSpot::JustDone;
                if (now != (pass == 1)) continue;
                const auto c = toPx (sp.x, sp.y);
                if (sp.speaker >= 0 && sp.speaker < numSpeakers() && plan.speakers[(size_t) sp.speaker].placed && sp.state == GuideSpot::Now)
                {
                    // close up: a ring on the speaker, a dashed line from the mic to it
                    const auto sc = toPx (plan.speakers[(size_t) sp.speaker].x, plan.speakers[(size_t) sp.speaker].y);
                    g.setColour (p.accent.withAlpha (0.9f));
                    g.drawEllipse (juce::Rectangle<float> (52.0f, 52.0f).withCentre (sc), 2.0f);
                    juce::Path l, d;
                    l.startNewSubPath (c);
                    l.lineTo (sc);
                    const float dashes[] = { 4.0f, 4.0f };
                    juce::PathStrokeType (1.4f).createDashedStroke (d, l, dashes, 2);
                    g.fillPath (d);
                }
                if (sp.state == GuideSpot::ToDo)
                {
                    juce::Path dm;
                    dm.addPolygon (c, 4, 8.0f, 0.0f);
                    g.setColour (p.textDim);
                    g.strokePath (dm, juce::PathStrokeType (1.5f));
                    g.setFont (font (11.5f));
                    g.drawText (sp.label, juce::Rectangle<float> (c.x + 12.0f, c.y - 9.0f, 160.0f, 18.0f), juce::Justification::centredLeft, true);
                    continue;
                }
                if (sp.state == GuideSpot::Done)
                {
                    g.setColour ((sp.problem ? p.bad : p.good).withAlpha (0.85f));
                    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (c));
                    if (sp.problem)
                    {
                        g.setColour (p.onAccent);
                        g.setFont (font (12.0f, true));
                        g.drawText ("!", juce::Rectangle<float> (18.0f, 18.0f).withCentre (c), juce::Justification::centred);
                    }
                    else tick (c, 12.0f, p.onAccent);
                    g.setColour (p.textDim);
                    g.setFont (font (11.5f));
                    g.drawText (sp.label, juce::Rectangle<float> (c.x + 13.0f, c.y - 9.0f, 160.0f, 18.0f), juce::Justification::centredLeft, true);
                    continue;
                }
                const bool justDone = sp.state == GuideSpot::JustDone;
                const auto doneCol = sp.problem ? p.bad : p.good;
                const auto col = justDone ? doneCol : p.accent;
                if (! justDone)
                    for (int k = 0; k < 2; ++k)
                    {
                        const float ph = std::fmod (guide.pulse + 0.5f * (float) k, 1.0f);
                        g.setColour (col.withAlpha (0.55f * (1.0f - ph)));
                        g.drawEllipse (juce::Rectangle<float> (30.0f + 46.0f * ph, 30.0f + 46.0f * ph).withCentre (c), 2.0f);
                    }
                const auto mc = sp.mic >= 0 ? micColour (sp.mic) : col;
                g.setColour ((justDone ? doneCol : mc).withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (c));
                g.setColour (justDone ? doneCol : mc);
                g.drawEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (c), 2.4f);
                if (justDone && sp.problem)
                {
                    g.setFont (font (18.0f, true));
                    g.drawText ("!", juce::Rectangle<float> (30.0f, 30.0f).withCentre (c), juce::Justification::centred);
                }
                else if (justDone) tick (c, 18.0f, p.good);
                else if (sp.up)
                {
                    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));   // pointing up, out of the page
                }
                else
                {
                    float a = juce::degreesToRadians (sp.angle);
                    if (sp.aimAt >= 0 && sp.aimAt < numSpeakers() && plan.speakers[(size_t) sp.aimAt].placed)
                    {
                        const auto sc = toPx (plan.speakers[(size_t) sp.aimAt].x, plan.speakers[(size_t) sp.aimAt].y);
                        a = std::atan2 (sc.y - c.y, sc.x - c.x);
                    }
                    juce::Path arrow;
                    arrow.startNewSubPath (15.0f, 0.0f);
                    arrow.lineTo (32.0f, 0.0f);
                    arrow.startNewSubPath (25.0f, -6.0f);
                    arrow.lineTo (32.0f, 0.0f);
                    arrow.lineTo (25.0f, 6.0f);
                    arrow.applyTransform (juce::AffineTransform::rotation (a).translated (c));
                    g.strokePath (arrow, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                if (sp.mic >= 0 && ! justDone)
                {
                    g.setFont (monoFont (11.5f, true));
                    g.drawText (juce::String (sp.mic + 1), juce::Rectangle<float> (16.0f, 14.0f).withCentre (sp.up ? c.translated (0.0f, -9.0f) : c),
                                juce::Justification::centred);
                }
                // the label in a chip under the point
                const juce::String txt = (sp.mic >= 0 ? proc.mics.nameOf (sp.mic) + "  -  " : juce::String()) + sp.label;
                g.setFont (font (12.5f, true));
                const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 20.0f;
                auto chip = juce::Rectangle<float> (w, 22.0f).withCentre (c.translated (0.0f, 30.0f)).constrainedWithin (drawArea);
                g.setColour (col);
                g.fillRoundedRectangle (chip, 11.0f);
                g.setColour (p.onAccent);
                g.drawText (txt, chip, juce::Justification::centred, false);
            }
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
    mutable int layer = 0;
    int guideDrag = -1;
    mutable float alignX = -1.0f, alignY = -1.0f;   // while dragging: the line it is aligned to (-1: none)
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
        setup (addMarkerButton, "Add a measurement point: a seat or spot where the mic goes (FOH, Row 8, Balcony ...). Room EQ, Room acoustics and Level balance measure at these points.");
        setup (addNoteButton, "Add a note on the drawing");
        setup (exportButton, "Save the plan as a picture (PNG) to print or share");
        setup (renameButton, "Rename the selected area, position or note (or double-click it)");
        setup (deleteButton, "Remove the selected area, position or note (Delete key)");
        setup (rotLeftButton, "Turn 15 degrees (key: [ )");
        setup (rotRightButton, "Turn 15 degrees (key: ] )");
        setup (resetButton, "Put the speakers that are not on the drawing yet along the front of the stage");
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
            juce::Component::SafePointer<PlanPage> safe (this);
            auto askNew = [safe]
            {
                if (safe == nullptr) return;
                safe->askName (safe->canvas.getLayer() == 0 ? "New tuning point" : "New monitoring point", safe->canvas.getLayer() == 0 ? "Row 5" : "Stage left",
                               [safe] (const juce::String& t) { if (safe != nullptr && t.trim().isNotEmpty()) safe->addItem (PlanItem::Marker, t.trim()); });
            };
            juce::PopupMenu m;
            m.addItem (999, "New point...");
            m.addSeparator();
            // points already in the venue that are not on this layer yet (picking one makes it both)
            int offered = 0;
            const auto all = allPoints();
            for (int i = 0; i < all.size(); ++i)
                if (! canvas.onLayer (all[i])) { m.addItem (i + 1, all[i] + (canvas.getLayer() == 0 ? "  (also for tuning)" : "  (also for monitoring)")); ++offered; }
            for (int a = 0; a < (int) proc.system.alignments.size(); ++a)
            {
                const auto& sp = proc.system.alignments[(size_t) a].spot;
                if (sp.isNotEmpty() && ! all.contains (sp)) { m.addItem (500 + a, sp + "  (alignment spot)"); ++offered; }
            }
            if (offered == 0) { askNew(); return; }   // nothing else to pick: straight to the name
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addMarkerButton), [safe, askNew] (int r)
            {
                if (safe == nullptr || r == 0) return;
                auto& pr = safe->proc;
                if (r == 999) askNew();
                else if (r >= 500 && r - 500 < (int) pr.system.alignments.size()) safe->addItem (PlanItem::Marker, pr.system.alignments[(size_t) (r - 500)].spot);
                else if (const auto all = safe->allPoints(); r - 1 < all.size())
                {
                    const auto name = all[r - 1];
                    pr.setPointUse (name, LiveAlignProcessor::UseBoth);
                    safe->addItem (PlanItem::Marker, name);
                }
            });
        };
        addNoteButton.onClick = [this] { askName ("New note", "Note", [this] (const juce::String& t) { addItem (PlanItem::Note, t); }); };
        setup (layoutButton, "Put a ready-made set of measurement points on this layer (front, centre, rear, FOH, balcony ...), in place of the ones there");
        layoutButton.onClick = [this]
        {
            juce::PopupMenu m;
            for (int i = 0; i < (int) std::size (kLayoutNames); ++i) m.addItem (i + 1, kLayoutNames[i]);
            juce::Component::SafePointer<PlanPage> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&layoutButton), [safe] (int r)
            {
                if (safe != nullptr && r > 0) safe->confirmLayout (r - 1);
            });
        };
        exportButton.onClick = [this] { if (onExport) onExport(); };
        renameButton.onClick = [this] { if (canvas.selType() == PlanCanvas::Sel::Item) renameItem (canvas.selIndex()); };
        deleteButton.onClick = [this] { canvas.removeSelectedItem(); };
        rotLeftButton.onClick = [this] { turn (-15.0f); };
        rotRightButton.onClick = [this] { turn (15.0f); };
        resetButton.onClick = [this] { proc.plan.placeMissing (canvas.numSpeakers(), 0); changed(); };

        for (int i = 0; i < 2; ++i)
        {
            layerButtons[i].setButtonText (i == 0 ? "Tuning points" : "Monitoring points");
            layerButtons[i].setRadioGroupId (8401);
            layerButtons[i].setClickingTogglesState (true);
            layerButtons[i].onClick = [this, i] { if (layerButtons[i].getToggleState()) { canvas.setLayer (i); refreshSelection(); } };
            layerButtons[i].setTooltip (i == 0 ? "The points measured when tuning (Room EQ, Room acoustics, Level balance), with the mic at each"
                                               : "The points the monitoring mic can stand at during the show, and where it is");
            addAndMakeVisible (layerButtons[i]);
        }
        layerButtons[0].setToggleState (true, juce::dontSendNotification);
        seatingBox.addItemList (PlanItem::seatingNames(), 1);
        seatingBox.setTooltip ("Chairs drawn in this area, to show where a seat or mic is");
        seatingBox.onChange = [this]
        {
            const int i = canvas.selIndex();
            if (canvas.selType() != PlanCanvas::Sel::Item || i < 0 || i >= (int) proc.plan.items.size()) return;
            proc.plan.items[(size_t) i].seating = seatingBox.getSelectedItemIndex();
            changed();
        };
        addChildComponent (seatingBox);

        useBox.addItemList ({ "For tuning", "For monitoring", "For both" }, 1);
        useBox.setTooltip ("What this point is for: tuning (Room EQ, Room acoustics, Level balance measure here), monitoring "
                           "(the monitoring mic can stand here during the show), or both");
        useBox.onChange = [this]
        {
            const auto name = selectedPoint();
            if (name.isEmpty()) return;
            proc.setPointUse (name, useBox.getSelectedItemIndex());
            changed();
        };
        addChildComponent (useBox);
        micBox.setTooltip ("The mic at this point. Picking a mic moves it here (the same as At on the Mics page).");
        micBox.onChange = [this]
        {
            const auto name = selectedPoint();
            if (name.isEmpty()) return;
            const int id = micBox.getSelectedId();
            if (canvas.getLayer() == 1)
            {
                if (id == 1000) proc.monitorMic.position = name;
                else if (proc.monitorMic.position == name) proc.monitorMic.position.clear();
                proc.applyMonitorMic();
            }
            else proc.linkMicToPoint (name, id >= 2 ? id - 2 : -1);
            changed();
        };
        addChildComponent (micBox);
        micLabel.setText ("Mic here", juce::dontSendNotification);
        micLabel.setFont (font (12.0f));
        micLabel.setColour (juce::Label::textColourId, pal().textDim);
        addChildComponent (micLabel);

        refresh();
        startTimerHz (2);
    }

    PlanCanvas& getCanvas() { return canvas; }
    void setLayer (int l) { layerButtons[juce::jlimit (0, 1, l)].setToggleState (true, juce::dontSendNotification); canvas.setLayer (l); refreshSelection(); }
    int layer() const { return canvas.getLayer(); }

    // After a venue is opened, or speakers or mics changed.
    void refresh()
    {
        proc.syncPositionsWithPlan();
        refreshSelection();
        canvas.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, side.toFloat(), "Plan");
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        g.drawFittedText (canvas.getLayer() == 0
                              ? "A sketch of the setup, not to scale. Speakers come from the Speakers tab. Add the points you will measure "
                                "at and pick the mic at each. Drag to move; double-click to rename."
                              : "The points the monitoring mic can stand at during the show. Pick the one it is at; the Monitor mic "
                                "page shows the same. Drag to move; double-click to rename.",
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
        {
            auto lr = c.removeFromTop (28);
            layerButtons[0].setBounds (lr.removeFromLeft ((lr.getWidth() - 6) / 2));
            lr.removeFromLeft (6);
            layerButtons[1].setBounds (lr);
            c.removeFromTop (10);
        }
        helpArea = c.removeFromTop (78);
        c.removeFromTop (8);
        addLabel = c.removeFromTop (18);
        c.removeFromTop (4);
        addAreaButton.setBounds (c.removeFromTop (30));
        c.removeFromTop (6);
        addMarkerButton.setBounds (c.removeFromTop (30));
        c.removeFromTop (6);
        addNoteButton.setBounds (c.removeFromTop (30));
        c.removeFromTop (6);
        layoutButton.setBounds (c.removeFromTop (30));
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
        useBox.setBounds (c.removeFromTop (28));
        c.removeFromTop (8);
        {
            auto row = c.removeFromTop (28);
            micLabel.setBounds (row.removeFromLeft (70));
            micBox.setBounds (row);
        }
        seatingBox.setBounds (useBox.getBounds());
        const auto itemRow = rotLeftButton.getBounds().getUnion (rotRightButton.getBounds());
        renameButton.setBounds (itemRow.withWidth ((itemRow.getWidth() - 8) / 2));
        deleteButton.setBounds (itemRow.withTrimmedLeft ((itemRow.getWidth() + 8) / 2));

        auto bottom = side.reduced (14, 14).removeFromBottom (30);
        reportButton.setVisible (false);   // the reports have their own pages now
        exportButton.setBounds (bottom);
        resetButton.setBounds (bottom.translated (0, -38));
    }

private:
    void timerCallback() override
    {
        // speakers added elsewhere, or points changed on the Mics or Monitor mic pages
        const int sig = canvas.numSpeakers() * 16 + proc.positions.size() * 4096 + proc.monitorPoints.size() * 131072;
        if (sig != lastSig) { lastSig = sig; refresh(); }
        else if (isVisible()) { updateResetButton(); canvas.repaint(); }
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
        if (kind == PlanItem::Marker)
        {
            // a point: one marker each; a new one is for the layer it was added on
            if (! proc.positions.contains (text) && ! proc.monitorPoints.contains (text))
                proc.setPointUse (text, canvas.getLayer() == 0 ? LiveAlignProcessor::UseTuning : LiveAlignProcessor::UseMonitoring);
            if (canvas.getLayer() == 1 && proc.monitorMic.position.isEmpty()) { proc.monitorMic.position = text; proc.applyMonitorMic(); }
            if (const int k = proc.plan.markerOf (text); k >= 0) { canvas.select (PlanCanvas::Sel::Item, k); changed(); return; }
        }
        PlanItem it;
        it.kind = kind;
        it.text = text;
        it.x = 0.5f; it.y = 0.45f;
        if (kind == PlanItem::Area) { it.w = 0.25f; it.h = 0.14f; if (text.containsIgnoreCase ("seat") || text.containsIgnoreCase ("balcony") || text.containsIgnoreCase ("overflow")) it.seating = PlanItem::Rows; }
        proc.plan.items.push_back (it);
        canvas.select (PlanCanvas::Sel::Item, (int) proc.plan.items.size() - 1);
        changed();
    }

    // Ready-made sets of points (Load a layout...), in this order.
    static constexpr const char* kLayoutNames[] = { "Single floor x5", "Single floor x5 + FOH", "Single floor x5 + FOH + balcony x2", "FOH only", "Small room x3" };

    void confirmLayout (int preset)
    {
        const auto& mine = canvas.getLayer() == 0 ? proc.positions : proc.monitorPoints;
        const int n = juce::jmax (mine.size(), canvas.pointsOnLayer());
        if (n == 0) { loadLayout (preset); return; }
        const juce::String what = canvas.getLayer() == 0 ? "tuning" : "monitoring";
        juce::Component::SafePointer<PlanPage> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Load a layout",
                                          "Replace the " + juce::String (n) + " " + what + (n == 1 ? " point" : " points") + " on the plan with \""
                                              + kLayoutNames[preset] + "\"?\n\nMics at those points are unlinked, and what was measured at them is "
                                              "removed. A point also used for " + (canvas.getLayer() == 0 ? "monitoring" : "tuning") + " stays for that.",
                                          "Replace", "Cancel", this),
                                      [safe, preset] (int r)
                                      {
                                          if (safe != nullptr && r == 1) safe->loadLayout (preset);
                                      });
    }

    // Replaces the points of the layer shown with a ready-made set, placed on the seating (or the whole drawing).
    void loadLayout (int preset)
    {
        const int layer = canvas.getLayer();
        canvas.select (PlanCanvas::Sel::None, -1);
        // the layer's points go: a point on both layers stays on the other one, the rest are removed everywhere
        const auto mine = layer == 0 ? proc.positions : proc.monitorPoints;
        for (const auto& name : mine)
        {
            if (proc.pointUse (name) == LiveAlignProcessor::UseBoth) proc.setPointUse (name, layer == 0 ? LiveAlignProcessor::UseMonitoring : LiveAlignProcessor::UseTuning);
            else proc.removePosition (name);
        }
        // where they go: the seating, or the drawing below the stage
        float x0 = 0.12f, x1 = 0.88f, y0 = 0.25f, y1 = 0.85f;
        const bool seating = proc.plan.seatingBounds (x0, x1, y0, y1);
        const float w = x1 - x0, h = y1 - y0;
        struct Spot { juce::String name; float x, y; };
        std::vector<Spot> spots;
        auto five = [&]
        {
            spots.push_back ({ "Front L", x0 + 0.25f * w, y0 + 0.2f * h });
            spots.push_back ({ "Front R", x0 + 0.75f * w, y0 + 0.2f * h });
            spots.push_back ({ "Centre", x0 + 0.5f * w, y0 + 0.5f * h });
            spots.push_back ({ "Rear L", x0 + 0.25f * w, y0 + 0.78f * h });
            spots.push_back ({ "Rear R", x0 + 0.75f * w, y0 + 0.78f * h });
        };
        auto foh = [&]
        {
            if (const auto* a = proc.plan.areaNamed ("FOH")) spots.push_back ({ "FOH", a->x, a->y });
            else spots.push_back ({ "FOH", x0 + 0.5f * w, seating ? juce::jmin (0.95f, y1 + 0.06f) : y0 + 0.92f * h });   // behind the seating
        };
        auto balcony = [&]
        {
            if (const auto* a = proc.plan.areaNamed ("Balcony"))
            {
                spots.push_back ({ "Balcony L", a->x - 0.25f * a->w, a->y });
                spots.push_back ({ "Balcony R", a->x + 0.25f * a->w, a->y });
            }
            else
            {
                const float by = juce::jmin (0.96f, y1 + 0.06f);   // over the back of the room
                spots.push_back ({ "Balcony L", x0 + 0.15f * w, by });
                spots.push_back ({ "Balcony R", x1 - 0.15f * w, by });
            }
        };
        switch (preset)
        {
            case 0: five(); break;
            case 1: five(); foh(); break;
            case 2: five(); foh(); balcony(); break;
            case 3: foh(); break;
            case 4:
                // spread unevenly, so the three do not all sit on the same room modes
                spots.push_back ({ "Front L", x0 + 0.3f * w, y0 + 0.28f * h });
                spots.push_back ({ "Centre", x0 + 0.5f * w, y0 + 0.55f * h });
                spots.push_back ({ "Rear R", x0 + 0.7f * w, y0 + 0.8f * h });
                break;
            default: break;
        }
        for (const auto& s : spots)
        {
            const float x = juce::jlimit (0.02f, 0.98f, s.x), y = juce::jlimit (0.02f, 0.98f, s.y);
            if (proc.positions.contains (s.name) || proc.monitorPoints.contains (s.name))
            {
                proc.setPointUse (s.name, LiveAlignProcessor::UseBoth);   // already on the other layer: the same spot, now for both
                continue;
            }
            if (const int k = proc.plan.markerOf (s.name); k >= 0) { proc.plan.items[(size_t) k].x = x; proc.plan.items[(size_t) k].y = y; }
            else proc.plan.items.push_back ({ PlanItem::Marker, s.name, x, y, 0.0f, 0.0f });
            proc.setPointUse (s.name, layer == 0 ? LiveAlignProcessor::UseTuning : LiveAlignProcessor::UseMonitoring);
        }
        if (layer == 1 && proc.monitorMic.position.isEmpty() && ! spots.empty())
        {
            // the monitoring mic goes to FOH when there is one, else the first point
            auto at = spots.front().name;
            for (const auto& s : spots) if (s.name == "FOH") at = s.name;
            proc.monitorMic.position = at;
            proc.applyMonitorMic();
        }
        proc.syncPositionsWithPlan();
        changed();
    }

    void renameItem (int i)
    {
        if (i < 0 || i >= (int) proc.plan.items.size()) return;
        askName ("Rename", proc.plan.items[(size_t) i].text, [this, i] (const juce::String& t)
        {
            if (i >= (int) proc.plan.items.size() || t.trim().isEmpty()) return;
            auto& it = proc.plan.items[(size_t) i];
            if (it.kind == PlanItem::Marker) proc.renamePosition (it.text, t);   // the position everywhere
            else it.text = t;
            changed();
        });
    }

    void turn (float deg)
    {
        const auto t = canvas.selType();
        if (t != PlanCanvas::Sel::Speaker) return;
        canvas.symbol (t, canvas.selIndex()).angle += deg;
        changed();
    }

    juce::StringArray allPoints() const
    {
        auto all = proc.positions;
        for (const auto& m : proc.monitorPoints) all.addIfNotAlreadyThere (m);
        return all;
    }
    // The selected measurement point's name (empty: no point selected).
    juce::String selectedPoint() const
    {
        const int i = canvas.selIndex();
        if (canvas.selType() != PlanCanvas::Sel::Item || i < 0 || i >= (int) proc.plan.items.size()) return {};
        const auto& it = proc.plan.items[(size_t) i];
        return it.kind == PlanItem::Marker ? it.text : juce::String();
    }
    void updateResetButton()
    {
        bool missing = false;
        for (int k = 0; k < juce::jmin (canvas.numSpeakers(), (int) proc.plan.speakers.size()); ++k) missing = missing || ! proc.plan.speakers[(size_t) k].placed;
        if (resetButton.isVisible() != missing) resetButton.setVisible (missing);
    }

    void refreshSelection()
    {
        const auto t = canvas.selType();
        const int i = canvas.selIndex();
        const bool sym = t == PlanCanvas::Sel::Speaker;
        const bool item = t == PlanCanvas::Sel::Item && i >= 0 && i < (int) proc.plan.items.size();
        rotLeftButton.setVisible (sym);
        rotRightButton.setVisible (sym);
        const bool area = item && proc.plan.items[(size_t) i].kind == PlanItem::Area;
        seatingBox.setVisible (area);
        if (area) seatingBox.setSelectedItemIndex (proc.plan.items[(size_t) i].seating, juce::dontSendNotification);
        const auto point = selectedPoint();
        const bool isPoint = point.isNotEmpty();
        useBox.setVisible (isPoint);
        micBox.setVisible (isPoint);
        micLabel.setVisible (isPoint);
        if (isPoint)
        {
            useBox.setSelectedItemIndex (proc.pointUse (point), juce::dontSendNotification);
            micBox.clear (juce::dontSendNotification);
            micBox.addItem ("No mic", 1);
            if (canvas.getLayer() == 1)
            {
                micBox.addItem (proc.monitorMic.name.isNotEmpty() ? proc.monitorMic.name : juce::String ("Monitoring mic"), 1000);
                micBox.setSelectedId (proc.monitorMic.position == point ? 1000 : 1, juce::dontSendNotification);
            }
            else
            {
                for (int m = 0; m < juce::jmin (proc.settings.micCount.load(), kMaxMics); ++m)
                {
                    const auto& at = proc.mics.position[(size_t) m];
                    micBox.addItem (proc.mics.nameOf (m) + (at.isNotEmpty() && at != point ? "  (now at " + at + ")" : juce::String()), m + 2);
                }
                const int here = proc.micAtPoint (point);
                micBox.setSelectedId (here >= 0 ? here + 2 : 1, juce::dontSendNotification);
            }
        }
        updateResetButton();
        renameButton.setVisible (item);
        deleteButton.setVisible (item);

        if (t == PlanCanvas::Sel::Speaker && i < (int) proc.system.sources.size())
        {
            const auto& s = proc.system.sources[(size_t) i];
            selectionText = (s.name.isNotEmpty() ? s.name : "Speaker " + juce::String (i + 1)) + "\n" + speakerRoleNames()[s.role] + ", "
                            + speakerChannelNames()[s.channel];
        }
        else if (item)
        {
            static const char* kinds[] = { "Area", "Measurement point", "Note" };
            selectionText = juce::String (kinds[proc.plan.items[(size_t) i].kind]) + "\n" + proc.plan.items[(size_t) i].text;
        }
        else selectionText = "Click something on the plan.";
        repaint (selArea);
    }

    LiveAlignProcessor& proc;
    PlanCanvas canvas;
    juce::TextButton addAreaButton { "Area..." }, addMarkerButton { "+ Measurement point..." }, addNoteButton { "Note..." }, layoutButton { "Load a layout..." };
    juce::TextButton reportButton { "Tuning report (PDF)..." }, exportButton { "Save as picture..." }, resetButton { "Place missing speakers" };
    juce::TextButton renameButton { "Rename" }, deleteButton { "Remove" };
    juce::TextButton rotLeftButton { juce::String (juce::CharPointer_UTF8 ("\xe2\x9f\xb2  Turn left")) }, rotRightButton { juce::String (juce::CharPointer_UTF8 ("Turn right  \xe2\x9f\xb3")) };
    juce::ComboBox useBox, micBox, seatingBox;
    juce::Label micLabel;
    juce::TextButton layerButtons[2];
    juce::Rectangle<int> side, helpArea, addLabel, selLabel, selArea;
    juce::String selectionText;
    int lastSig = -1;
};
} // namespace la::ui
