// Custom standalone application (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1).
// Same as JUCE's default standalone app, with two changes for a measurement tool:
//  - the audio input is NOT muted. JUCE mutes it by default to stop a plug-in feeding its input back to its output;
//    LiveAlign's app never passes its inputs to its outputs, so that protection would only silence the mic.
//  - the editor gets a button for the audio device settings.
//  - an audio interface that goes away (unplugged, switched off) is NOT replaced by another device: JUCE would open
//    the computer's own audio instead, and measuring or playing pink noise there by surprise is worse than stopping.
//    LiveAlign closes the audio, says the interface is gone, and opens it again when it comes back.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if JucePlugin_Build_Standalone

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "StandaloneHooks.h"
#include "InterfaceGuard.h"
#include "UI/ListenSetup.h"

namespace la::ui
{
// The Audio interface window's content: the device selector, and the headphones under it.
class AudioWindowContent : public juce::Component
{
public:
    AudioWindowContent (juce::AudioDeviceManager& dm, LiveAlignProcessor* p)
        : selector (dm, 0, 64, 0, 64, false, false, false, false)
    {
        addAndMakeVisible (selector);
        if (p != nullptr) { listenSetup = std::make_unique<ListenSetup> (*p); addAndMakeVisible (*listenSetup); }
        setSize (540, listenSetup != nullptr ? 640 : 440);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        if (listenSetup != nullptr) listenSetup->setBounds (r.removeFromBottom (190));
        selector.setBounds (r);
    }

private:
    juce::AudioDeviceSelectorComponent selector;
    std::unique_ptr<ListenSetup> listenSetup;
};
} // namespace la::ui

class LiveAlignStandaloneApp final : public juce::JUCEApplication
{
public:
    LiveAlignStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = JucePlugin_Name;
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            return;

        auto holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String {},
                                                                      nullptr, juce::Array<juce::StandalonePluginHolder::PluginInOuts> {},
                                                                      false);
        auto* h = holder.get();
        h->getMuteInputValue().setValue (false);

        // LiveAlign's own audio settings: every input and output listed on its own (JUCE's standalone dialog groups
        // them in stereo pairs, so a mic on input 2 alone could not be picked), and no MIDI or feedback options.
        la::StandaloneHooks::showAudioSettings() = [this, h]
        {
            // the interface, and under it the headphones (what to hear and where: a spare output, or another device)
            auto content = std::make_unique<la::ui::AudioWindowContent> (h->deviceManager, dynamic_cast<LiveAlignProcessor*> (h->processor.get()));
            juce::DialogWindow::LaunchOptions o;
            o.content.setOwned (content.release());
            o.dialogTitle = "Audio interface";
            o.dialogBackgroundColour = juce::Colour (0xff161d22);
            o.escapeKeyTriggersCloseButton = true;
            o.useNativeTitleBar = true;
            o.resizable = true;
            audioWindow = o.launchAsync();
        };
        // The input and the output device, which can be different (a USB interface in, the computer's speakers out).
        auto devicesText = [h]() -> juce::String
        {
            const auto setup = h->deviceManager.getAudioDeviceSetup();
            if (h->deviceManager.getCurrentAudioDevice() == nullptr) return {};
            if (setup.inputDeviceName.isEmpty() || setup.inputDeviceName == setup.outputDeviceName)
                return setup.outputDeviceName.isNotEmpty() ? setup.outputDeviceName : h->deviceManager.getCurrentAudioDevice()->getName();
            return "in: " + setup.inputDeviceName + ", out: " + (setup.outputDeviceName.isNotEmpty() ? setup.outputDeviceName : juce::String ("none"));
        };
        la::StandaloneHooks::deviceDescription() = [h, devicesText]() -> juce::String
        {
            if (auto* dev = h->deviceManager.getCurrentAudioDevice())
                return devicesText() + "  " + juce::String (dev->getCurrentSampleRate() / 1000.0, 1) + " kHz  "
                       + juce::String (dev->getCurrentBufferSizeSamples()) + " samples";
            return "No audio device";
        };

        la::StandaloneHooks::deviceName() = [devicesText]() -> juce::String { return devicesText(); };
        la::StandaloneHooks::toggleFullScreen() = [this]
        {
            if (mainWindow == nullptr) return;
           #if JUCE_MAC
            mainWindow->setFullScreen (! mainWindow->isFullScreen());   // macOS full screen (its own space), as the green button
           #else
            auto& desktop = juce::Desktop::getInstance();
            if (desktop.getKioskModeComponent() == mainWindow.get()) desktop.setKioskModeComponent (nullptr, false);
            else desktop.setKioskModeComponent (mainWindow.get(), false);
           #endif
        };
        la::StandaloneHooks::isFullScreen() = [this]
        {
           #if JUCE_MAC
            return mainWindow != nullptr && mainWindow->isFullScreen();
           #else
            return mainWindow != nullptr && juce::Desktop::getInstance().getKioskModeComponent() == mainWindow.get();
           #endif
        };

        interfaceGuard = std::make_unique<la::InterfaceGuard> (h->deviceManager, appProperties.getUserSettings()->getXmlValue ("audioSetup"),
                                                                   h->processor->getTotalNumInputChannels(), h->processor->getTotalNumOutputChannels(),
                                                                   [this] { return audioWindow != nullptr; });
        la::StandaloneHooks::driverOverloads() = [h]() -> int { return h->deviceManager.getXRunCount(); };
        la::StandaloneHooks::interfaceLost() = [this]() -> juce::String { return interfaceGuard != nullptr ? interfaceGuard->lostName() : juce::String(); };

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            std::move (holder));
        // JUCE's "Options" title-bar button is replaced by Settings (audio device) and venue files (state)
        for (auto* c : mainWindow->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
                if (b->getButtonText() == "Options") b->setVisible (false);
        // The system's own title bar with all three buttons: on macOS the small red / yellow / green ones, and green
        // takes the window to full screen in its own space (macOS only offers that when the window has a maximise
        // button and is resizable). On Windows the usual minimise / maximise / close.
        mainWindow->setResizable (true, false);
        mainWindow->setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, false);
        mainWindow->setUsingNativeTitleBar (true);
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        la::StandaloneHooks::showAudioSettings() = nullptr;
        la::StandaloneHooks::deviceDescription() = nullptr;
        la::StandaloneHooks::deviceName() = nullptr;
        la::StandaloneHooks::toggleFullScreen() = nullptr;
        la::StandaloneHooks::isFullScreen() = nullptr;
        la::StandaloneHooks::interfaceLost() = nullptr;
        la::StandaloneHooks::driverOverloads() = nullptr;
        // the interface is gone: keep it as the choice for next time (not what JUCE opened, or nothing)
        auto keep = interfaceGuard != nullptr ? interfaceGuard->savedChoice() : nullptr;
        interfaceGuard = nullptr;
        if (juce::Desktop::getInstance().getKioskModeComponent() != nullptr)
            juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
        mainWindow = nullptr;
        if (keep != nullptr) appProperties.getUserSettings()->setValue ("audioSetup", keep.get());
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
    std::unique_ptr<la::InterfaceGuard> interfaceGuard;   // after the window: it watches the window's device manager, so goes first
    juce::Component::SafePointer<juce::DialogWindow> audioWindow;
};

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new LiveAlignStandaloneApp(); }

#endif
