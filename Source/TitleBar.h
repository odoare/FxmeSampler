/*
  ==============================================================================

    TitleBar.h

    Small centred title strip used as the name plate at the top of each mixer
    strip. It was previously fxme::TitleBar in the FxmeJuceTools module; that
    module was replaced by FxmeTools, whose equivalent (fxme::TopBar) is the
    full plugin header bar (logo, version, blurb) rather than a plain label,
    so this stays here as a plugin-local component.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class TitleBar : public juce::Component
{
public:
    TitleBar() = default;

    TitleBar (juce::String tit, juce::Colour fontCol, juce::Colour barCol)
        : fontColour (fontCol), barColour (barCol), title (std::move (tit))
    {
    }

    ~TitleBar() override = default;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (barColour.darker());
        g.setColour (fontColour);
        g.setFont (18.0f);
        g.drawText (title, getLocalBounds(), juce::Justification::centred);
    }

    void resized() override
    {
        repaint();
    }

    void setTitle (juce::String tit)      { title = std::move (tit); }
    void setFontColour (juce::Colour col) { fontColour = col; }
    void setBarColour (juce::Colour col)  { barColour = col; }

private:
    juce::Colour fontColour { juce::Colours::white }, barColour { juce::Colours::grey };
    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitleBar)
};
