/*
  ==============================================================================

    PluginEditor.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MixerComponent.h"

/**
 * @class FxmeSamplerAudioProcessorEditor
 * @brief The editor (GUI) for the FxmeSampler plugin.
 *
 * Owns the single fxme::FxmeLookAndFeel for the whole editor. JUCE resolves a
 * component's look-and-feel by walking up its parents, so every child inherits
 * it without setting one of its own. It is declared before the components so it
 * is destroyed after them (members are destroyed in reverse declaration order),
 * and cleared in the destructor.
 */
class FxmeSamplerAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    /**
     * @brief Constructor.
     * @param p The AudioProcessor to edit.
     */
    FxmeSamplerAudioProcessorEditor (FxmeSamplerAudioProcessor&);
    ~FxmeSamplerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FxmeSamplerAudioProcessor& audioProcessor;

    fxme::FxmeLookAndFeel laf;   // before the components: outlives them

    MixerComponent mixerComponent;
    fxme::InfoButton infoButton;

    // Exactly one per editor, declared after the child components. Makes
    // keyboard focus reliable for TextEditors in hosted (Linux) windows, which
    // here means FxmeSlider's right-click value entry.
    fxme::TextEntryFocusFixer textEntryFixer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxmeSamplerAudioProcessorEditor)
};
