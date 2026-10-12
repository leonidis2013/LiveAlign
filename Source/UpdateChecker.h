// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Check for updates: asks GitHub for the latest LiveAlign release and compares its version with this build.
// Runs on its own thread; the result is delivered on the message thread. Preferences (check at start, last result)
// live in the user folder, not in venue files.
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <juce_events/juce_events.h>
#include "TraceStore.h"

namespace la
{
struct UpdateInfo
{
    enum State { Unknown, Checking, UpToDate, Available, Failed };
    State state = Unknown;
    juce::String latest;      // "0.8.0"
    juce::String pageUrl;     // release page
    juce::String notes;       // release notes (first lines)
    juce::String error;
    juce::Time checkedAt;
};

class UpdateChecker
{
public:
    static constexpr const char* kRepo = "leonidis2013/LiveAlign";

    UpdateChecker() { loadPrefs(); }
    ~UpdateChecker() { alive->store (false); }

    std::function<void()> onResult;   // message thread

    const UpdateInfo& info() const { return current; }
    bool autoCheck() const { return checkAtStart; }
    void setAutoCheck (bool b) { checkAtStart = b; savePrefs(); }

    // Compares dotted versions ("0.10.1" > "0.9.3"). Returns -1, 0 or 1.
    static int compareVersions (const juce::String& a, const juce::String& b)
    {
        auto parts = [] (juce::String v)
        {
            v = v.trim().trimCharactersAtStart ("vV");
            juce::StringArray p;
            p.addTokens (v.upToFirstOccurrenceOf ("-", false, false), ".", {});
            return p;
        };
        const auto pa = parts (a), pb = parts (b);
        for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
        {
            const int x = pa[i].getIntValue(), y = pb[i].getIntValue();
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
    }

    static juce::String thisVersion() { return JucePlugin_VersionString; }

    // Reads GitHub's "latest release" answer (JSON). Kept separate so it can be tested.
    static UpdateInfo parseRelease (const juce::String& json, const juce::String& installed)
    {
        UpdateInfo r;
        r.checkedAt = juce::Time::getCurrentTime();
        const auto v = juce::JSON::parse (json);
        const auto tag = v.getProperty ("tag_name", {}).toString();
        if (tag.isEmpty()) { r.state = UpdateInfo::Failed; r.error = "The release information could not be read."; return r; }
        r.latest = tag.trimCharactersAtStart ("vV");
        r.pageUrl = v.getProperty ("html_url", juce::String ("https://github.com/") + kRepo + "/releases/latest").toString();
        juce::StringArray lines;
        lines.addLines (v.getProperty ("body", {}).toString());
        lines.removeEmptyStrings();
        while (lines.size() > 8) lines.remove (lines.size() - 1);
        r.notes = lines.joinIntoString ("\n");
        r.state = compareVersions (r.latest, installed) > 0 ? UpdateInfo::Available : UpdateInfo::UpToDate;
        return r;
    }

    void check()
    {
        if (current.state == UpdateInfo::Checking) return;
        current.state = UpdateInfo::Checking;
        if (onResult) onResult();
        auto flag = alive;
        auto* self = this;
        std::thread ([flag, self]
        {
            UpdateInfo r;
            const juce::URL url (juce::String ("https://api.github.com/repos/") + kRepo + "/releases/latest");
            int status = 0;
            juce::StringPairArray headers;
            auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: LiveAlign\r\n")
                                                     .withConnectionTimeoutMs (10000)
                                                     .withStatusCode (&status)
                                                     .withResponseHeaders (&headers));
            if (stream == nullptr)
            {
                r.state = UpdateInfo::Failed;
                r.error = "No connection to GitHub. Check the internet connection and try again.";
            }
            else
            {
                const auto body = stream->readEntireStreamAsString();
                if (status != 0 && status != 200)
                {
                    r.state = UpdateInfo::Failed;
                    r.error = status == 404 ? juce::String ("No release has been published yet.")
                                            : "GitHub answered with error " + juce::String (status) + ". Try again later.";
                }
                else
                    r = parseRelease (body, thisVersion());
            }
            r.checkedAt = juce::Time::getCurrentTime();
            if (! flag->load()) return;
            juce::MessageManager::callAsync ([flag, self, r]
            {
                if (! flag->load()) return;
                self->current = r;
                self->savePrefs();
                if (self->onResult) self->onResult();
            });
        }).detach();
    }

    // Once per start, when the user has not turned it off.
    void checkAtStartIfWanted() { if (checkAtStart && ! startChecked) { startChecked = true; check(); } }

    void openReleasePage() const
    {
        juce::URL (current.pageUrl.isNotEmpty() ? current.pageUrl : juce::String ("https://github.com/") + kRepo + "/releases/latest").launchInDefaultBrowser();
    }

private:
    static juce::File prefsFile() { return TraceStore::userFolder().getChildFile ("Preferences.xml"); }

    void loadPrefs()
    {
        if (auto x = juce::XmlDocument::parse (prefsFile()))
        {
            checkAtStart = x->getBoolAttribute ("checkUpdatesAtStart", true);
            // a newer version found last time is still worth showing until checked again
            const auto latest = x->getStringAttribute ("latestVersion");
            if (latest.isNotEmpty() && compareVersions (latest, thisVersion()) > 0)
            {
                current.state = UpdateInfo::Available;
                current.latest = latest;
                current.pageUrl = x->getStringAttribute ("latestUrl");
            }
        }
    }

    void savePrefs() const
    {
        juce::XmlElement x ("LiveAlignPreferences");
        if (auto old = juce::XmlDocument::parse (prefsFile())) x = *old;
        x.setAttribute ("checkUpdatesAtStart", checkAtStart);
        if (current.state == UpdateInfo::Available || current.state == UpdateInfo::UpToDate)
        {
            x.setAttribute ("latestVersion", current.latest);
            x.setAttribute ("latestUrl", current.pageUrl);
            x.setAttribute ("lastChecked", current.checkedAt.toISO8601 (true));
        }
        prefsFile().getParentDirectory().createDirectory();
        x.writeTo (prefsFile());
    }

    UpdateInfo current;
    bool checkAtStart = true, startChecked = false;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace la
