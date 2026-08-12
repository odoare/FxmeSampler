/*
  ==============================================================================

    EffectChain.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class EffectChain
{
public:
    virtual ~EffectChain() = default;

    /** The name this chain answers to in a mapping's effectChain= attribute.
        Reported by the dev host's --check so a mapping can be compared against
        what it actually built. */
    virtual juce::String getTypeName() const = 0;

    virtual void prepare (double sampleRate, int samplesPerBlock, int numChannels) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer) = 0;
    virtual void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) = 0;
    virtual void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& prefix) = 0;
    virtual void setBPM(double bpm) {}
};
