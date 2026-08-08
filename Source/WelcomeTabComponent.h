/*
  ==============================================================================

    WelcomeTabComponent.h

    The kit's welcome page: artwork and blurb from mapping.xml on the left, the
    preset browser on the right. It replaced a pair of Save/Load buttons that
    wrote loose XML files through a FileChooser, which predated
    fxme::PresetManager and bypassed it.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class WelcomeTabComponent : public juce::Component
{
public:
    WelcomeTabComponent(const juce::String& text, const juce::Image& image, fxme::PresetManager& presetManager);
    ~WelcomeTabComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::String text;
    juce::Image img;
    fxme::PresetComponent presetComp;

    /** Splits the content between the welcome artwork and the preset browser.
        One function so paint() and resized() cannot drift apart. */
    void computeAreas (juce::Rectangle<int>& welcomeArea, juce::Rectangle<int>& presetArea) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WelcomeTabComponent)
};
