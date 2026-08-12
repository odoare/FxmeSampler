/*
  ==============================================================================

    EffectChainAmp.cpp

  ==============================================================================
*/

#include "EffectChainAmp.h"
#include "ResourceProvider.h"

void factoryCabinetImpulses (juce::StringArray& names, juce::StringArray& resources)
{
    names.clear();
    resources.clear();

    int manifestSize = 0;
    const char* manifest = BinaryData::getNamedResource ("cabinets_txt", manifestSize);

    if (manifest == nullptr || manifestSize <= 0)
        return;

    auto lines = juce::StringArray::fromLines (juce::String::createStringFromData (manifest, manifestSize));

    for (auto& line : lines)
    {
        const auto name = line.trim();

        if (name.isEmpty())
            continue;

        const auto identifier = fxsampler::ResourceProvider::makeIdentifier (name);

        int size = 0;
        if (BinaryData::getNamedResource (identifier.toRawUTF8(), size) != nullptr)
        {
            names.add (name);
            resources.add (identifier);
        }
    }
}

EffectChainAmp::EffectChainAmp()
{
}

void EffectChainAmp::prepare (double sampleRate, int samplesPerBlock, int numChannels)
{
    eq.prepare (sampleRate, numChannels);
    comp.prepare (sampleRate, numChannels);
    tube.prepare (sampleRate);
    cab.prepare (sampleRate, samplesPerBlock);
}

void EffectChainAmp::assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
{
    orderParam = apvts.getRawParameterValue (prefix + "_Order");
    eq.assignParameters (apvts, prefix);
    comp.assignParameters (apvts, prefix);
    tube.assignParameters (apvts, prefix);
    cab.assignParameters (apvts, prefix);
}

void EffectChainAmp::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& prefix)
{
    // These strings are the single source of truth for the order box in
    // EffectChainAmpComponent, which reads them back off the parameter.
    juce::StringArray orderOptions;
    orderOptions.add ("EQ -> Comp -> Tube -> Cab");
    orderOptions.add ("EQ -> Tube -> Comp -> Cab");
    orderOptions.add ("Comp -> EQ -> Tube -> Cab");
    orderOptions.add ("Comp -> Tube -> EQ -> Cab");
    orderOptions.add ("Tube -> EQ -> Comp -> Cab");
    orderOptions.add ("Tube -> Comp -> EQ -> Cab");
    params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { prefix + "_Order", 1 }, prefix + " Order", orderOptions, 0));

    Equalizer::addParameters (params, prefix);
    Compressor::addParameters (params, prefix);
    Tube::addParameters (params, prefix);

    // The IR-choice parameters are ranged 1..numIRs, so the impulse list has to
    // be in place by now. It is: Mixer::loadFromXml sets it while building the
    // strips, and createParameterLayout runs this on those same objects
    // afterwards.
    Cab::addParameters (params, prefix, cab.getImpulseNames().size());
}

void EffectChainAmp::checkParameters()
{
    eq.checkParameters();
    comp.checkParameters();
    tube.checkParameters();
    // Cab checks its own inside process(), as ConvolReverb does.
}

void EffectChainAmp::process (juce::AudioBuffer<float>& buffer)
{
    checkParameters();

    int order = 0;
    if (orderParam) order = (int) *orderParam;

    // 0: EQ -> Comp -> Tube
    // 1: EQ -> Tube -> Comp
    // 2: Comp -> EQ -> Tube
    // 3: Comp -> Tube -> EQ
    // 4: Tube -> EQ -> Comp
    // 5: Tube -> Comp -> EQ

    switch (order)
    {
        case 0: eq.process (buffer); comp.process (buffer); tube.process (buffer); break;
        case 1: eq.process (buffer); tube.process (buffer); comp.process (buffer); break;
        case 2: comp.process (buffer); eq.process (buffer); tube.process (buffer); break;
        case 3: comp.process (buffer); tube.process (buffer); eq.process (buffer); break;
        case 4: tube.process (buffer); eq.process (buffer); comp.process (buffer); break;
        case 5: tube.process (buffer); comp.process (buffer); eq.process (buffer); break;
        default: eq.process (buffer); comp.process (buffer); tube.process (buffer); break;
    }

    // Always last, whatever the order says.
    cab.process (buffer);
}
