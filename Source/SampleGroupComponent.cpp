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
    paramPrefix = prefix;

    oneShotAtt = std::make_unique<ButtonAttachment> (apvts, prefix + "OneShot", oneShotButton);

    addAndMakeVisible (loopButton);
    loopButton.setButtonText ("Loop");
    loopButton.setColour (juce::ToggleButton::tickColourId, fxsampler::theme::groupAccent);
    loopButton.setTooltip ("Repeat loopStart..loopEnd while the note is held. "
                           "Ignored by a one-shot group.");
    loopAtt = std::make_unique<ButtonAttachment> (apvts, prefix + "Loop", loopButton);

    addAndMakeVisible (releaseModeLabel);
    releaseModeLabel.setText ("Release Mode", juce::NotificationType::dontSendNotification);
    releaseModeLabel.setJustificationType (juce::Justification::centred);
    releaseModeLabel.setFont (12.0f);

    addAndMakeVisible (releaseModeBox);
    // Item ids are 1-based; the order must match the choice parameter.
    releaseModeBox.addItem ("Loop", 1);
    releaseModeBox.addItem ("Region", 2);
    releaseModeBox.setTooltip ("Loop: keep looping and let the Release knob fade it out. "
                               "Region: play the recorded tail from releaseStart instead, "
                               "which leaves the Release knob unused.");
    releaseModeAtt = std::make_unique<ComboBoxAttachment> (apvts, prefix + "ReleaseMode", releaseModeBox);

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

    // Smooths the loop seam, and doubles as the length of the jump into the
    // release region, where it gets a 5 ms floor.
    setupSlider (crossfadeSlider, crossfadeLabel, "Crossfade", 0.0, 500.0, 0.0);
    crossfadeSlider.setTextValueSuffix (" ms");
    crossfadeSlider.setAttachment(new SliderAttachment (apvts, prefix + "Crossfade", crossfadeSlider));

    // One-shot, loop and release mode between them decide which of these
    // controls do anything, and any of the three can move from the host.
    for (auto* id : { "OneShot", "Loop", "ReleaseMode" })
        apvts.addParameterListener (paramPrefix + id, this);

    updateEnablement();
}

SampleGroupComponent::~SampleGroupComponent()
{
    for (auto* id : { "OneShot", "Loop", "ReleaseMode" })
        apvts.removeParameterListener (paramPrefix + id, this);

    cancelPendingUpdate();
}

float SampleGroupComponent::paramValue (const juce::String& id) const
{
    if (auto* raw = apvts.getRawParameterValue (id))
        return raw->load();

    return 0.0f;
}

void SampleGroupComponent::setLive (juce::Component& c, bool live)
{
    c.setEnabled (live);
    // The look and feel draws knobs the same either way, so setEnabled alone
    // would leave a dead control looking live. Alpha is what actually shows it.
    c.setAlpha (live ? 1.0f : 0.4f);
}

void SampleGroupComponent::updateEnablement()
{
    const bool oneShot = paramValue (paramPrefix + "OneShot") > 0.5f;
    const bool loops = ! oneShot && paramValue (paramPrefix + "Loop") > 0.5f;
    const bool region = paramValue (paramPrefix + "ReleaseMode") > 0.5f;

    // A one-shot ignores the loop points entirely.
    setLive (loopButton, ! oneShot);

    // Both fades belong to the loop: no loop, nothing to fade.
    setLive (crossfadeSlider, loops);
    setLive (crossfadeLabel, loops);
    setLive (releaseModeBox, loops);
    setLive (releaseModeLabel, loops);

    // In Region mode the envelope holds at sustain and the recorded tail does
    // the decay, so the Release knob really is unused. Saying so here is the
    // difference between a deliberate design and an apparent bug.
    const bool releaseKnobLive = ! (loops && region);
    setLive (releaseSlider, releaseKnobLive);
    setLive (releaseLabel, releaseKnobLive);
}

void SampleGroupComponent::parameterChanged (const juce::String&, float)
{
    // Can arrive on the audio thread, so do nothing here but ask for a callback
    // on the message thread.
    triggerAsyncUpdate();
}

void SampleGroupComponent::handleAsyncUpdate()
{
    updateEnablement();
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
    juce::FlexBox fbMain,fbDetune,fbRandomDetune, fbGain, fbAttack, fbDecay, fbSustain, fbRelease, fbVelGain, fbStartOffset, fbCrossfade, fbReleaseMode;
    fbCrossfade.flexDirection = juce::FlexBox::Direction::column;
    fbReleaseMode.flexDirection = juce::FlexBox::Direction::column;
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

    fbCrossfade.items.add(fi(crossfadeLabel).withFlex(0.2f));
    fbCrossfade.items.add(fi(crossfadeSlider).withFlex(1.f));

    // A combo box has no business filling the height a knob does, so cap it and
    // let it sit under its label.
    fbReleaseMode.items.add(fi(releaseModeLabel).withFlex(0.2f));
    fbReleaseMode.items.add(fi(releaseModeBox).withFlex(1.f).withMaxHeight(26.f)
                                              .withMargin(fi::Margin(2.f, 4.f, 0.f, 4.f)));

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

    // The loop controls stay together at the end, after the envelope they
    // interact with.
    fbMain.items.add(fi(loopButton).withFlex(.4f));
    fbMain.items.add(fi(fbCrossfade).withFlex(0.6f));
    fbMain.items.add(fi(fbReleaseMode).withFlex(0.8f));

    fbMain.performLayout (area);
}
