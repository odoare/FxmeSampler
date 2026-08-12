/*
  ==============================================================================

    EffectChainComponent.h

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "EffectChain.h"

class EffectChainComponent : public juce::Component
{
public:
    EffectChainComponent() = default;
    ~EffectChainComponent() override = default;
};

/**
 * @brief Fills a combo box with a choice parameter's own value strings.
 *
 * ComboBoxAttachment maps selection to parameter value by *index* and never
 * looks at the item text, so a hand-written item list is free to drift away
 * from the parameter it drives — which is exactly what had happened to the
 * Dynamics chain's order box, whose items omitted the fixed "Trans ->" stage
 * that the parameter's own strings named. Reading the strings back from the
 * parameter makes that impossible: there is one list, in addParameters.
 *
 * Item IDs are 1-based indices, which is what the attachment expects.
 */
inline void populateFromChoiceParameter (juce::ComboBox& box,
                                         juce::AudioProcessorValueTreeState& apvts,
                                         const juce::String& parameterID)
{
    auto* parameter = apvts.getParameter (parameterID);

    // Missing parameter, or one that is not a choice: the attachment built on
    // this box is about to assert, so say which one and why.
    jassert (parameter != nullptr && ! parameter->getAllValueStrings().isEmpty());

    if (parameter == nullptr)
        return;

    int itemId = 1;

    for (const auto& choice : parameter->getAllValueStrings())
        box.addItem (choice, itemId++);
}
