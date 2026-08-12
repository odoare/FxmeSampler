/*
  ==============================================================================

    EffectChainDelay.h

  ==============================================================================
*/

#pragma once

#include "EffectChain.h"
#include "StereoDelay.h"

class EffectChainDelay : public EffectChain
{
public:
    EffectChainDelay();
    
    juce::String getTypeName() const override { return "Delay"; }

    void prepare (double sampleRate, int samplesPerBlock, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override;
    void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& prefix) override;
    void setBPM(double bpm) override;

    StereoDelay& getDelay() { return delay; }
    
private:
    StereoDelay delay;
};