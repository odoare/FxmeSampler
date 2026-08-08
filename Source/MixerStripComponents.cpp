/*
  ==============================================================================

    MixerStripComponents.cpp

  ==============================================================================
*/

#include "MixerStripComponents.h"
#include "Theme.h"

//==============================================================================
// StripComponent Base
StripComponent::StripComponent (MixerStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : strip (s)
{

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::ambisonicStripDefault;

    addAndMakeVisible (nameBar);
    nameBar.setTitle(strip.getName());
    nameBar.setBarColour(c);

    addAndMakeVisible (levelSlider);
    levelSlider.setSliderStyle (juce::Slider::LinearBarVertical);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 15);
    levelSlider.setRange (-60.0, 6.0);
    levelSlider.setValue (0.0);
    levelSlider.setTextValueSuffix ("dB");
    levelSlider.setAttachment(new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, strip.getName() + "_Level", levelSlider));

    addAndMakeVisible (muteButton);
    muteButton.setButtonText ("M");
    muteButton.setColour(juce::ToggleButton::tickColourId, fxsampler::theme::muteAccent);
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, strip.getName() + "_Mute", muteButton);
    
    if (strip.isSoloable())
    {
        addAndMakeVisible (soloButton);
        soloButton.setButtonText ("S");
        soloButton.setColour(juce::ToggleButton::tickColourId, fxsampler::theme::soloAccent);
        soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, strip.getName() + "_Solo", soloButton);
    }

    addAndMakeVisible (icon);
    icon.setImage (strip.getImage());

    // Create Send Sliders
    for (const auto& send : strip.getSends())
    {
        auto slider = std::make_unique<fxme::FxmeSlider>();
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider->setRange (-60.0, 6.0);
        slider->setTooltip ("Send to " + send.busName);
        setSliderColours (*slider, fxsampler::theme::sendAccent);
        slider->setAttachment(new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, strip.getName() + "_Send_" + send.busName, *slider));
        addAndMakeVisible (*slider);
        sendSliders.push_back (std::move (slider));

        auto button = std::make_unique<juce::ToggleButton>();
        button->setButtonText("Pre");
        button->setTooltip("Pre/Post FX+Fader");
        button->setColour(juce::ToggleButton::tickColourId, fxsampler::theme::routeAccent);
        addAndMakeVisible(*button);
        prePostAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, strip.getName() + "_Send_" + send.busName + "_Pre", *button));
        prePostButtons.push_back(std::move(button));
    }

    createRouteButtons(apvts);

    addAndMakeVisible (meterL); meterL.setMeterColor (c);
    addAndMakeVisible (meterR); meterR.setMeterColor (c);
    meterL.setRange (-60.0f, 6.0f); meterL.setZeroLevel (0.0f);
    meterR.setRange (-60.0f, 6.0f); meterR.setZeroLevel (0.0f);

    startTimerHz (24);
}

StripComponent::~StripComponent() { stopTimer(); }

void StripComponent::paint (juce::Graphics& g)
{
    fxsampler::theme::paintPanelBackground (g, getLocalBounds(), fxsampler::theme::background, 2);
}

void StripComponent::resized()
{
    // Now overidden
}

