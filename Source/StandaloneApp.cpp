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

        la::StandaloneHooks::showAudioSettings() = [h] { h->showAudioSettingsDialog(); };
        la::StandaloneHooks::deviceDescription() = [h]() -> juce::String
        {
            if (auto* dev = h->deviceManager.getCurrentAudioDevice())
                return dev->getName() + "  " + juce::String (dev->getCurrentSampleRate() / 1000.0, 1) + " kHz  "
                       + juce::String (dev->getCurrentBufferSizeSamples()) + " samples";
            return "No audio device";
        };

        la::StandaloneHooks::toggleFullScreen() = [this]
        {
            auto& desktop = juce::Desktop::getInstance();
            if (mainWindow == nullptr) return;
            if (desktop.getKioskModeComponent() == mainWindow.get()) desktop.setKioskModeComponent (nullptr, false);
            else desktop.setKioskModeComponent (mainWindow.get(), false);
        };
        la::StandaloneHooks::isFullScreen() = [this]
        {
            return mainWindow != nullptr && juce::Desktop::getInstance().getKioskModeComponent() == mainWindow.get();
        };

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            std::move (holder));
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        la::StandaloneHooks::showAudioSettings() = nullptr;
        la::StandaloneHooks::deviceDescription() = nullptr;
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
