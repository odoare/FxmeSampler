/*
  ==============================================================================

    SampleGroupComponent.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Sampler.h"

class SampleGroupComponent : public juce::Component
{
public:
    SampleGroupComponent (SampleGroup& group, juce::AudioProcessorValueTreeState& apvts);
    ~SampleGroupComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SampleGroup& group;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label nameLabel;
    juce::ToggleButton oneShotButton;
    
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel, detuneLabel, randomDetuneLabel, velGainLabel, groupLevelLabel, startOffsetLabel;
    fxme::FxmeSlider attackSlider, decaySlider, sustainSlider, releaseSlider, detuneSlider, randomDetuneSlider, velGainSlider, groupLevelSlider, startOffsetSlider;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<ButtonAttachment> oneShotAtt;

    void setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& text, double min, double max, double def);
    void setSliderColours (juce::Slider& s, juce::Colour c);


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleGroupComponent)
};
