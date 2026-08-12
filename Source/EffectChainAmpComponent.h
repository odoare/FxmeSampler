/*
  ==============================================================================

    EffectChainAmpComponent.h

  ==============================================================================
*/

#pragma once

#include "EffectChainComponent.h"
#include "EffectChainAmp.h"
#include "EqualizerComponent.h"
#include "CompressorComponent.h"
#include "TubeComponent.h"
#include "CabComponent.h"

class EffectChainAmpComponent : public EffectChainComponent
{
public:
    EffectChainAmpComponent (EffectChainAmp& chain, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix);
    void resized() override;

private:
    EffectChainAmp& chain;
    EqualizerComponent eqComp;
    CompressorComponent compComp;
    TubeComponent tubeComp;
    CabComponent cabComp;

    juce::ComboBox orderBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> orderAtt;
};
