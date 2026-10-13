// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The venue plan: a simple, unscaled sketch of the setup seen from above. Where the speakers and mics are, which way
// they point, areas (stage, FOH, seating ...), named mic positions and notes. Positions are 0..1 across the drawing.
// Saved in the venue file. Message thread only.
#include <array>
#include <cmath>
#include <functional>
#include <vector>
#include <juce_core/juce_core.h>
#include "Settings.h"

namespace la
{
struct PlanSymbol
{
    float x = 0.5f, y = 0.5f;
    float angle = -90.0f;   // degrees, the way it points on the drawing (-90 = up, toward the stage)
    bool placed = false;
};

struct PlanItem
{
    enum Kind { Area = 0, Marker = 1, Note = 2 };
    // An area can show chairs, so a position reads as "row 8, left of the aisle": rows (a rectangle with a centre
    // aisle), square blocks, or curved rows facing the stage.
    enum Seating { Plain = 0, Rows = 1, Blocks = 2, Curved = 3 };
    int kind = Area;
    juce::String text;
    float x = 0.5f, y = 0.5f, w = 0.3f, h = 0.15f;   // areas use w and h; markers and notes only x and y
    int seating = Plain;

    static juce::StringArray seatingNames() { return { "No chairs", "Rows (rectangle)", "Square blocks", "Curved rows" }; }
};

// A point of a guided measurement, as the plan shows it.
struct GuideSpot
{
    enum State { ToDo = 0, Done = 1, Now = 2, JustDone = 3 };
    float x = 0.5f, y = 0.5f;
    juce::String label;      // the position or speaker
    int id = -1;             // what the page knows it by (a speaker index)
    int mic = -1;            // the mic that goes there (-1: none)
    bool up = true;          // pointing up (90 degree cal) or aimed along `angle`
    float angle = -90.0f;
    int state = ToDo;
    int marker = -1;         // the plan item it sits on (it can be dragged into place)
    int speaker = -1;        // a close-up spot: the speaker it is in front of
    int aimAt = -1;          // not up: the speaker it points at (the plan works out the angle)
    juce::String how;        // how to set the mic up there, when the usual words do not fit
};

struct VenuePlan
{
    static constexpr int kMaxSpeakers = 8;
    std::array<PlanSymbol, kMaxSpeakers> speakers {};
    std::array<PlanSymbol, kMaxMics> mics {};
    PlanSymbol monitorMic;   // the monitoring mic (the monitoring layer of the plan)
    std::vector<PlanItem> items;
    bool edited = false;   // changed by hand since it was created

    // A new venue's plan: the stage at the top, seating below, FOH at the back.
    static VenuePlan defaults()
    {
        VenuePlan p;
        p.items.push_back ({ PlanItem::Area, "Stage", 0.5f, 0.12f, 0.6f, 0.16f });
        p.items.push_back ({ PlanItem::Area, "Seating", 0.5f, 0.56f, 0.8f, 0.52f, PlanItem::Rows });
        p.items.push_back ({ PlanItem::Area, "FOH", 0.5f, 0.9f, 0.18f, 0.08f });
        return p;
    }

    bool drawn() const { return edited; }

    // Puts speakers that have no place yet along the front, and mics across the seating, away from what is placed.
    void placeMissing (int numSpeakers, int numMics)
    {
        auto freeSpot = [this] (float y, float x0, float x1)
        {
            float bestX = 0.5f * (x0 + x1), bestD = -1.0f;
            for (int k = 0; k <= 16; ++k)
            {
                const float x = x0 + (x1 - x0) * (float) k / 16.0f;
                float d = 10.0f;
                for (const auto& s : speakers) if (s.placed) d = juce::jmin (d, std::hypot (s.x - x, s.y - y));
                for (const auto& m : mics) if (m.placed) d = juce::jmin (d, std::hypot (m.x - x, m.y - y));
                if (d > bestD + 1e-4f) { bestD = d; bestX = x; }
            }
            return bestX;
        };
        for (int i = 0; i < juce::jmin (numSpeakers, kMaxSpeakers); ++i)
        {
            auto& s = speakers[(size_t) i];
            if (s.placed) continue;
            s.y = 0.27f;
            s.x = freeSpot (s.y, 0.15f, 0.85f);
            s.angle = 90.0f;   // facing the audience
            s.placed = true;
        }
        if (! monitorMic.placed) { monitorMic = { 0.5f, 0.9f, -90.0f, true }; }   // at FOH
        for (int m = 0; m < juce::jmin (numMics, kMaxMics); ++m)
        {
            auto& p = mics[(size_t) m];
            if (p.placed) continue;
            p.y = 0.55f + 0.1f * (float) (m % 2);
            p.x = freeSpot (p.y, 0.25f, 0.75f);
            p.angle = -90.0f;  // toward the stage
            p.placed = true;
        }
    }

