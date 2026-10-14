// Custom standalone application (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1).
// Same as JUCE's default standalone app, with two changes for a measurement tool:
//  - the audio input is NOT muted. JUCE mutes it by default to stop a plug-in feeding its input back to its output;
//    LiveAlign's app never passes its inputs to its outputs, so that protection would only silence the mic.
//  - the editor gets a button for the audio device settings.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if JucePlugin_Build_Standalone

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "StandaloneHooks.h"

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
        la::StandaloneHooks::showAudioSettings() = [h]
        {
            auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (h->deviceManager, 0, 64, 0, 64, false, false, false, false);
            selector->setSize (520, 420);
            juce::DialogWindow::LaunchOptions o;
            o.content.setOwned (selector.release());
            o.dialogTitle = "Audio interface";
            o.dialogBackgroundColour = juce::Colour (0xff161d22);
            o.escapeKeyTriggersCloseButton = true;
            o.useNativeTitleBar = true;
            o.resizable = true;
            o.launchAsync();
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
        if (juce::Desktop::getInstance().getKioskModeComponent() != nullptr)
            juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
        mainWindow = nullptr;
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
};

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new LiveAlignStandaloneApp(); }

#endif
