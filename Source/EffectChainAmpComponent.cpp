/*
  ==============================================================================

    EffectChainAmpComponent.cpp

  ==============================================================================
*/

#include "EffectChainAmpComponent.h"

EffectChainAmpComponent::EffectChainAmpComponent (EffectChainAmp& c, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
    : chain (c),
      eqComp (c.getEQ(), apvts, prefix),
      compComp (c.getComp(), apvts, prefix),
      // A wide, short slot in the right-hand column, so the tube's knobs go in
      // one row rather than the default 2x2.
      tubeComp (c.getTube(), apvts, prefix, true, true),
      cabComp (c.getCab(), apvts, prefix)
{
    addAndMakeVisible (orderBox);
    populateFromChoiceParameter (orderBox, apvts, prefix + "_Order");
    orderAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, prefix + "_Order", orderBox);

    addAndMakeVisible (eqComp);
    addAndMakeVisible (compComp);
    addAndMakeVisible (tubeComp);
    addAndMakeVisible (cabComp);
}

void EffectChainAmpComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fbRight, fbCols;
    fbMain.flexDirection  = juce::FlexBox::Direction::column;
    fbRight.flexDirection = juce::FlexBox::Direction::column;
    fbCols.flexDirection  = juce::FlexBox::Direction::row;

    // Compressor, tube, cab stacked in the right-hand column, in signal order.
    // The cab is last in the chain and last down the column, but it does not
    // get the full width: at half width its two channels are still side by
    // side, and the EQ curve gets the whole height of the panel instead of
    // being squeezed into the top two thirds.
    //
    // The flex weights give the tube roughly the height it has in the Dynamics
    // chain, and hand the slack to the cab, which needs room for two IR plots.
    fbRight.items.add (fi (compComp).withFlex (1.2f).withMargin (juce::FlexItem::Margin (3.f, 6.f, 3.f, 3.f)));
    fbRight.items.add (fi (tubeComp).withFlex (0.9f).withMargin (juce::FlexItem::Margin (3.f, 6.f, 3.f, 3.f)));
    fbRight.items.add (fi (cabComp) .withFlex (1.3f).withMargin (juce::FlexItem::Margin (3.f, 6.f, 6.f, 3.f)));

    // Full height now, so the bottom margin is the panel edge rather than a
    // gap to something below it.
    fbCols.items.add (fi (eqComp).withFlex (1.0f).withMargin (juce::FlexItem::Margin (6.f, 3.f, 6.f, 6.f)));
    fbCols.items.add (fi (fbRight).withFlex (1.0f));

    fbMain.items.add (fi (orderBox).withFlex (0.1f).withMargin (juce::FlexItem::Margin (6.f, 6.f, 3.f, 6.f)));
    fbMain.items.add (fi (fbCols).withFlex (2.0f));

    fbMain.performLayout (bounds);
}
