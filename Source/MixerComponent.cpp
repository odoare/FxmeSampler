/*
  ==============================================================================

    MixerComponent.cpp

  ==============================================================================
*/

#include "MixerComponent.h"
#include "Theme.h"
#include "EffectChainAmpComponent.h"
#include "EffectChainReverbComponent.h"
#include "EffectChainDelay.h"
#include "EffectChainDelayComponent.h"
#include "EffectChainDynamicsComponent.h"
#include "WelcomeTabComponent.h"

//==============================================================================
MixerComponent::LevelsComponent::LevelsComponent (Mixer& m, juce::AudioProcessorValueTreeState& state)
    : mixer (m)
{
    const auto& strips = mixer.getStrips();
    for (auto& strip : strips)
    {
        if (auto* s = dynamic_cast<AmbisonicStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<AmbisonicStripComponent> (*s, state));
        else if (auto* s = dynamic_cast<AmbisonicMonoStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<AmbisonicMonoStripComponent> (*s, state));
        else if (auto* s = dynamic_cast<StereoStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<StereoStripComponent> (*s, state));
        else if (auto* s = dynamic_cast<MSStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<MSStripComponent> (*s, state));
        else if (auto* s = dynamic_cast<MonoStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<MonoStripComponent> (*s, state));
        else if (auto* s = dynamic_cast<StereoReverbStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<StereoReverbStripComponent> (*s, state));
        else if (auto* s = dynamic_cast<MonoReverbStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<MonoReverbStripComponent> (*s, state));
        else if (auto* s = dynamic_cast<BusStrip*>(strip.get()))
            this->strips.push_back (std::make_unique<BusStripComponent> (*s, state));
            
        if (!this->strips.empty() && this->strips.back()->getParentComponent() == nullptr)
            addAndMakeVisible (*this->strips.back());
    }

    this->strips.push_back (std::make_unique<MasterStripComponent> (mixer.getMasterStrip(), state));
    addAndMakeVisible (*this->strips.back());
}

void MixerComponent::LevelsComponent::resized()
{
    auto area = getLocalBounds().reduced (5);
    int stripWidth = area.getWidth() / (int) strips.size();
    
    for (auto& s : strips)
    {
        s->setBounds (area.removeFromLeft (stripWidth).reduced (2));
    }
}

//==============================================================================
MixerComponent::MixerComponent (Mixer& m, Sampler& s, juce::AudioProcessorValueTreeState& state,
                                fxme::PresetManager& presetManager)
    : mixer (m), apvts (state), tabs (juce::TabbedButtonBar::TabsAtTop), levelsComp (m, state), samplerComp (s, state)
{
    // The welcome tab hosts the preset browser alongside the kit artwork.
    tabs.addTab("Welcome", juce::Colours::black,
                new WelcomeTabComponent(mixer.getWelcomeText(), mixer.getWelcomeImage(), presetManager), true);

    tabs.addTab ("Levels", juce::Colours::black, &levelsComp, false);
    
    // We create EffectChainComponents dynamically and pass ownership to the tabs
    // Note: In a real app we might want to store these pointers to avoid leaks if tabs are cleared,
    // but addTab takes ownership if we pass true (which we will for these new components).
    // However, addTab with 'true' deletes the component. We need to make sure we allocate them.
    
    auto addChainComponent = [&](const juce::String& name, EffectChain* chain) {
        if (auto* dynChain = dynamic_cast<EffectChainDynamics*>(chain))
        {
            tabs.addTab (name, juce::Colours::black, new EffectChainDynamicsComponent (*dynChain, apvts, name), true);
        }
        else if (auto* ampChain = dynamic_cast<EffectChainAmp*>(chain))
        {
            tabs.addTab (name, juce::Colours::black, new EffectChainAmpComponent (*ampChain, apvts, name), true);
        }
        else if (auto* reverbChain = dynamic_cast<EffectChainReverb*>(chain))
        {
            tabs.addTab (name, juce::Colours::black, new EffectChainReverbComponent (*reverbChain, apvts, name), true);
        }
        else if (auto* delayChain = dynamic_cast<EffectChainDelay*>(chain))
        {
            tabs.addTab (name, juce::Colours::black, new EffectChainDelayComponent (*delayChain, apvts, name), true);
        }
    };

    for (auto& strip : mixer.getStrips())
    {
        addChainComponent (strip->getName(), strip->getEffectChain());
    }
    
    auto& master = mixer.getMasterStrip();
    addChainComponent (master.getName(), master.getEffectChain());

    tabs.addTab ("Sampler", juce::Colours::black, &samplerComp, false);

    addAndMakeVisible (tabs);
    int n = mixer.getStrips().size() + 1; // +1 for Master
    setSize (n*150, 500);
}

MixerComponent::~MixerComponent()
{
}

void MixerComponent::paint (juce::Graphics& g)
{
    fxsampler::theme::paintPanelBackground (g, getLocalBounds(), fxsampler::theme::background);
}

void MixerComponent::paintOverChildren (juce::Graphics& g)
{
    auto& bar = tabs.getTabbedButtonBar();

    // Where the tabs actually end. JUCE lays each one out at its natural width
    // and never stretches them to fill the bar, so there is normally room to
    // spare. Only visible tabs count: once they stop fitting, the overflow is
    // hidden behind a chevron at the far right and the hidden buttons keep
    // bounds that run off the end of the bar.
    int tabsEnd = 0;
    for (int i = 0; i < bar.getNumTabs(); ++i)
        if (auto* tab = bar.getTabButton (i))
            if (tab->isVisible())
                tabsEnd = juce::jmax (tabsEnd, tab->getRight());

    const juce::Rectangle<int> freeInBar (tabsEnd, 0,
                                          bar.getWidth() - tabsEnd, bar.getHeight());
    auto area = getLocalArea (&bar, freeInBar).reduced (10, 0);

    // Too tight to be worth it, or the chevron is sitting there. The tabs and
    // the overflow control both matter more than the version does.

    if (area.getWidth() < 90)
        return;

    // Same treatment as fxmefx::TopBar, so the suite reads consistently.
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("v" JucePlugin_VersionString "  -  FX-Mechanics",
                area, juce::Justification::centredRight);
}

void MixerComponent::resized()
{
    tabs.setBounds (getLocalBounds());
}