    int markerOf (const juce::String& name) const
    {
        for (int i = 0; i < (int) items.size(); ++i)
            if (items[(size_t) i].kind == PlanItem::Marker && items[(size_t) i].text == name) return i;
        return -1;
    }

    // The marker of a named position, placed when there is none: on an area of the same name (FOH), otherwise
    // spread across the seating, away from the markers already there. Returns its index in `items`.
    int ensureMarker (const juce::String& name)
    {
        if (const int i = markerOf (name); i >= 0) return i;
        for (const auto& it : items)
            if (it.kind == PlanItem::Area && it.text.isNotEmpty()
                && (it.text.equalsIgnoreCase (name) || name.containsIgnoreCase (it.text) || it.text.containsIgnoreCase (name)))
            {
                items.push_back ({ PlanItem::Marker, name, it.x, it.y, 0.0f, 0.0f });
                return (int) items.size() - 1;
            }
        // the biggest seating area, or the middle of the room
        float x0 = 0.15f, x1 = 0.85f, y0 = 0.38f, y1 = 0.8f, best = 0.0f;
        for (const auto& it : items)
            if (it.kind == PlanItem::Area && (it.seating != PlanItem::Plain || it.text.containsIgnoreCase ("seat")) && it.w * it.h > best)
            {
                best = it.w * it.h;
                x0 = it.x - it.w * 0.5f; x1 = it.x + it.w * 0.5f;
                y0 = it.y - it.h * 0.5f; y1 = it.y + it.h * 0.5f;
            }
        float bx = 0.5f * (x0 + x1), by = 0.5f * (y0 + y1), bestD = -1.0f;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 5; ++col)
            {
                const float x = x0 + (x1 - x0) * (0.14f + 0.18f * (float) col), y = y0 + (y1 - y0) * (0.2f + 0.22f * (float) row);
                float d = 10.0f;
                for (const auto& it : items) if (it.kind == PlanItem::Marker) d = juce::jmin (d, std::hypot (it.x - x, it.y - y));
                if (d > bestD + 0.02f) { bestD = d; bx = x; by = y; }   // front rows and the left first when it is a tie
            }
        items.push_back ({ PlanItem::Marker, name, bx, by, 0.0f, 0.0f });
        return (int) items.size() - 1;
    }

    // The speaker a mic at `x, y` aims at: the nearest placed one that `isTop` (or any). -1: none placed.
    int nearestSpeaker (float x, float y, int numSpeakers, const std::function<bool (int)>& isTop) const
    {
        int pick = -1;
        float bestD = 1e9f;
        for (int pass = 0; pass < 2 && pick < 0; ++pass)
            for (int i = 0; i < juce::jmin (numSpeakers, kMaxSpeakers); ++i)
            {
                const auto& s = speakers[(size_t) i];
                if (! s.placed || (pass == 0 && isTop && ! isTop (i))) continue;
                const float d = std::hypot (s.x - x, s.y - y);
                if (d < bestD) { bestD = d; pick = i; }
            }
        return pick;
    }

    // After speaker `i` is removed from the system.
    void speakerRemoved (int i)
    {
        for (int k = i; k < kMaxSpeakers - 1; ++k) speakers[(size_t) k] = speakers[(size_t) k + 1];
        speakers[kMaxSpeakers - 1] = {};
    }

