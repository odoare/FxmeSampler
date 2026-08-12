/*
  ==============================================================================

    EffectChainAmp.h

    Equalizer, compressor and tube in any order, into a cabinet IR.

    The cab is deliberately not part of the ordering: a speaker cabinet is the
    last thing in the signal path of the instrument it belongs to, and putting
    an EQ or a compressor after it would be a different effect entirely (a
    post-cab EQ is a mix decision, not an amp decision). So the order parameter
    permutes the three upstream stages and every option ends "-> Cab", the same
    way EffectChainDynamics names its fixed "Trans ->" stage in its own.

  ==============================================================================
*/

#pragma once

#include "EffectChain.h"
#include "Equalizer.h"
#include "Compressor.h"
#include "Tube.h"
#include "Cab.h"
#include <atomic>

/**
 * @brief The cabinet IRs embedded in this binary, ready for setImpulseList.
 * @param names Receives the file names, for display.
 * @param resources Receives the matching BinaryData identifiers.
 *
 * Read from the cabinets.txt manifest that fxme_factory_cabinets() generates
 * beside the IRs it embeds — a kit's BinaryData is mostly drum samples, so
 * "every embedded wav" would not do. Names that turn out not to be embedded
 * after all are skipped, so a target that opts out of the set simply gets an
 * empty list rather than a combo box full of silence.
 *
 * Deliberately reads BinaryData rather than a ResourceProvider: Cab resolves IR
 * names through BinaryData too, so an IR that is only in a kit folder could be
 * listed here and still not load.
 */
void factoryCabinetImpulses (juce::StringArray& names, juce::StringArray& resources);

class EffectChainAmp : public EffectChain
{
public:
    EffectChainAmp();

    juce::String getTypeName() const override { return "Amp"; }

    void prepare (double sampleRate, int samplesPerBlock, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void assignParameters (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override;
    void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params, const juce::String& prefix) override;

    Equalizer& getEQ() { return eq; }
    Compressor& getComp() { return comp; }
    Tube& getTube() { return tube; }

    /** The cabinet. Mixer::loadFromXml hands it the strip's resource= list
        before parameters are created, because the number of IRs decides the
        range of the two IR-choice parameters. */
    Cab& getCab() { return cab; }

private:
    Equalizer eq;
    Compressor comp;
    Tube tube;
    Cab cab;
    std::atomic<float>* orderParam = nullptr;

    void checkParameters();
};
