/*
  ==============================================================================

    Mixer.cpp

  ==============================================================================
*/

#include "Mixer.h"
#include "EffectChainAmp.h"
#include "EffectChainDelay.h"
#include "EffectChainDynamics.h"
#include "EffectChainReverb.h"

//==============================================================================
// The one place that knows the effectChain= vocabulary. Returns nullptr for
// "None" and for anything unrecognised — a bus takes that as no chain at all,
// a strip falls back to the Dynamics chain its prepare() creates.
static std::unique_ptr<EffectChain> makeEffectChain (const juce::String& type)
{
    if (type.equalsIgnoreCase ("Dynamics")) return std::make_unique<EffectChainDynamics>();
    if (type.equalsIgnoreCase ("Amp"))      return std::make_unique<EffectChainAmp>();
    if (type.equalsIgnoreCase ("Reverb"))   return std::make_unique<EffectChainReverb>();
    if (type.equalsIgnoreCase ("Delay"))    return std::make_unique<EffectChainDelay>();

    return nullptr;
}

//==============================================================================
// Every img= attribute in a mapping goes through here. Returns a null Image
// when the name resolves to nothing, which the strips already treat as "no
// artwork".
static juce::Image loadImageResource (const fxsampler::ResourceProvider& resources,
                                      const juce::String& imageName)
{
    if (imageName.isEmpty())
        return {};

    int dataSize = 0;

    if (auto* data = resources.find (imageName, dataSize))
        return juce::ImageCache::getFromMemory (data, dataSize);

    return {};
}

//==============================================================================
void Mixer::prepare (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentSamplesPerBlock = samplesPerBlock;
    mixBuffer.setSize (2, samplesPerBlock); // Stereo mix bus
    for (auto& strip : strips)
        strip->prepare (sampleRate, samplesPerBlock);
    masterStrip.prepare (sampleRate, samplesPerBlock);
}

