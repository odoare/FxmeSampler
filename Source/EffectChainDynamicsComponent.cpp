/*
  ==============================================================================

    EffectChainDynamicsComponent.cpp

  ==============================================================================
*/

#include "EffectChainDynamicsComponent.h"

EffectChainDynamicsComponent::EffectChainDynamicsComponent (EffectChainDynamics& c, juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
    : chain (c),
      eqComp (c.getEQ(), apvts, prefix),
      compComp (c.getComp(), apvts, prefix),
      // The chain gives the tube a wide, short slot (a third of the right-hand
      // column), so lay its knobs out in one row instead of the default 2x2.
      tubeComp (c.getTube(), apvts, prefix, true, true),
      transComp (c.getTransient(), apvts, prefix)
{
    addAndMakeVisible (orderBox);
    populateFromChoiceParameter (orderBox, apvts, prefix + "_Order");
    orderAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, prefix + "_Order", orderBox);

    addAndMakeVisible (eqComp);
    addAndMakeVisible (compComp);
    addAndMakeVisible (tubeComp);
    addAndMakeVisible (transComp);
}

void EffectChainDynamicsComponent::resized()
{
    auto bounds = getLocalBounds();
    using fi = juce::FlexItem;
    juce::FlexBox fbMain, fb1, fb2;
    fbMain.flexDirection = juce::FlexBox::Direction::column;
    fb1.flexDirection = juce::FlexBox::Direction::column;
    fb2.flexDirection = juce::FlexBox::Direction::row;       
    
    fb1.items.add(fi(transComp).withFlex(1.0f).withMargin(juce::FlexItem::Margin(3.f, 3.f, 6.f, 6.f)));
    fb1.items.add(fi(compComp).withFlex(1.2f).withMargin(juce::FlexItem::Margin(3.f, 3.f, 6.f, 6.f)));
    fb1.items.add(fi(tubeComp).withFlex(0.8f).withMargin(juce::FlexItem::Margin(3.f, 6.f, 6.f, 3.f)));
    
    fb2.items.add(fi(eqComp).withFlex(1.0f).withMargin(juce::FlexItem::Margin(6.f, 6.f, 3.f, 6.f)));
    fb2.items.add(fi(fb1).withFlex(1.0f));        
    
    fbMain.items.add(fi(orderBox).withFlex(0.1).withMargin(juce::FlexItem::Margin(6.f, 6.f, 3.f, 6.f)));
    fbMain.items.add(fi(fb2).withFlex(2.));
    
    fbMain.performLayout(bounds);
}
