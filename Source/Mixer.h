/*
  ==============================================================================

    Mixer.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MixerStrips.h"

//==============================================================================
/**
 * @class Mixer
 * @brief Manages the collection of mixer strips and the master output.
 *
 * Threading: the strip list is built once by loadFromXml/assignParameters from
 * the processor constructor, before the processor is live, and is only read
 * afterwards. prepare() runs from prepareToPlay, which the host guarantees not
 * to overlap with processBlock. processBlock therefore runs lock-free.
 *
 * If strips ever need rebuilding at runtime, do NOT reintroduce a lock around
 * processBlock: publish the new list with an atomic swap (or a message-thread
 * suspendProcessing) instead, so the audio thread never blocks.
 */
class Mixer
{
public:
    /** @brief Default constructor. */
    Mixer() = default;

    /**
     * @brief Prepares the mixer and all strips for playback.
     * @param sampleRate The sample rate.
     * @param samplesPerBlock The expected number of samples per block.
     */
    void prepare (double sampleRate, int samplesPerBlock);

    /**
     * @brief Processes a block of audio through the mixer.
     * @param inputBuffer The input buffer from the sampler.
     * @param outputBuffer The final output buffer to the host.
     */
    void processBlock (const juce::AudioBuffer<float>& inputBuffer, juce::AudioBuffer<float>& outputBuffer);

    /**
     * @brief Loads mixer configuration from XML data.
     * @param xmlData Pointer to XML data.
     * @param xmlSize Size of XML data.
     */
    void loadFromXml (const void* xmlData, int xmlSize);

    /**
     * @brief Assigns APVTS parameters to all strips.
     * @param apvts The AudioProcessorValueTreeState.
     */
    void assignParameters (juce::AudioProcessorValueTreeState& apvts);

    /**
     * @brief Adds parameters from all strips to the layout builder.
     * @param params Vector to add parameters to.
     */
    void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);

    /**
     * @brief Sets the BPM for the mixer.
     * @param bpm Beats per minute.
     */
    void setBPM(double bpm);

    /** @brief Gets the list of mixer strips. */
    const std::vector<std::unique_ptr<MixerStrip>>& getStrips() const { return strips; }

    /** @brief Gets the master strip. */
    MasterStrip& getMasterStrip() { return masterStrip; }
    
    /** @brief Gets the welcome text. */
    juce::String getWelcomeText() const { return welcomeText; }

    /** @brief Gets the welcome image. */
    juce::Image getWelcomeImage() const { return welcomeImage; }

private:
    std::vector<std::unique_ptr<MixerStrip>> strips;
    MasterStrip masterStrip { "Master" };
    juce::AudioBuffer<float> mixBuffer;
    double currentSampleRate = 44100.0;
    int currentSamplesPerBlock = 512;
    
    juce::String welcomeText;
    juce::Image welcomeImage;
};