void StripComponent::layoutBottomRow(juce::FlexBox& bottomBox, juce::FlexBox& metersBox)
{
    using fi = juce::FlexItem;

    fbButtonsMeters.items.clear();
    fbRoutes.items.clear();
    fbSends.items.clear();
    fbOutputs.items.clear();

    fbButtonsMeters.flexDirection = juce::FlexBox::Direction::column;
    fbRoutes.flexDirection = juce::FlexBox::Direction::column;
    fbSends.flexDirection = juce::FlexBox::Direction::column;
    fbOutputs.flexDirection = juce::FlexBox::Direction::column;

    // Add sends
    for (size_t i = 0; i < sendSliders.size(); ++i)
    {
        fbSends.items.add(fi(*sendSliders[i]).withFlex(1.0f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0.f, -5.f)));
        fbSends.items.add(fi(*prePostButtons[i]).withFlex(0.3f).withMargin(juce::FlexItem::Margin(0.f, -1.f, 0.f, -1.f)));
    }
    fbSends.items.add(fi().withFlex(1.f));

    // Add routing buttons
    for (auto& b : routeButtons)
        fbOutputs.items.add(fi(*b).withFlex(1.0f));

    fbRoutes.items.add(fi(fbSends).withFlex(1.f));
    fbRoutes.items.add(fi(fbOutputs).withFlex(.5f));

    fbButtonsMeters.items.add(fi(muteButton).withFlex(0.2f));
    if (strip.isSoloable())
        fbButtonsMeters.items.add(fi(soloButton).withFlex(0.2f));
    else
        fbButtonsMeters.items.add(fi().withFlex(0.2f)); // Spacer
    fbButtonsMeters.items.add(fi(metersBox).withFlex(1.f));

    bottomBox.items.add(fi(levelSlider).withFlex(.6f).withMargin(juce::FlexItem::Margin(2.f, 2.f, 2.f, 2.f)));
    bottomBox.items.add(fi(fbRoutes).withFlex(1.f).withMargin(juce::FlexItem::Margin(2.f, 1.f, 2.f, 1.f)));
    bottomBox.items.add(fi(fbButtonsMeters).withFlex(.8f).withMargin(2.f));
}

void StripComponent::timerCallback()
{
    updateMeters();
}

void StripComponent::setupKnob (fxme::FxmeSlider& s, const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts)
{
    addAndMakeVisible (s);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    // Attachments will handle range
    // But we can set default range just in case
    s.setRange (-1.0, 1.0); 
    // Store attachment in subclass
    s.setAttachment(new juce::AudioProcessorValueTreeState::SliderAttachment(apvts, paramID, s));
}

void StripComponent::setSliderColours (juce::Slider& s, juce::Colour c)
{
    fxsampler::theme::accentSlider (s, c);
}

void StripComponent::setStripColor(juce::Colour c)
{
    setSliderColours(levelSlider, c);
    for (auto& s : sendSliders)
        setSliderColours(*s, c);
    for (auto& b : prePostButtons)
        b->setColour(juce::ToggleButton::tickColourId, c);
    for (auto& b : routeButtons)
        b->setColour(juce::ToggleButton::tickColourId, c);
}

void StripComponent::createRouteButtons(juce::AudioProcessorValueTreeState& apvts)
{
    for (int i = 0; i < 4; ++i)
    {
        auto btn = std::make_unique<juce::ToggleButton>();
        juce::String name = (i == 0) ? "Main" : "Aux " + juce::String(i);
        juce::String paramId = (i == 0) ? strip.getName() + "_Route_Main" : strip.getName() + "_Route_Aux" + juce::String(i);
        
        btn->setButtonText(juce::String(i));
        btn->setTooltip("Route to " + name);
        btn->setColour(juce::ToggleButton::tickColourId, fxsampler::theme::routeAccent);
        addAndMakeVisible(*btn);
        
        // Master strip doesn't have these params, so check if param exists
        if (apvts.getParameter(paramId) != nullptr)
            routeAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, paramId, *btn));
        
        routeButtons.push_back(std::move(btn));
    }
}

//==============================================================================
AmbisonicStripComponent::AmbisonicStripComponent (AmbisonicStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), ambStrip (s)
{
    setupKnob (azSlider, s.getName() + "_Azimuth", apvts);
    azSlider.setTooltip ("MS microphone Azimuth (=Pan)");
    azSlider.setCentralValue(0.f);
    
    setupKnob (elSlider, s.getName() + "_Elevation", apvts);
    elSlider.setTooltip ("MS microphone elevation");
    
    setupKnob (wSlider, s.getName() + "_Width", apvts);
    wSlider.setTooltip ("MS mix width");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::ambisonicStripDefault;

    setStripColor(c);
    setSliderColours (azSlider, c);
    setSliderColours (elSlider, c);
    setSliderColours (wSlider, c);

}

void AmbisonicStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs, fbKnobsRow1;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::column;
    fbKnobsRow1.flexDirection = juce::FlexBox::Direction::row;

    fbKnobsRow1.items.add(fi(wSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));
    fbKnobsRow1.items.add(fi(elSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));
    fbKnobs.items.add(fi(fbKnobsRow1).withFlex(1.f));
    fbKnobs.items.add(fi(azSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, 10.f, 0, 10.f)));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(1.f, 1.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(1.f, 0, 0, 1.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    fbMain.items.add(fi(icon).withFlex(0.2f));
    fbMain.items.add(fi(fbKnobs).withFlex(0.5f).withMargin(juce::FlexItem::Margin(5.f, 0.f, 0.f, 0.f)));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void AmbisonicStripComponent::updateMeters()
{
    meterL.setValue (ambStrip.meterL.getRMS());
    meterR.setValue (ambStrip.meterR.getRMS());
}

//==============================================================================
AmbisonicMonoStripComponent::AmbisonicMonoStripComponent (AmbisonicMonoStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), amStrip (s)
{
    setupKnob (azSlider, s.getName() + "_Azimuth", apvts);
    azSlider.setTooltip ("MS microphone Azimuth (=Pan)");
    azSlider.setCentralValue(0.f);
    
    setupKnob (elSlider, s.getName() + "_Elevation", apvts);
    elSlider.setTooltip ("MS microphone Elevation");
    
    setupKnob (wSlider, s.getName() + "_Width", apvts);
    wSlider.setTooltip ("MS mix Width");

    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setTooltip ("Mono proximity mic Pan");
    panSlider.setCentralValue(0.f);

    setupKnob (mixSlider, s.getName() + "_Mix", apvts);
    mixSlider.setTooltip ("Mix (Ambix <-> Mono)");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::ambisonicStripDefault; // Use same default as Ambisonic

    setStripColor(c);
    setSliderColours (azSlider, c);
    setSliderColours (elSlider, c);
    setSliderColours (wSlider, c);
    setSliderColours (panSlider, c);
    setSliderColours (mixSlider, c);
}

void AmbisonicMonoStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs, fbKnobsRow1, fbKnobsRow2, fbKnobsRow3;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::column;
    fbKnobsRow1.flexDirection = juce::FlexBox::Direction::row;
    fbKnobsRow2.flexDirection = juce::FlexBox::Direction::row;
    fbKnobsRow3.flexDirection = juce::FlexBox::Direction::row;

    // Row 1: Width, Elevation
    fbKnobsRow1.items.add(fi(wSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(-5.f, -5.f, -5.f, -5.f)));
    fbKnobsRow1.items.add(fi(elSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(-5.f, -5.f, -5.f, -5.f)));
    
    // Row 2: Azimuth, Pan
    fbKnobsRow2.items.add(fi(azSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(-5.f, -5.f, -5.f, -5.f)));
    fbKnobsRow2.items.add(fi(panSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(-5.f, -5.f, -5.f, -5.f)));

    // Row 3: Mix
    fbKnobsRow3.items.add(fi(mixSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(-5.f, 20.f, -5.f, 20.f)));

    fbKnobs.items.add(fi(fbKnobsRow1).withFlex(1.f));
    fbKnobs.items.add(fi(fbKnobsRow2).withFlex(1.f));
    fbKnobs.items.add(fi(fbKnobsRow3).withFlex(1.f));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(1.f, 1.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(1.f, 0, 0, 1.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    fbMain.items.add(fi(icon).withFlex(0.2f));
    fbMain.items.add(fi(fbKnobs).withFlex(0.5f).withMargin(juce::FlexItem::Margin(10.f, 0.f, 0.f, 0.f)));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void AmbisonicMonoStripComponent::updateMeters()
{
    meterL.setValue (amStrip.meterL.getRMS());
    meterR.setValue (amStrip.meterR.getRMS());
}

//==============================================================================
MSStripComponent::MSStripComponent (MSStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), msStrip (s)
{
    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setCentralValue(0.f);
    setupKnob (wSlider, s.getName() + "_Width", apvts);

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::stereoStripDefault;

    setStripColor(c);
    setSliderColours (panSlider, c);
    setSliderColours (wSlider, c);

}

void MSStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::row;

    fbKnobs.items.add(fi(panSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));
    fbKnobs.items.add(fi(wSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 1.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 0, 0, 1.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    fbMain.items.add(fi(icon).withFlex(0.2f));
    fbMain.items.add(fi(fbKnobs).withFlex(0.2f));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void MSStripComponent::updateMeters()
{
    meterL.setValue (msStrip.meterL.getRMS());
    meterR.setValue (msStrip.meterR.getRMS());
}

//==============================================================================
StereoStripComponent::StereoStripComponent (StereoStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), stereoStrip (s)
{
    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setCentralValue(0.f);
    panSlider.setTooltip ("Stereo Pan");

    setupKnob (wSlider, s.getName() + "_Width", apvts);
    wSlider.setTooltip ("Stereo Width");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::stereoStripDefault;

    setStripColor(c);
    setSliderColours (panSlider, c);
    setSliderColours (wSlider, c);

}

void StereoStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::row;

    fbKnobs.items.add(fi(panSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));
    fbKnobs.items.add(fi(wSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 1.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 0, 0, 1.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    fbMain.items.add(fi(icon).withFlex(0.2f));
    fbMain.items.add(fi(fbKnobs).withFlex(0.2f));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void StereoStripComponent::updateMeters()
{
    meterL.setValue (stereoStrip.meterL.getRMS());
    meterR.setValue (stereoStrip.meterR.getRMS());
}

//==============================================================================
MonoStripComponent::MonoStripComponent (MonoStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), monoStrip (s)
{
    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setCentralValue(0.f);
    panSlider.setTooltip ("Mono Pan");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::monoStripDefault;

    setStripColor(c);
    setSliderColours (panSlider, c);

}

void MonoStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::row;

    fbKnobs.items.add(fi(panSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(10.f,0.f,0.f,0.f)));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 1.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 0, 0, 1.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    fbMain.items.add(fi(icon).withFlex(0.2f));
    fbMain.items.add(fi(fbKnobs).withFlex(0.2f));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void MonoStripComponent::updateMeters()
{
    meterL.setValue (monoStrip.meterL.getRMS());
    meterR.setValue (monoStrip.meterR.getRMS());
}

//==============================================================================
StereoReverbStripComponent::StereoReverbStripComponent (StereoReverbStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), reverbStrip (s)
{
    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setCentralValue(0.f);
    panSlider.setTooltip ("Stereo Pan");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::stereoStripDefault;

    if (s.reverb.getImpulseNames().size() > 1)
    {
        addAndMakeVisible (irBox);
        const auto& names = s.reverb.getImpulseNames();
        for (int i = 0; i < names.size(); ++i)
            irBox.addItem (juce::File (names[i]).getFileNameWithoutExtension(), i + 1);
        
        irAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, s.getName() + "_Rev_IR", irBox);
    }

    setStripColor(c);
    setSliderColours (panSlider, c);

}

void StereoReverbStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::row;

    fbKnobs.items.add(fi(panSlider).withFlex(1.f));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(1.f, 1.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(1.f, 0, 0, 1.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    
    if (irBox.isVisible())
        fbMain.items.add(fi(icon).withFlex(0.24f));
    else
        fbMain.items.add(fi(icon).withFlex(0.3f));

    if (irBox.isVisible())
        fbMain.items.add(fi(irBox).withFlex(0.06f).withMargin(2));

    fbMain.items.add(fi(fbKnobs).withFlex(0.2f));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void StereoReverbStripComponent::updateMeters()
{
    meterL.setValue (reverbStrip.meterL.getRMS());
    meterR.setValue (reverbStrip.meterR.getRMS());
}

//==============================================================================
MonoReverbStripComponent::MonoReverbStripComponent (MonoReverbStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), reverbStrip (s)
{
    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setCentralValue(0.f);
    panSlider.setTooltip ("Mono Pan");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::stereoStripDefault;

    if (s.reverb.getImpulseNames().size() > 1)
    {
        addAndMakeVisible (irBox);
        const auto& names = s.reverb.getImpulseNames();
        for (int i = 0; i < names.size(); ++i)
            irBox.addItem (juce::File (names[i]).getFileNameWithoutExtension(), i + 1);
        
        irAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, s.getName() + "_Rev_IR", irBox);
    }

    setStripColor(c);
    setSliderColours (panSlider, c);

}

void MonoReverbStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::row;

    fbKnobs.items.add(fi(panSlider).withFlex(1.f));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 1.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 0, 0, 1.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));

    if (irBox.isVisible())
        fbMain.items.add(fi(icon).withFlex(0.24f));
    else
        fbMain.items.add(fi(icon).withFlex(0.3f));

    if (irBox.isVisible())
        fbMain.items.add(fi(irBox).withFlex(0.06f).withMargin(2));

    fbMain.items.add(fi(fbKnobs).withFlex(0.2f));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void MonoReverbStripComponent::updateMeters()
{
    meterL.setValue(reverbStrip.meterL.getRMS());
    meterR.setValue(reverbStrip.meterR.getRMS());
}

//==============================================================================
BusStripComponent::BusStripComponent (BusStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), busStrip (s)
{
    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setCentralValue(0.f);
    panSlider.setTooltip ("Bus Pan");

    setupKnob (wSlider, s.getName() + "_Width", apvts);
    wSlider.setTooltip ("Bus Width");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::reverbStripDefault;

    setStripColor(c);
    setSliderColours (panSlider, c);
    setSliderColours (wSlider, c);

}

void BusStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::row;

    fbKnobs.items.add(fi(panSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));
    fbKnobs.items.add(fi(wSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 2.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(0, 0, 0, 2.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    fbMain.items.add(fi(icon).withFlex(0.3f));
    fbMain.items.add(fi(fbKnobs).withFlex(0.2f));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void BusStripComponent::updateMeters()
{
    meterL.setValue (busStrip.meterL.getRMS());
    meterR.setValue (busStrip.meterR.getRMS());
}

//==============================================================================
MasterStripComponent::MasterStripComponent (MasterStrip& s, juce::AudioProcessorValueTreeState& apvts)
    : StripComponent (s, apvts), masterStrip (s)
{
    setupKnob (panSlider, s.getName() + "_Pan", apvts);
    panSlider.setCentralValue(0.f);
    panSlider.setTooltip ("Master Pan");

    setupKnob (wSlider, s.getName() + "_Width", apvts);
    wSlider.setTooltip ("Master Width");

    juce::Colour c = s.getColor();
    if (c.isTransparent()) c = fxsampler::theme::busStripDefault;

    setStripColor(c);
    setSliderColours (panSlider, c);
    setSliderColours (wSlider, c);

}

void MasterStripComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbBottom, fbMeters, fbKnobs;

    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fbBottom.flexDirection = juce::FlexBox::Direction::row;
    fbMeters.flexDirection = juce::FlexBox::Direction::row;
    fbKnobs.flexDirection = juce::FlexBox::Direction::row;

    fbKnobs.items.add(fi(panSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));
    fbKnobs.items.add(fi(wSlider).withFlex(1.f).withMargin(juce::FlexItem::Margin(0.f, -5.f, 0, -5.f)));

    fbMeters.items.add(fi(meterL).withFlex(1.f).withMargin(juce::FlexItem::Margin(5.f, 2.f, 0, 0)));
    fbMeters.items.add(fi(meterR).withFlex(1.f).withMargin(juce::FlexItem::Margin(5.f, 0, 0, 2.f)));

    fbMain.items.add(fi(nameBar).withFlex(0.05f));
    fbMain.items.add(fi(icon).withFlex(0.3f));
    fbMain.items.add(fi(fbKnobs).withFlex(0.2f).withMargin(juce::FlexItem::Margin(10.f, 0.f, 0.f, 0.f)));
    fbMain.items.add(fi(fbBottom).withFlex(0.8f));

    layoutBottomRow(fbBottom, fbMeters);
    fbMain.performLayout (bounds);
}

void MasterStripComponent::updateMeters()
{
    meterL.setValue (masterStrip.meterL.getRMS());
    meterR.setValue (masterStrip.meterR.getRMS());
}
