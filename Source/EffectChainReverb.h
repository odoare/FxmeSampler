/*
  ==============================================================================

    EffectChainReverb.h

  ==============================================================================
*/

#pragma once

#include "EffectChain.h"
#include "ConvolReverb.h"
#include "Equalizer.h"
#include "StereoDelay.h"

class EffectChainReverb : public EffectChain
{
public:
    EffectChainReverb();
    
    juce::String getTypeName() const override { return "Reverb"; }

    void prepare (double sampleRate, int samplesPerBlock, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override;
    void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& prefix) override;
    void setBPM(double bpm) override;

    ConvolReverb& getReverb() { return reverb; }
    Equalizer& getEQ() { return eq; }
    StereoDelay& getDelay() { return delay; }
    
private:
    ConvolReverb reverb;
    Equalizer eq;
    StereoDelay delay;
};