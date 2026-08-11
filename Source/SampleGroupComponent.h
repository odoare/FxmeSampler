/*
  ==============================================================================

    SampleGroupComponent.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Sampler.h"

class SampleGroupComponent : public juce::Component,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater
{
public:
    SampleGroupComponent (SampleGroup& group, juce::AudioProcessorValueTreeState& apvts);
    ~SampleGroupComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SampleGroup& group;
    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramPrefix;

    juce::Label nameLabel;
    juce::ToggleButton oneShotButton, loopButton;
    juce::ComboBox releaseModeBox;

    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel, detuneLabel, randomDetuneLabel, velGainLabel, groupLevelLabel, startOffsetLabel, crossfadeLabel, releaseModeLabel;
    fxme::FxmeSlider attackSlider, decaySlider, sustainSlider, releaseSlider, detuneSlider, randomDetuneSlider, velGainSlider, groupLevelSlider, startOffsetSlider, crossfadeSlider;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ButtonAttachment> oneShotAtt, loopAtt;
    std::unique_ptr<ComboBoxAttachment> releaseModeAtt;

    /** Greys the controls a group's current mode makes inert, so the panel says
        what is actually live rather than offering knobs that do nothing.

        Message thread only. Reached from parameterChanged, which can arrive on
        the audio thread, via the AsyncUpdater. */
    void updateEnablement();
    void setLive (juce::Component& c, bool live);
    float paramValue (const juce::String& id) const;

    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;

    void setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& text, double min, double max, double def);
    void setSliderColours (juce::Slider& s, juce::Colour c);


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleGroupComponent)
};
