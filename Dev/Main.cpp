/*
  ==============================================================================

    Dev/Main.cpp

    FxmeSamplerDev — a standalone host for kits that have not been compiled.

    Point it at a folder holding a mapping.xml (plus its wav/, img/ and
    presets/ directories, in any arrangement, only file names matter) and it
    builds a complete FxmeSamplerAudioProcessor around that folder: the same
    engine, the same editor, the same parameters as a shipping kit.

    This can't be a plugin
    ----------------------
    A mapping decides how many parameters exist, and a host reads the parameter
    list the moment the constructor returns so the kit has to be chosen
    before the processor is built. In a plugin that is impossible: the DAW
    builds the processor. Here the choice happens first and reloading simply
    throws the processor away and constructs a new one, which is also why no
    lock-free hand-over is needed (see the threading note in Sampler.h): the
    player is detached before anything is torn down, so no audio thread is
    running while the swap happens.

  ==============================================================================
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ResourceProvider.h"
#include "EffectChainAmp.h"
#include "EffectChainReverb.h"
#include <iostream>

namespace
{
    const char* const lastKitProperty    = "lastKitFolder";
    const char* const audioStateProperty = "audioDeviceState";

    constexpr int barHeight      = 34;
    constexpr int keyboardHeight = 72;
    constexpr int minimumWidth   = 760;
}

//==============================================================================
/**
 * @brief The window contents: a toolbar, the kit's own editor, a keyboard.
 */
class KitHostComponent final : public juce::Component,
                               private juce::MidiKeyboardState::Listener
{
public:
    explicit KitHostComponent (juce::PropertiesFile& propertiesToUse)
        : properties (propertiesToUse),
          keyboard (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        std::unique_ptr<juce::XmlElement> savedAudioState (properties.getXmlValue (audioStateProperty));

        // A failure here is not fatal — the editor still works, the kit still
        // loads — but silence with no explanation is not something to leave
        // the user guessing about.
        if (const auto error = deviceManager.initialise (0, 2, savedAudioState.get(), true); error.isNotEmpty())
            audioError = "No audio: " + error + "  |  ";

        deviceManager.addAudioCallback (&player);

        // Every MIDI input, no picking: this is a test rig and the point is
        // that whatever keyboard is plugged in just works.
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled (input.identifier, true);

        deviceManager.addMidiInputDeviceCallback ({}, &player);
        keyboardState.addListener (this);

        loadButton.onClick = [this] { chooseKitFolder(); };
        reloadButton.onClick = [this] { loadKit (currentFolder); };
        audioButton.onClick = [this] { showAudioSettings(); };

        for (auto* b : { &loadButton, &reloadButton, &audioButton })
            addAndMakeVisible (b);

        status.setJustificationType (juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (status);

        // These kits live between the kick at 36 and the top of a bass range,
        // so opening on the bottom octaves saves a scroll every time.
        keyboard.setLowestVisibleKey (36);
        addAndMakeVisible (keyboard);

        reloadButton.setEnabled (false);
        setStatus ("No kit loaded. Load a folder containing mapping.xml.",
                   audioError.isEmpty() ? neutralColour : errorColour);
        setSize (minimumWidth, barHeight + keyboardHeight + 200);
    }

    ~KitHostComponent() override
    {
        keyboardState.removeListener (this);
        deviceManager.removeMidiInputDeviceCallback ({}, &player);
        deviceManager.removeAudioCallback (&player);

        if (auto state = deviceManager.createStateXml())
            properties.setValue (audioStateProperty, state.get());

        properties.saveIfNeeded();

        unloadKit();
    }

    /** @brief Loads the folder remembered from last time, or the one given on
               the command line. Safe to call with a folder that no longer
               exists. */
    void loadInitialKit (const juce::File& fromCommandLine)
    {
        const auto folder = fromCommandLine.isDirectory()
                                ? fromCommandLine
                                : juce::File (properties.getValue (lastKitProperty));

        if (folder.isDirectory())
            loadKit (folder);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1a1a1a));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (barHeight).reduced (4, 4);

        loadButton  .setBounds (bar.removeFromLeft (110));
        bar.removeFromLeft (4);
        reloadButton.setBounds (bar.removeFromLeft (80));
        bar.removeFromLeft (4);
        audioButton .setBounds (bar.removeFromLeft (80));
        bar.removeFromLeft (10);
        status.setBounds (bar);

        keyboard.setBounds (area.removeFromBottom (keyboardHeight));

        if (editor != nullptr)
            editor->setBounds (area);
    }