    void micRemoved (int i)
    {
        for (int k = i; k < kMaxMics - 1; ++k) mics[(size_t) k] = mics[(size_t) k + 1];
        mics[kMaxMics - 1] = {};
    }

    static void saveSymbol (juce::XmlElement& e, const PlanSymbol& s)
    {
        e.setAttribute ("x", s.x);
        e.setAttribute ("y", s.y);
        e.setAttribute ("angle", s.angle);
    }

    static PlanSymbol loadSymbol (const juce::XmlElement& e)
    {
        PlanSymbol s;
        s.x = juce::jlimit (0.0f, 1.0f, (float) e.getDoubleAttribute ("x", 0.5));
        s.y = juce::jlimit (0.0f, 1.0f, (float) e.getDoubleAttribute ("y", 0.5));
        s.angle = (float) e.getDoubleAttribute ("angle", -90.0);
        s.placed = true;
        return s;
    }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("VenuePlan");
        e->setAttribute ("edited", edited);
        for (int i = 0; i < kMaxSpeakers; ++i)
            if (speakers[(size_t) i].placed)
            {
                auto* c = e->createNewChildElement ("Speaker");
                c->setAttribute ("index", i);
                saveSymbol (*c, speakers[(size_t) i]);
            }
        for (int m = 0; m < kMaxMics; ++m)
            if (mics[(size_t) m].placed)
            {
                auto* c = e->createNewChildElement ("Mic");
                c->setAttribute ("index", m);
                saveSymbol (*c, mics[(size_t) m]);
            }
        if (monitorMic.placed) saveSymbol (*e->createNewChildElement ("MonitorMic"), monitorMic);
        for (const auto& it : items)
        {
            auto* c = e->createNewChildElement ("Item");
            c->setAttribute ("kind", it.kind);
            c->setAttribute ("text", it.text);
            c->setAttribute ("x", it.x);
            c->setAttribute ("y", it.y);
            c->setAttribute ("w", it.w);
            c->setAttribute ("h", it.h);
            if (it.seating != PlanItem::Plain) c->setAttribute ("seating", it.seating);
        }
    }

    void load (const juce::XmlElement& parent)
    {
        const auto* e = parent.getChildByName ("VenuePlan");
        if (e == nullptr) { *this = defaults(); return; }
        *this = {};
        edited = e->getBoolAttribute ("edited", true);
        for (const auto* c : e->getChildWithTagNameIterator ("Speaker"))
        {
            const int i = c->getIntAttribute ("index", -1);
            if (i >= 0 && i < kMaxSpeakers) speakers[(size_t) i] = loadSymbol (*c);
        }
        for (const auto* c : e->getChildWithTagNameIterator ("Mic"))
        {
            const int m = c->getIntAttribute ("index", -1);
            if (m >= 0 && m < kMaxMics) mics[(size_t) m] = loadSymbol (*c);
        }
        if (const auto* mm = e->getChildByName ("MonitorMic")) monitorMic = loadSymbol (*mm);
        for (const auto* c : e->getChildWithTagNameIterator ("Item"))
        {
            PlanItem it;
            it.kind = juce::jlimit (0, 2, c->getIntAttribute ("kind"));
            it.text = c->getStringAttribute ("text");
            it.x = juce::jlimit (0.0f, 1.0f, (float) c->getDoubleAttribute ("x", 0.5));
            it.y = juce::jlimit (0.0f, 1.0f, (float) c->getDoubleAttribute ("y", 0.5));
            it.w = juce::jlimit (0.03f, 1.0f, (float) c->getDoubleAttribute ("w", 0.3));
            it.h = juce::jlimit (0.03f, 1.0f, (float) c->getDoubleAttribute ("h", 0.15));
            // seating areas from earlier versions get rows of chairs
            it.seating = juce::jlimit (0, 3, c->getIntAttribute ("seating", it.kind == PlanItem::Area && it.text.containsIgnoreCase ("seat") ? PlanItem::Rows : PlanItem::Plain));
            items.push_back (it);
        }
    }
};
} // namespace la
