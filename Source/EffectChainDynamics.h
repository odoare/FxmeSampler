/*
  ==============================================================================

    EffectChainDynamics.h

  ==============================================================================
*/

#pragma once

#include "EffectChain.h"
#include "Equalizer.h"
#include "Compressor.h"
#include "Transient.h"
#include "Tube.h"
#include <atomic>

class EffectChainDynamics : public EffectChain
{
public:
    EffectChainDynamics();
    
    juce::String getTypeName() const override { return "Dynamics"; }

    void prepare (double sampleRate, int samplesPerBlock, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override;
    void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& prefix) override;

    Equalizer& getEQ() { return eq; }
    Compressor& getComp() { return comp; }
    Tube& getTube() { return tube; }
    Transient& getTransient() { return trans; }

private:
    Equalizer eq;
    Compressor comp;
    Tube tube;
    Transient trans;
    std::atomic<float>* orderParam = nullptr;

    void checkParameters();
};