private:
    //==========================================================================
    void chooseKitFolder()
    {
        const auto start = currentFolder.isDirectory() ? currentFolder
                                                       : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Select a kit folder (containing mapping.xml)", start);

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto folder = fc.getResult();

                                  if (folder.isDirectory())
                                      loadKit (folder);
                              });
    }

    void loadKit (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return;

        unloadKit();

        auto folderResources = std::make_unique<fxsampler::FolderResources> (folder);

        if (! folderResources->isValid())
        {
            setStatus (folderResources->getWarnings().joinIntoString ("  |  "), errorColour);
            return;
        }

        // Order matters on the way out as much as on the way in: the sounds
        // hold pointers into the provider, so it is released last (see
        // unloadKit).
        resources = std::move (folderResources);
        currentFolder = folder;

        processor = std::make_unique<FxmeSamplerAudioProcessor> (*resources, resources->getPresetFolder());

        editor.reset (processor->createEditorIfNeeded());

        if (editor != nullptr)
            addAndMakeVisible (*editor);

        player.setProcessor (processor.get());

        properties.setValue (lastKitProperty, folder.getFullPathName());
        properties.saveIfNeeded();
        reloadButton.setEnabled (true);

        reportLoad();
        sizeToEditor();
        resized();
    }

    void unloadKit()
    {
        player.setProcessor (nullptr);

        if (editor != nullptr && processor != nullptr)
            processor->editorBeingDeleted (editor.get());

        editor.reset();
        processor.reset();
        resources.reset();
        reloadButton.setEnabled (false);
    }

    /** Everything a mapping asks for is resolved while the processor is being
        built, so by now the provider knows exactly what it could not find. */
    void reportLoad()
    {
        juce::String text;
        text << currentFolder.getFileName() << " — " << resources->getNumFiles() << " files";

        if (processor != nullptr)
            text << ", " << processor->getParameters().size() << " parameters";

        const auto& misses = resources->getMisses();
        const auto& warnings = resources->getWarnings();

        if (! warnings.isEmpty())
            text << "  |  " << warnings.joinIntoString ("  |  ");

        if (! misses.isEmpty())
            text << "  |  missing: " << misses.joinIntoString (", ");

        setStatus (text, (misses.isEmpty() && warnings.isEmpty()) ? okColour : errorColour);
    }

    void setStatus (const juce::String& body, juce::Colour colour)
    {
        const auto text = audioError + body;
        status.setText (text, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, audioError.isEmpty() ? colour : errorColour);
        status.setTooltip (text);
    }

    void showAudioSettings()
    {
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager, 0, 0, 2, 8, true, false, true, false);
        selector->setSize (500, 350);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (selector.release());
        options.dialogTitle = "Audio / MIDI settings";
        options.dialogBackgroundColour = juce::Colour (0xff1a1a1a);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void sizeToEditor()
    {
        const int width  = juce::jmax (minimumWidth, editor != nullptr ? editor->getWidth() : 0);
        const int height = (editor != nullptr ? editor->getHeight() : 200) + barHeight + keyboardHeight;

        if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
            window->setContentComponentSize (width, height);
        else
            setSize (width, height);
    }

    //==========================================================================
    // The on-screen keyboard feeds the player's collector, which is merged
    // into the MIDI the processor sees alongside the hardware inputs.
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override
    {
        postToPlayer (juce::MidiMessage::noteOn (channel, note, velocity));
    }

    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override
    {
        postToPlayer (juce::MidiMessage::noteOff (channel, note, velocity));
    }

    void postToPlayer (juce::MidiMessage message)
    {
        message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        player.getMidiMessageCollector().addMessageToQueue (message);
    }

    //==========================================================================
    static const juce::Colour neutralColour, okColour, errorColour;

    juce::PropertiesFile& properties;
    juce::String audioError;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;

    // Declared in load order and destroyed in the reverse: the editor points
    // at the processor, and the processor's sounds point into the provider.
    std::unique_ptr<fxsampler::FolderResources> resources;
    std::unique_ptr<FxmeSamplerAudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;

    juce::File currentFolder;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    juce::TextButton loadButton { "Load kit..." }, reloadButton { "Reload" }, audioButton { "Audio..." };
    juce::Label status;

    // The status line is where missing resources are reported and it runs out
    // of room quickly; hovering gives the whole thing.
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitHostComponent)
};

const juce::Colour KitHostComponent::neutralColour { 0xffb0b0b0 };
const juce::Colour KitHostComponent::okColour      { 0xff80d080 };
const juce::Colour KitHostComponent::errorColour   { 0xffff8060 };

//==============================================================================
/**
 * @brief Loads a kit folder, prints what came of it, and opens no window.
 *
 * The same construction the GUI performs, so it answers the question that
 * matters while authoring — does this mapping resolve? — from a shell or a
 * build script. Returns 0 when every name in the mapping found a file.
 */