void Mixer::loadFromXml (const void* xmlData, int xmlSize,
                         const fxsampler::ResourceProvider& resources)
{
    if (xmlData == nullptr || xmlSize <= 0)
        return;

    juce::XmlDocument doc (juce::String::createStringFromData (xmlData, xmlSize));
    auto root = doc.getDocumentElement();

    // The root tag is <Mapping>, matching the file name. <Mappings> is the
    // older spelling and is still accepted so an unconverted kit keeps loading.
    if (root == nullptr || ! (root->hasTagName ("Mapping") || root->hasTagName ("Mappings")))
        return;

    auto* welcomeNode = root->getChildByName ("WelcomeTab");
    if (welcomeNode != nullptr)
    {
        welcomeText = welcomeNode->getStringAttribute ("text");
        welcomeImage = loadImageResource (resources, welcomeNode->getStringAttribute ("img"));
    }

    auto* masterNode = root->getChildByName ("Master");
    if (masterNode != nullptr)
    {
        juce::String imgName = masterNode->getStringAttribute ("img");
        juce::String colorStr = masterNode->getStringAttribute ("color");

        if (colorStr.isNotEmpty())
        {
            juce::StringArray tokens;
            tokens.addTokens (colorStr, ",", "");
            if (tokens.size() == 3)
            {
                masterStrip.setColor (juce::Colour::fromRGB (
                    (juce::uint8) tokens[0].getIntValue(),
                    (juce::uint8) tokens[1].getIntValue(),
                    (juce::uint8) tokens[2].getIntValue()));
            }
            else
            {
                masterStrip.setColor (juce::Colour::fromString (colorStr));
            }
        }

        if (auto image = loadImageResource (resources, imgName); image.isValid())
            masterStrip.setImage (image);
    }

    auto* mixerNode = root->getChildByName ("Mixer");
    if (mixerNode == nullptr)
        return;

    strips.clear();

    for (auto* child : mixerNode->getChildIterator())
    {
        std::unique_ptr<MixerStrip> newStrip;
        juce::String type = child->getStringAttribute ("type");
        juce::String name = child->getStringAttribute ("name");
        juce::String chainType = child->getStringAttribute ("effectChain", "dynamics");

        if (child->hasTagName ("Strip"))
        {
            if (type.equalsIgnoreCase ("ambisonic"))
                newStrip = std::make_unique<AmbisonicStrip> (name);
            else if (type.equalsIgnoreCase ("ambisonicmono"))
                newStrip = std::make_unique<AmbisonicMonoStrip> (name);
            else if (type.equalsIgnoreCase ("stereo"))
                newStrip = std::make_unique<StereoStrip> (name);
            else if (type.equalsIgnoreCase ("ms"))
                newStrip = std::make_unique<MSStrip> (name);
            else if (type.equalsIgnoreCase ("mono"))
                newStrip = std::make_unique<MonoStrip> (name);
            else if (type.equalsIgnoreCase ("stereoreverb"))
                newStrip = std::make_unique<StereoReverbStrip> (name);
            else if (type.equalsIgnoreCase ("reverb"))
                newStrip = std::make_unique<MonoReverbStrip> (name);
        }
        else if (child->hasTagName ("Bus"))
        {
            // Bus is always stereo for now
            auto bus = std::make_unique<BusStrip> (name);

            // A bus with effectChain="None" (or a typo) keeps a null chain:
            // BusStrip::prepare is the one prepare() that does not invent a
            // default, so the bus really is a plain sum.
            bus->setEffectChain (makeEffectChain (child->getStringAttribute ("effectChain", "Dynamics")));

            newStrip = std::move (bus);
        }

        if (newStrip != nullptr)
        {
            // A strip's chain comes from the same attribute. Setting it here,
            // before prepare(), is what makes it stick: every strip's prepare()
            // creates a Dynamics chain only if it finds none.
            if (child->hasTagName ("Strip"))
                if (auto chain = makeEffectChain (chainType))
                    newStrip->setEffectChain (std::move (chain));

            juce::String imgName = child->getStringAttribute ("img");
            juce::String irName = child->getStringAttribute ("resource");
            juce::String colorStr = child->getStringAttribute ("color");

                if (colorStr.isNotEmpty())
                {
                    juce::StringArray tokens;
                    tokens.addTokens (colorStr, ",", "");
                    if (tokens.size() == 3)
                    {
                        newStrip->setColor (juce::Colour::fromRGB (
                            (juce::uint8) tokens[0].getIntValue(),
                            (juce::uint8) tokens[1].getIntValue(),
                            (juce::uint8) tokens[2].getIntValue()));
                    }
                    else
                    {
                        newStrip->setColor (juce::Colour::fromString (colorStr));
                    }
                }

                if (auto image = loadImageResource (resources, imgName); image.isValid())
                    newStrip->setImage (image);

                if (irName.isNotEmpty())
                {
                    juce::StringArray resources = juce::StringArray::fromTokens (irName, ",", "");
                    juce::StringArray namesList;
                    juce::StringArray resList;

                    for (auto& res : resources)
                    {
                        juce::String trimmedRes = res.trim();
                        if (trimmedRes.isNotEmpty())
                        {
                            // ConvolReverb does its own BinaryData lookup, so
                            // what it gets has to be the mangled identifier
                            // rather than the file name.
                            resList.add (fxsampler::ResourceProvider::makeIdentifier (trimmedRes));
                            namesList.add (trimmedRes);
                        }
                    }

                    if (auto* rs = dynamic_cast<StereoReverbStrip*>(newStrip.get()))
                    {
                        rs->setImpulseList (namesList, resList);
                    }
                    else if (auto* rs = dynamic_cast<MonoReverbStrip*>(newStrip.get()))
                    {
                        rs->setImpulseList (namesList, resList);
                    }
                    // Whoever in this strip consumes impulse responses gets the
                    // list: the reverb chain's convolution reverb, or the amp
                    // chain's cabinet. It has to happen before addParameters,
                    // because the count sets the range of the IR-choice
                    // parameters.
                    else if (auto* chain = dynamic_cast<EffectChainReverb*>(newStrip->getEffectChain()))
                    {
                        chain->getReverb().setImpulseList (namesList, resList);
                    }
                    else if (auto* chain = dynamic_cast<EffectChainAmp*>(newStrip->getEffectChain()))
                    {
                        chain->getCab().setImpulseList (namesList, resList);
                    }
                }

                // An amp chain whose strip named no IRs gets the factory
                // cabinets, so effectChain="Amp" is playable on its own. Still
                // before addParameters, which needs the final count.
                if (auto* ampChain = dynamic_cast<EffectChainAmp*> (newStrip->getEffectChain()))
                {
                    if (ampChain->getCab().getImpulseNames().isEmpty())
                    {
                        juce::StringArray cabNames, cabResources;
                        factoryCabinetImpulses (cabNames, cabResources);
                        ampChain->getCab().setImpulseList (cabNames, cabResources);
                    }
                }

                strips.push_back (std::move (newStrip));
        }
    }

    // Link sends
    std::vector<BusStrip*> buses;
    for (auto& strip : strips)
        if (auto* bus = dynamic_cast<BusStrip*>(strip.get()))
            buses.push_back (bus);

    for (auto& strip : strips)
    {
        auto* busStrip = dynamic_cast<BusStrip*> (strip.get());
        int sourceBusIndex = -1;

        // If this strip is a bus, find its index in the bus list
        if (busStrip != nullptr)
        {
            for (size_t i = 0; i < buses.size(); ++i)
                if (buses[i] == busStrip)
                {
                    sourceBusIndex = (int)i;
                    break;
                }
        }

        for (size_t i = 0; i < buses.size(); ++i)
        {
            auto* bus = buses[i];
            if (strip.get() == bus)
                continue; // Don't send to self

            // If current strip is a bus, only add send if destination bus index is greater
            if (busStrip == nullptr || (int)i > sourceBusIndex)
                strip->addSend (bus->getName(), bus);
        }
    }

    // Re-prepare if we are already running
    if (currentSampleRate > 0)
        prepare (currentSampleRate, currentSamplesPerBlock);
}

