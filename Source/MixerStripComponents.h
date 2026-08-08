/*
  ==============================================================================

    MixerStripComponents.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MixerStrips.h"
#include "TitleBar.h"
#include "VuMeterComponent.h"
#include "ConvolReverbComponent.h" // For irBox

//==============================================================================
/**
 * @class StripComponent
 * @brief Base class for a mixer strip GUI component.
 */
class StripComponent : public juce::Component, public juce::Timer
{
public:
    /**
     * @brief Constructor.
     * @param strip The MixerStrip to control.
     * @param apvts The APVTS for parameter attachments.
     */
    StripComponent (MixerStrip& strip, juce::AudioProcessorValueTreeState& apvts);
    ~StripComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /**
     * @brief Timer callback for updating meters.
     */
    void timerCallback() override;
    
    void setStripColor(juce::Colour c);

protected:
    MixerStrip& strip;
    juce::Label nameLabel;
    TitleBar nameBar;
    fxme::FxmeSlider levelSlider;
    juce::ToggleButton muteButton, soloButton;
    juce::ImageComponent icon;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;
    std::vector<std::unique_ptr<juce::Slider>> sendSliders;

    std::vector<std::unique_ptr<juce::ToggleButton>> prePostButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> prePostAtts;

    std::vector<std::unique_ptr<juce::ToggleButton>> routeButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> routeAtts;
    void createRouteButtons(juce::AudioProcessorValueTreeState& apvts);

    virtual void updateMeters() = 0;
    void layoutBottomRow(juce::FlexBox& bottomBox, juce::FlexBox& metersBox);
    void setupKnob (fxme::FxmeSlider& s, const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts);
    void setSliderColours (juce::Slider& s, juce::Colour c);

    VuMeterComponent meterL, meterR;


private:
    // FlexBox members for layout to avoid lifetime issues with local FlexBoxes
    // inside the layoutBottomRow helper function.
    juce::FlexBox fbButtonsMeters, fbRoutes, fbSends, fbOutputs;

};

/**
 * @class AmbisonicStripComponent
 * @brief GUI component for an Ambisonic mixer strip.
 */
class AmbisonicStripComponent : public StripComponent
{
public:
    AmbisonicStripComponent (AmbisonicStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    AmbisonicStrip& ambStrip;
    fxme::FxmeSlider azSlider, elSlider, wSlider;
    // VuMeterComponent meterL, meterR;
};

/**
 * @class AmbisonicMonoStripComponent
 * @brief GUI component for an Ambisonic + Mono mixer strip.
 */
class AmbisonicMonoStripComponent : public StripComponent
{
public:
    AmbisonicMonoStripComponent (AmbisonicMonoStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    AmbisonicMonoStrip& amStrip;
    fxme::FxmeSlider azSlider, elSlider, wSlider, panSlider, mixSlider;
};

/**
 * @class MSStripComponent
 * @brief GUI component for a Mid-Side mixer strip.
 */
class MSStripComponent : public StripComponent
{
public:
    MSStripComponent (MSStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    MSStrip& msStrip;
    fxme::FxmeSlider panSlider;
    fxme::FxmeSlider wSlider;
    // VuMeterComponent meterL, meterR;
};

/**
 * @class StereoStripComponent
 * @brief GUI component for a Stereo mixer strip.
 */
class StereoStripComponent : public StripComponent
{
public:
    StereoStripComponent (StereoStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    StereoStrip& stereoStrip;
    fxme::FxmeSlider panSlider;
    fxme::FxmeSlider wSlider;
    // VuMeterComponent meterL, meterR;
};

/**
 * @class MonoStripComponent
 * @brief GUI component for a Mono mixer strip.
 */
class MonoStripComponent : public StripComponent
{
public:
    MonoStripComponent (MonoStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    MonoStrip& monoStrip;
    fxme::FxmeSlider panSlider;
    // VuMeterComponent meterL, meterR;
};

/**
 * @class StereoReverbStripComponent
 * @brief GUI component for a Stereo Reverb mixer strip.
 */
class StereoReverbStripComponent : public StripComponent
{
public:
    StereoReverbStripComponent (StereoReverbStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    StereoReverbStrip& reverbStrip;
    fxme::FxmeSlider panSlider;
    juce::ComboBox irBox; // For IR selection dropdown
    juce::Label irLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> irAtt;
    // VuMeterComponent meterL, meterR;
};

/**
 * @class MonoReverbStripComponent
 * @brief GUI component for a Mono Reverb mixer strip.
 */
class MonoReverbStripComponent : public StripComponent
{
public:
    MonoReverbStripComponent (MonoReverbStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    MonoReverbStrip& reverbStrip;
    fxme::FxmeSlider panSlider;
    juce::ComboBox irBox; // For IR selection dropdown
    juce::Label irLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> irAtt;
    // VuMeterComponent meterL, meterR;
};

/**
 * @class BusStripComponent
 * @brief GUI component for a Bus mixer strip.
 */
class BusStripComponent : public StripComponent
{
public:
    BusStripComponent (BusStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    BusStrip& busStrip;
    fxme::FxmeSlider panSlider, wSlider;
    // VuMeterComponent meterL, meterR;
};

/**
 * @class MasterStripComponent
 * @brief GUI component for the Master mixer strip.
 */
class MasterStripComponent : public StripComponent
{
public:
    MasterStripComponent (MasterStrip& s, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
protected:
    void updateMeters() override;
private:
    MasterStrip& masterStrip;
    fxme::FxmeSlider panSlider, wSlider;
    // VuMeterComponent meterL, meterR;
};