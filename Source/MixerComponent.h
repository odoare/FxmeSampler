/*
  ==============================================================================

    MixerComponent.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Mixer.h"
#include "MixerStripComponents.h"
#include "SamplerComponent.h"

//==============================================================================
/**
 * @class MixerComponent
 * @brief The main component containing the mixer interface.
 */
class MixerComponent : public juce::Component
{
public:
    /**
     * @brief Constructor.
     * @param mixer The Mixer instance.
     * @param sampler The Sampler instance.
     * @param apvts The APVTS.
     */
    MixerComponent (Mixer& mixer, Sampler& sampler, juce::AudioProcessorValueTreeState& apvts,
                    fxme::PresetManager& presetManager);
    ~MixerComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Mixer& mixer;
    juce::AudioProcessorValueTreeState& apvts;
    juce::TabbedComponent tabs;
    juce::TooltipWindow tooltipWindow {nullptr, 50};

    // Dynamic Levels Component
    class LevelsComponent : public juce::Component
    {
    public:
        LevelsComponent (Mixer& m, juce::AudioProcessorValueTreeState& apvts);
        void resized() override;
        
    private:
        Mixer& mixer;
        std::vector<std::unique_ptr<StripComponent>> strips;
    };
    
    LevelsComponent levelsComp;
    SamplerComponent samplerComp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};