void Mixer::assignParameters (juce::AudioProcessorValueTreeState& apvts)
{
    for (auto& strip : strips)
        strip->assignParameters (apvts);
    masterStrip.assignParameters (apvts);
}

void Mixer::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    for (auto& strip : strips)
        strip->addParameters (params);
    masterStrip.addParameters (params);
}

void Mixer::setBPM(double bpm)
{
    for (auto& strip : strips)
        strip->setBPM(bpm);
    masterStrip.setBPM(bpm);
}

void Mixer::processBlock (const juce::AudioBuffer<float>& inputBuffer, juce::AudioBuffer<float>& outputBuffer)
{
    int currentInputChannel = 0;
    int totalInputChannels = inputBuffer.getNumChannels();

    // Ensure mixBuffer is ready. avoidReallocating keeps the audio thread
    // allocation-free: prepare() sized it to the worst case, so this only
    // shrinks the reported size when the host sends a smaller block. Same
    // idiom as every MixerStrip::process.
    if (mixBuffer.getNumChannels() != 2 || mixBuffer.getNumSamples() != outputBuffer.getNumSamples())
        mixBuffer.setSize (2, outputBuffer.getNumSamples(),
                           /*keepExistingContent*/ false,
                           /*clearExtraSpace*/     false,
                           /*avoidReallocating*/   true);
    mixBuffer.clear();

    for (auto& strip : strips)
        if (auto* bus = dynamic_cast<BusStrip*>(strip.get()))
            bus->clearBusBuffer();

    bool anySolo = false;
    for (auto& strip : strips)
        if (strip->isSolo()) { anySolo = true; break; }

    for (auto& strip : strips)
    {
        int needed = strip->getNumInputChannels();
        if (currentInputChannel + needed <= totalInputChannels)
        {
            bool shouldProcess = true;
            if (anySolo) shouldProcess = strip->isSolo();
            else         shouldProcess = ! strip->isMute();

            if (shouldProcess)
                strip->process (inputBuffer, mixBuffer, outputBuffer, currentInputChannel); // Sum to mixBuffer and/or outputBuffer
            else
                strip->clearMeters();
            currentInputChannel += needed;
        }
    }

    // Process Master Chain
    // Master takes mixBuffer (stereo) and adds to outputBuffer (which is cleared by processor)
    if (! masterStrip.isMute())
        masterStrip.process (mixBuffer, mixBuffer, outputBuffer, 0);
    else
        masterStrip.clearMeters();
}