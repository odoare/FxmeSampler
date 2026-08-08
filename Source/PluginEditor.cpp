/*
  ==============================================================================

    PluginEditor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"

FxmeSamplerAudioProcessorEditor::FxmeSamplerAudioProcessorEditor (FxmeSamplerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      mixerComponent (p.getMixer(), p.getSampler(), p.getAPVTS(), p.getPresetManager())
{
    setLookAndFeel (&laf);

    addAndMakeVisible (mixerComponent);

    infoButton.setColours ({ fxsampler::theme::presetAccent,
                             fxsampler::theme::text,
                             fxsampler::theme::background.darker (2.0f),
                             fxsampler::theme::background.brighter (0.3f) });
    infoButton.setInfo ("FxmeSampler",
                        "Tabs\n"
                        "  Welcome   kit artwork, and the preset browser\n"
                        "  Levels    one strip per microphone or bus, plus Master\n"
                        "  <strip>   the effect chain for that strip\n"
                        "  Sampler   per-group envelope, detune and velocity\n"
                        "\n"
                        "Presets\n"
                        "  Factory presets ship with the kit; Save As writes your\n"
                        "  own next to them in the user preset folder. A dot next\n"
                        "  to the name means unsaved changes.\n"
                        "\n"
                        "Knobs\n"
                        "  Drag to change, double-click to reset,\n"
                        "  right-click to type a value.\n"
                        "\n"
                        "Mixer\n"
                        "  Solo is global: soloing any strip mutes the others.\n"
                        "  Sends feed the aux buses; each has a pre/post switch.");
    addAndMakeVisible (infoButton);

    int n = p.getMixer().getStrips().size();
    setSize (juce::jmax<int>(n*140,1024), 640);
}

FxmeSamplerAudioProcessorEditor::~FxmeSamplerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void FxmeSamplerAudioProcessorEditor::paint (juce::Graphics& g)
{
    fxsampler::theme::paintPanelBackground (g, getLocalBounds(), fxsampler::theme::background);
}

void FxmeSamplerAudioProcessorEditor::resized()
{
    mixerComponent.setBounds (getLocalBounds());

    // Parked in the top-right corner, over the spare space at the end of the
    // tab bar. Added after mixerComponent, so it draws on top of it.
    const int size   = fxsampler::theme::infoButtonSize;
    const int margin = fxsampler::theme::infoButtonMargin;
    infoButton.setBounds (getWidth() - size - margin, margin, size, size);
}
