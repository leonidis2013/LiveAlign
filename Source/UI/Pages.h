// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Placeholder pages for the steps that come in later stages.
#include <functional>
#include "NavRail.h"

namespace la::ui
{
class PlaceholderPage : public juce::Component
{
public:
    PlaceholderPage (int pageIndex, juce::String stage, juce::String introText, juce::StringArray bulletPoints)
        : page (pageIndex), stageText (std::move (stage)), intro (std::move (introText)), bullets (std::move (bulletPoints))
    {
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto card = getLocalBounds().withSizeKeepingCentre (juce::jmin (780, getWidth()), juce::jmin (460, getHeight()));
        drawPanel (g, card.toFloat(), {});

        auto r = card.reduced (36, 30);
        auto icon = pageIcon (page);
        icon.applyTransform (juce::AffineTransform::scale (2.0f).translated ((float) r.getX(), (float) r.getY()));
        g.setColour (p.accent);
        g.strokePath (icon, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        r.removeFromTop (60);

        g.setColour (p.accent);
        g.setFont (font (11.5f, true));
        g.drawText ("STEP " + juce::String (page) + "   " + stageText.toUpperCase(), r.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (p.text);
        g.setFont (font (24.0f, true));
        g.drawText (pageName (page), r.removeFromTop (38), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (13.5f));
        g.drawFittedText (intro, r.removeFromTop (44), juce::Justification::topLeft, 2);
        r.removeFromTop (6);
        for (const auto& b : bullets)
        {
            auto line = r.removeFromTop (28);
            g.setColour (p.accent);
            g.fillEllipse (line.removeFromLeft (18).toFloat().withSizeKeepingCentre (6.0f, 6.0f));
            g.setColour (p.text);
            g.setFont (font (13.5f));
            g.drawText (b, line, juce::Justification::centredLeft, true);
        }
    }

private:
    int page;
    juce::String stageText, intro;
    juce::StringArray bullets;
};
} // namespace la::ui
