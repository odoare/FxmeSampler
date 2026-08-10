/*
  ==============================================================================

    SampleGroupComponent.cpp

  ==============================================================================
*/

#include "SampleGroupComponent.h"
#include "Theme.h"

void SampleGroupComponent::setSliderColours (juce::Slider& s, juce::Colour c)
{
    fxsampler::theme::accentSlider (s, c);
}

SampleGroupComponent::SampleGroupComponent (SampleGroup& g, juce::AudioProcessorValueTreeState& state)
    : group (g), apvts (state)
{
    addAndMakeVisible (nameLabel);
    nameLabel.setText (group.getName(), juce::NotificationType::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::Font (16.0f, juce::Font::bold));

    addAndMakeVisible (oneShotButton);
    oneShotButton.setButtonText ("One Shot");
    oneShotButton.setColour (juce::ToggleButton::tickColourId, fxsampler::theme::groupAccent);
    
    // Assuming parameter naming convention: GroupName_Parameter
    juce::String prefix = group.getName() + "_";
    
    oneShotAtt = std::make_unique<ButtonAttachment> (apvts, prefix + "OneShot", oneShotButton);

    setupSlider (attackSlider, attackLabel, "Attack", 0.0, 5.0, 0.0);
    attackSlider.setAttachment(new SliderAttachment (apvts, prefix + "Attack", attackSlider));

    setupSlider (decaySlider, decayLabel, "Decay", 0.0, 5.0, 0.0);
    decaySlider.setAttachment(new SliderAttachment (apvts, prefix + "Decay", decaySlider));

    setupSlider (sustainSlider, sustainLabel, "Sustain", 0.0, 1.0, 1.0);
    sustainSlider.setAttachment(new SliderAttachment (apvts, prefix + "Sustain", sustainSlider));

    setupSlider (releaseSlider, releaseLabel, "Release", 0.0, 5.0, 0.1);
    releaseSlider.setAttachment(new SliderAttachment (apvts, prefix + "Release", releaseSlider));

    setupSlider (detuneSlider, detuneLabel, "Detune", -12.0, 12.0, 0.0);
    detuneSlider.setTextValueSuffix (" st");
    detuneSlider.setAttachment(new SliderAttachment (apvts, prefix + "Detune", detuneSlider));

    setupSlider (randomDetuneSlider, randomDetuneLabel, "Rnd Detune", 0.0, 10.0, 0.0);
    randomDetuneSlider.setTextValueSuffix (" ct");
    randomDetuneSlider.setAttachment(new SliderAttachment (apvts, prefix + "RandomDetune", randomDetuneSlider));

    setupSlider (groupLevelSlider, groupLevelLabel, "Group Level", -12.0, 6.0, 0.0);
    groupLevelSlider.setTextValueSuffix (" dB");
    groupLevelSlider.setAttachment(new SliderAttachment (apvts, prefix + "GroupLevel", groupLevelSlider));

    setupSlider (velGainSlider, velGainLabel, "Min Vel Gain", -40.0, 0.0, -40.0);
    velGainSlider.setTextValueSuffix (" dB");
    velGainSlider.setAttachment(new SliderAttachment (apvts, prefix + "MinVelGain", velGainSlider));

    // Slides this group against the others in time: positive plays it later,
    // negative eats into the head of its samples. A close mic and an ambient
    // one are the same performance about a metre apart, so a millisecond
    // either way moves them between reinforcing and cancelling.
    setupSlider (startOffsetSlider, startOffsetLabel, "Start Offset", -10.0, 10.0, 0.0);
    startOffsetSlider.setTextValueSuffix (" ms");
    startOffsetSlider.setCentralValue (0.0);   // reads from the centre, not from -10
    startOffsetSlider.setAttachment(new SliderAttachment (apvts, prefix + "StartOffset", startOffsetSlider));
}

SampleGroupComponent::~SampleGroupComponent()
{
}

