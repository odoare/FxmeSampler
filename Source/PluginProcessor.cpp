/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FxmeSamplerAudioProcessor::FxmeSamplerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                       .withOutput ("Main Output", juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Aux 1", juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Aux 2", juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Aux 3", juce::AudioChannelSet::stereo(), true)
                       ),
       apvts (*this, nullptr, "Parameters", createParameterLayout()),
       presetManager (apvts,
                      fxme::PresetManager::getDefaultUserPresetDirectory ("FxmeSampler", JucePlugin_Name),
                      BinaryData::namedResourceList,
                      BinaryData::namedResourceListSize,
                      BinaryData::getNamedResource)
#else
     : apvts (*this, nullptr, "Parameters", createParameterLayout()),
       presetManager (apvts,
                      fxme::PresetManager::getDefaultUserPresetDirectory ("FxmeSampler", JucePlugin_Name),
                      BinaryData::namedResourceList,
                      BinaryData::namedResourceListSize,
                      BinaryData::getNamedResource)
#endif
{
    // Load the mapping XML from BinaryData
    int xmlSize = 0;
    const char* xmlData = BinaryData::getNamedResource ("mapping_xml", xmlSize);
    
    if (xmlData != nullptr) {
        sampler.loadSamplesFromXml (xmlData, xmlSize);
        sampler.assignParameters (apvts);
        mixer.loadFromXml (xmlData, xmlSize);
        mixer.assignParameters (apvts);
    }

    // Factory presets are discovered by presetManager: it keeps every embedded
    // "*_xml" resource whose root tag matches the APVTS state type, so
    // mapping_xml (root <Mappings>) is skipped without special-casing it.
}

FxmeSamplerAudioProcessor::~FxmeSamplerAudioProcessor()
{
}

//==============================================================================
const juce::String FxmeSamplerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FxmeSamplerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FxmeSamplerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FxmeSamplerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double FxmeSamplerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

// The host's program menu is a view onto the factory bank, so that it and the
// editor's PresetComponent always agree on what is loaded. User presets are
// deliberately not exposed here: the program list has to be fixed in size, and
// the user bank changes at runtime.
int FxmeSamplerAudioProcessor::getNumPrograms()
{
    return juce::jmax (1, (int) presetManager.getFactoryPresets().size());
}

int FxmeSamplerAudioProcessor::getCurrentProgram()
{
    // -1 means the current state is a user preset or an unsaved edit; the host
    // needs a valid index, so report the first program.
    return juce::jmax (0, presetManager.getCurrentFactoryIndex());
}

void FxmeSamplerAudioProcessor::setCurrentProgram (int index)
{
    presetManager.loadFactoryPreset (index);
}

const juce::String FxmeSamplerAudioProcessor::getProgramName (int index)
{
    const auto& factory = presetManager.getFactoryPresets();

    if (juce::isPositiveAndBelow (index, (int) factory.size()))
        return factory[(size_t) index].name;

    return "Default";
}

void FxmeSamplerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    // Factory presets are embedded in the binary and cannot be renamed. Users
    // rename their own presets through the editor's preset browser.
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void FxmeSamplerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0 || samplesPerBlock <= 0)
        return;

    sampler.prepareToPlay(sampleRate, samplesPerBlock);
    mixer.prepare(sampleRate, samplesPerBlock);
    samplerOutputBuffer.setSize(sampler.getNumOutputChannels(), samplesPerBlock);
    lastBPM = -1.0;
}

void FxmeSamplerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FxmeSamplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
         return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void FxmeSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Clear the main buffer because the sampler adds to it
    for (int i = 0; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Resize buffer if needed (e.g. if XML loaded after prepareToPlay).
    // avoidReallocating=true keeps the audio thread allocation-free: the buffer
    // was sized to the worst case in prepareToPlay, so this should be a no-op
    // unless the host changes block size mid-session.
    if (samplerOutputBuffer.getNumChannels() != sampler.getNumOutputChannels() || samplerOutputBuffer.getNumSamples() != buffer.getNumSamples())
        samplerOutputBuffer.setSize(sampler.getNumOutputChannels(), buffer.getNumSamples(),
                                    /*keepExistingContent*/ false,
                                    /*clearExtraSpace*/     false,
                                    /*avoidReallocating*/   true);

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (pos->getBpm().hasValue())
                bpm = *pos->getBpm();
    }
    if (std::abs(bpm - lastBPM) > 0.0001)
    {
        lastBPM = bpm;
        mixer.setBPM(bpm);
    }

    samplerOutputBuffer.clear();
    
    sampler.updateParams();
    sampler.processBlock(samplerOutputBuffer, midiMessages);
    mixer.processBlock(samplerOutputBuffer, buffer);
}

//==============================================================================
bool FxmeSamplerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* FxmeSamplerAudioProcessor::createEditor()
{
    return new FxmeSamplerAudioProcessorEditor (*this);
}

//==============================================================================
// Version of the saved-state format, written as an attribute on the state root
// so a future format change has something to branch on in setStateInformation.
// State written before versioning existed carries no attribute and reads as 0.
static constexpr int currentStateVersion = 1;

void FxmeSamplerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
    {
        xml->setAttribute ("version", currentStateVersion);

        destData.setSize (0);
        juce::MemoryOutputStream stream (destData, false);
        xml->writeTo (stream);
    }
}

void FxmeSamplerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (juce::XmlDocument::parse (juce::String::createStringFromData (data, sizeInBytes)));

    if (xmlState == nullptr || ! xmlState->hasTagName (apvts.state.getType()))
        return;

    // 0 covers both sessions saved before versioning and the embedded factory
    // presets, which are plain APVTS dumps. Nothing to migrate yet; when the
    // format changes, branch here before replaceState.
    const int version = xmlState->getIntAttribute ("version", 0);
    juce::ignoreUnused (version);

    apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}


//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FxmeSamplerAudioProcessor();
}

juce::AudioProcessorValueTreeState::ParameterLayout FxmeSamplerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    int xmlSize = 0;
    const char* xmlData = BinaryData::getNamedResource ("mapping_xml", xmlSize);

    if (xmlData != nullptr && xmlSize > 0)
    {
        // Create temporary Sampler and Mixer to parse XML and generate parameters
        Sampler tempSampler;
        Mixer tempMixer;
        tempSampler.loadSamplesFromXml (xmlData, xmlSize);
        tempMixer.loadFromXml (xmlData, xmlSize);
        
        tempSampler.addParameters (params);
        tempMixer.addParameters (params);
    }

    return { params.begin(), params.end() };
}