static int checkKitFolder (const juce::File& folder)
{
    fxsampler::FolderResources resources (folder);

    std::cout << "Folder      : " << folder.getFullPathName() << std::endl;

    if (! resources.isValid())
    {
        for (const auto& warning : resources.getWarnings())
            std::cout << "ERROR       : " << warning << std::endl;

        return 1;
    }

    std::cout << "Mapping     : " << resources.getMappingFile().getFullPathName() << std::endl;
    std::cout << "Files       : " << resources.getNumFiles() << std::endl;
    std::cout << "Presets dir : " << resources.getPresetFolder().getFullPathName()
              << (resources.getPresetFolder().isDirectory() ? "" : "  (does not exist yet)") << std::endl;

    FxmeSamplerAudioProcessor processor (resources, resources.getPresetFolder());

    std::cout << "Groups      : " << processor.getSampler().getSampleGroups().size() << std::endl;
    std::cout << "Strips      : " << processor.getMixer().getStrips().size() << std::endl;

    // Naming an impulse response is not the same as loading one: Cab and
    // ConvolReverb look their names up in BinaryData themselves, so an IR that
    // is only in the kit folder, or misspelled, gives a populated chooser and
    // no convolution at all — silently. Both load on their first process()
    // call, before they check whether they are switched on, so one silent block
    // per chain is enough to find out which.
    processor.prepareToPlay (44100.0, 512);

    juce::AudioBuffer<float> silence (2, 512);

    auto touch = [&silence] (EffectChain* chain)
    {
        if (chain != nullptr)
        {
            silence.clear();
            chain->process (silence);   // deliberately direct: no mute, solo or
        }                               // routing between us and the answer
    };

    for (const auto& strip : processor.getMixer().getStrips())
        touch (strip->getEffectChain());

    juce::StringArray irProblems;

    // What each strip's effectChain= actually resolved to, how many IRs reached
    // the chain that consumes them, and whether those IRs loaded.
    auto describeStrip = [&irProblems] (const juce::String& name, EffectChain* chain)
    {
        std::cout << "  " << name.paddedRight (' ', 14)
                  << (chain != nullptr ? chain->getTypeName() : juce::String ("(none)"));

        auto reportIRs = [&] (const juce::String& what, int listed, std::vector<int> loaded)
        {
            std::cout << "  " << listed << " " << what;

            juce::StringArray lengths;
            bool anyEmpty = false;

            for (auto samples : loaded)
            {
                lengths.add (samples > 0 ? juce::String (samples) + " smp" : juce::String ("EMPTY"));
                anyEmpty = anyEmpty || samples <= 0;
            }

            std::cout << "  [" << lengths.joinIntoString (" / ") << "]";

            if (listed > 0 && anyEmpty)
                irProblems.add (name + ": " + juce::String (listed) + " " + what
                                    + " named, but not embedded in this binary");
        };

        if (auto* amp = dynamic_cast<EffectChainAmp*> (chain))
            reportIRs ("cab IRs", amp->getCab().getImpulseNames().size(),
                       { amp->getCab().getIR (0).getNumSamples(),
                         amp->getCab().getIR (1).getNumSamples() });
        else if (auto* reverb = dynamic_cast<EffectChainReverb*> (chain))
            reportIRs ("IRs", reverb->getReverb().getImpulseNames().size(),
                       { reverb->getReverb().getModifiedIR().getNumSamples() });

        std::cout << std::endl;
    };

    for (const auto& strip : processor.getMixer().getStrips())
        describeStrip (strip->getName(), strip->getEffectChain());

    describeStrip (processor.getMixer().getMasterStrip().getName(),
                   processor.getMixer().getMasterStrip().getEffectChain());

    std::cout << "Parameters  : " << processor.getParameters().size() << std::endl;
    std::cout << "Channels    : " << processor.getSampler().getNumOutputChannels() << std::endl;

    for (const auto& warning : resources.getWarnings())
        std::cout << "WARNING     : " << warning << std::endl;

    for (const auto& miss : resources.getMisses())
        std::cout << "MISSING     : " << miss << std::endl;

    for (const auto& problem : irProblems)
        std::cout << "IR          : " << problem << std::endl;

    const bool clean = resources.getMisses().isEmpty() && resources.getWarnings().isEmpty()
                           && irProblems.isEmpty();
    std::cout << (clean ? "OK" : "FAILED") << std::endl;

    return clean ? 0 : 1;
}

//==============================================================================
class FxmeSamplerDevApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "FxmeSamplerDev"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const auto arguments = getCommandLineParameterArray();

        // --check <folder>: load it, report, exit. No window, no audio device.
        if (arguments.contains ("--check"))
        {
            const int index = arguments.indexOf ("--check");
            const auto folder = index + 1 < arguments.size()
                                    ? juce::File::getCurrentWorkingDirectory().getChildFile (arguments[index + 1].unquoted())
                                    : juce::File();

            setApplicationReturnValue (checkKitFolder (folder));
            quit();
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = "FxmeSamplerDev";
        options.filenameSuffix = ".settings";
        options.folderName = "FxmeSampler";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        // A folder on the command line wins over the remembered one, so a
        // build script can open a kit directly.
        juce::File fromCommandLine;

        for (const auto& argument : arguments)
            if (juce::File::isAbsolutePath (argument.unquoted()))
                fromCommandLine = juce::File (argument.unquoted());

        juce::ignoreUnused (commandLine);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(),
                                                   *properties.getUserSettings(),
                                                   fromCommandLine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        properties.closeFiles();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::PropertiesFile& properties, const juce::File& initialKit)
            : DocumentWindow (name, juce::Colour (0xff1a1a1a), DocumentWindow::allButtons)
        {
            auto* host = new KitHostComponent (properties);

            setUsingNativeTitleBar (true);
            setContentOwned (host, true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);

            // After the window exists, so the first kit can size it.
            host->loadInitialKit (initialKit);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (FxmeSamplerDevApplication)