void SampleGroupComponent::setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& text, double min, double max, double def)
{
    addAndMakeVisible (label);
    label.setText (text, juce::NotificationType::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (12.0f);

    addAndMakeVisible (slider);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (min, max);
    slider.setValue (def);
    slider.setTooltip (text);
    
    setSliderColours (slider, fxsampler::theme::groupAccent);
}

void SampleGroupComponent::paint (juce::Graphics& g)
{
    fxsampler::theme::paintPanelBackground (g, getLocalBounds(), fxsampler::theme::groupPanel);
}

void SampleGroupComponent::resized()
{
    auto area = getLocalBounds().reduced (5);
   using fi = juce::FlexItem;
    juce::FlexBox fbMain,fbDetune,fbRandomDetune, fbGain, fbAttack, fbDecay, fbSustain, fbRelease, fbVelGain, fbStartOffset;
    fbStartOffset.flexDirection = juce::FlexBox::Direction::column;
    fbDetune.flexDirection = juce::FlexBox::Direction::column;
    fbRandomDetune.flexDirection = juce::FlexBox::Direction::column;
    fbGain.flexDirection = juce::FlexBox::Direction::column;
    fbAttack.flexDirection = juce::FlexBox::Direction::column;
    fbDecay.flexDirection = juce::FlexBox::Direction::column;
    fbSustain.flexDirection = juce::FlexBox::Direction::column;
    fbRelease.flexDirection = juce::FlexBox::Direction::column;
    fbMain.flexDirection = juce::FlexBox::Direction::row;

    fbStartOffset.items.add(fi(startOffsetLabel).withFlex(0.2f));
    fbStartOffset.items.add(fi(startOffsetSlider).withFlex(1.f));

    fbDetune.items.add(fi(detuneLabel).withFlex(0.2f));
    fbDetune.items.add(fi(detuneSlider).withFlex(1.f));

    fbGain.flexDirection = juce::FlexBox::Direction::column;
    fbGain.items.add(fi(groupLevelLabel).withFlex(0.2f));
    fbGain.items.add(fi(groupLevelSlider).withFlex(1.f));

    fbRandomDetune.items.add(fi(randomDetuneLabel).withFlex(0.2f));
    fbRandomDetune.items.add(fi(randomDetuneSlider).withFlex(1.f));

    fbVelGain.flexDirection = juce::FlexBox::Direction::column;
    fbVelGain.items.add(fi(velGainLabel).withFlex(0.2f));
    fbVelGain.items.add(fi(velGainSlider).withFlex(1.f));

    fbAttack.items.add(fi(attackLabel).withFlex(0.2f));
    fbAttack.items.add(fi(attackSlider).withFlex(1.f));

    fbDecay.items.add(fi(decayLabel).withFlex(0.2f));
    fbDecay.items.add(fi(decaySlider).withFlex(1.f));

    fbSustain.items.add(fi(sustainLabel).withFlex(0.2f));
    fbSustain.items.add(fi(sustainSlider).withFlex(1.f));

    fbRelease.items.add(fi(releaseLabel).withFlex(0.2f));
    fbRelease.items.add(fi(releaseSlider).withFlex(1.f));

    fbMain.items.add(fi(nameLabel).withFlex(1.f));
    fbMain.items.add(fi(fbStartOffset).withFlex(.6f));
    fbMain.items.add(fi(fbDetune).withFlex(.6f));
    fbMain.items.add(fi(fbRandomDetune).withFlex(.6f));
    fbMain.items.add(fi(fbGain).withFlex(.6f));
    fbMain.items.add(fi(fbVelGain).withFlex(.6f));
    fbMain.items.add(fi(oneShotButton).withFlex(.5f));
    fbMain.items.add(fi(fbAttack).withFlex(0.6f));    
    fbMain.items.add(fi(fbDecay).withFlex(0.6f));
    fbMain.items.add(fi(fbSustain).withFlex(0.6f));
    fbMain.items.add(fi(fbRelease).withFlex(0.6f));

    fbMain.performLayout (area);
}
