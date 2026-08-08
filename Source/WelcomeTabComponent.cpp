/*
  ==============================================================================

    WelcomeTabComponent.cpp

  ==============================================================================
*/

#include "WelcomeTabComponent.h"
#include "Theme.h"

WelcomeTabComponent::WelcomeTabComponent(const juce::String& text, const juce::Image& image, fxme::PresetManager& presetManager)
    : text(text), img(image), presetComp(presetManager)
{
    presetComp.setAccentColour (fxsampler::theme::presetAccent);
    addAndMakeVisible (presetComp);
}

WelcomeTabComponent::~WelcomeTabComponent()
{
}

void WelcomeTabComponent::computeAreas (juce::Rectangle<int>& welcomeArea, juce::Rectangle<int>& presetArea) const
{
    auto area = getLocalBounds().reduced (10);

    // The browser needs a usable width for its list and buttons, but must not
    // swallow a narrow window. removeFromRight clamps on its own if the
    // requested width exceeds what is left.
    const int presetWidth = juce::jlimit (fxsampler::theme::welcomePresetMinWidth,
                                         fxsampler::theme::welcomePresetMaxWidth,
                                         juce::roundToInt (area.getWidth() * fxsampler::theme::welcomePresetWidthRatio));

    presetArea = area.removeFromRight (presetWidth);
    area.removeFromRight (10);   // gap between the two halves
    welcomeArea = area;
}

void WelcomeTabComponent::paint(juce::Graphics& g)
{
    g.setColour(fxsampler::theme::panel);
    g.fillAll();

    juce::Rectangle<int> welcomeArea, presetArea;
    computeAreas (welcomeArea, presetArea);

    auto area = welcomeArea.toFloat();

    if (img.isValid())
    {
        auto imgArea = area.removeFromTop(area.getHeight() * fxsampler::theme::welcomeImageHeightRatio);
        g.drawImage(img, imgArea, juce::RectanglePlacement::centred);
    }

    g.setColour(fxsampler::theme::text);
    g.setFont(fxsampler::theme::welcomeTextHeight);
    g.drawFittedText(text, area.toNearestInt(), juce::Justification::centred, 10);
}

void WelcomeTabComponent::resized()
{
    juce::Rectangle<int> welcomeArea, presetArea;
    computeAreas (welcomeArea, presetArea);

    presetComp.setBounds (presetArea);
}
