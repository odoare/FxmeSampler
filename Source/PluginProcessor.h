/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Sampler.h"
#include "Mixer.h"

//==============================================================================
/**
 * @class FxmeSamplerAudioProcessor
 * @brief The main AudioProcessor class for the FxmeSampler plugin.
*/
class FxmeSamplerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    /** Constructor. */
    FxmeSamplerAudioProcessor();
    /** Destructor. */
    ~FxmeSamplerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /**
     * @brief Gets the Sampler instance.
     * @return Reference to the Sampler.
     */
    Sampler& getSampler() { return sampler; }

    /**
     * @brief Gets the Mixer instance.
     * @return Reference to the Mixer.
     */
    Mixer& getMixer() { return mixer; }

    /**
     * @brief Gets the AudioProcessorValueTreeState.
     * @return Reference to the APVTS.
     */
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    /**
     * @brief Gets the preset manager (factory presets from BinaryData, user
     *        presets on disk). Shared with the editor's PresetComponent.
     */
    fxme::PresetManager& getPresetManager() noexcept { return presetManager; }

private:
    //==============================================================================
    Sampler sampler;
    Mixer mixer;
    juce::AudioProcessorValueTreeState apvts;
    // Declared after apvts: members are constructed in declaration order and
    // the manager takes a reference to the state.
    fxme::PresetManager presetManager;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioBuffer<float> samplerOutputBuffer;

    double lastBPM = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxmeSamplerAudioProcessor)
};
