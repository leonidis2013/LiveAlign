#pragma once
// Progress through the guided tabs, saved with the venue project. Message thread only.
#include <juce_core/juce_core.h>

namespace la
{
struct MicChecklist
{
    bool calFileSkipped = false;   // "my mic has no calibration file"
    bool splSkipped = false;       // "no calibrator: show dBFS"
    bool loopbackSkipped = false;

    bool loopbackDone = false;
    double loopbackLatencyMs = 0.0;
    bool loopbackInverted = false;
    float loopbackLevelDbfs = -200.0f;

    bool noiseDone = false;
    float noiseA = -200.0f, noiseC = -200.0f;
    bool noiseIsSpl = false;

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("MicSetup");
        e->setAttribute ("calFileSkipped", calFileSkipped);
        e->setAttribute ("splSkipped", splSkipped);
        e->setAttribute ("loopbackSkipped", loopbackSkipped);
        e->setAttribute ("loopbackDone", loopbackDone);
        e->setAttribute ("loopbackLatencyMs", loopbackLatencyMs);
        e->setAttribute ("loopbackInverted", loopbackInverted);
        e->setAttribute ("loopbackLevelDbfs", loopbackLevelDbfs);
        e->setAttribute ("noiseDone", noiseDone);
        e->setAttribute ("noiseA", noiseA);
        e->setAttribute ("noiseC", noiseC);
        e->setAttribute ("noiseIsSpl", noiseIsSpl);
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        if (const auto* e = parent.getChildByName ("MicSetup"))
        {
            calFileSkipped = e->getBoolAttribute ("calFileSkipped");
            splSkipped = e->getBoolAttribute ("splSkipped");
            loopbackSkipped = e->getBoolAttribute ("loopbackSkipped");
            loopbackDone = e->getBoolAttribute ("loopbackDone");
            loopbackLatencyMs = e->getDoubleAttribute ("loopbackLatencyMs");
            loopbackInverted = e->getBoolAttribute ("loopbackInverted");
            loopbackLevelDbfs = (float) e->getDoubleAttribute ("loopbackLevelDbfs", -200.0);
            noiseDone = e->getBoolAttribute ("noiseDone");
            noiseA = (float) e->getDoubleAttribute ("noiseA", -200.0);
            noiseC = (float) e->getDoubleAttribute ("noiseC", -200.0);
            noiseIsSpl = e->getBoolAttribute ("noiseIsSpl");
        }
    }
};
} // namespace la
