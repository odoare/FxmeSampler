/*
  ==============================================================================

    Sampler.cpp
    Created: 30 Jan 2026 3:56:29pm
    Author:  doare

  ==============================================================================
*/

#include "Sampler.h"
#include <map>
#include <cmath>

//==============================================================================
void Sampler::loadSound (Sound& sound)
{
    if (sound.audioBuffer)
        return;

    // Check cache first
    if (sound.resourceName.isNotEmpty())
    {
        auto it = sampleCache.find (sound.resourceName);
        if (it != sampleCache.end())
        {
            sound.audioBuffer = it->second.first;
            sound.sourceSampleRate = it->second.second;
            return;
        }
    }

    if (sound.data == nullptr || sound.dataSize <= 0)
        return;

    auto stream = std::make_unique<juce::MemoryInputStream> (sound.data, (size_t) sound.dataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (std::move (stream)));

    if (reader != nullptr)
    {
        auto buffer = std::make_shared<juce::AudioBuffer<float>> ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (buffer.get(), 0, (int) reader->lengthInSamples, 0, true, true);
        
        sound.audioBuffer = buffer;
        sound.sourceSampleRate = reader->sampleRate;

        if (sound.resourceName.isNotEmpty())
        {
            sampleCache[sound.resourceName] = { buffer, sound.sourceSampleRate };
        }
    }
}

//==============================================================================
void Voice::start (const Sound* sound, int note, float velocity, double sampleRate)
{
    activeSound = sound;
    currentNote = note;
    currentPosition = (sound ? (double)sound->sampleStart : 0.0);
    currentVelocity = velocity;
    currentSampleRate = sampleRate;
    
    envelopeVal = 0.0f;
    state = State::Attack;

    if (activeSound && activeSound->audioBuffer)
    {
        double detune = 0.0;
        double attack = activeSound->attack;
        double decay = activeSound->decay;
        double sustain = activeSound->sustain;
        double release = activeSound->release;

        if (activeSound->group)
        {
            auto* g = activeSound->group;
            detune = g->detune;

            if (g->randomDetune > 0.0)
            {
                detune += (random.nextDouble() * 2.0 - 1.0) * g->randomDetune / 100.0;
            }

            attack = g->attack;
            decay = g->decay;
            sustain = g->sustain;
            release = g->release;

            float minGain = juce::Decibels::decibelsToGain ((float)g->minVelocityGain);
            // Scale velocity range from [0, 1] to [minGain, 1] and multiply by group level
            currentVelocity = (minGain + (1.0f - minGain) * velocity) * juce::Decibels::decibelsToGain (g->groupLevel);
        }

        double pitchRatio = std::pow (2.0, (note - activeSound->basePitch + detune) / 12.0);
        increment = (activeSound->sourceSampleRate / currentSampleRate) * pitchRatio;
        
        double attackSamples = attack * currentSampleRate;
        double decaySamples = decay * currentSampleRate;
        double releaseSamples = release * currentSampleRate;

        sustainLevel = sustain;

        attackRate = (attackSamples > 0.0) ? (1.0 / attackSamples) : 1.0;
        decayRate = (decaySamples > 0.0) ? ((1.0 - sustainLevel) / decaySamples) : 1.0;
        releaseRate = (releaseSamples > 0.0) ? (1.0 / releaseSamples) : 1.0;

    }
}

void Voice::stop()
{
    state = State::Idle;
    activeSound = nullptr;
}

void Voice::choke()
{
    if (state == State::Idle)
        return;

    state = State::Release;
    // Fast fade out (50ms) to avoid clicks
    double chokeSamples = 0.05 * currentSampleRate;
    releaseRate = (chokeSamples > 0.0) ? (1.0 / chokeSamples) : 1.0;
}

void Voice::noteOff()
{
    if (state != State::Idle && state != State::Release)
    {
        state = State::Release;
    }
}

bool Voice::isActive() const
{
    return state != State::Idle;
}

void Voice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (state == State::Idle || activeSound == nullptr || activeSound->audioBuffer == nullptr)
        return;

    const auto& sourceBuffer = *activeSound->audioBuffer;
    int numSourceChannels = sourceBuffer.getNumChannels();
    int numOutputChannels = outputBuffer.getNumChannels();
    const auto& targetChannels = activeSound->outputChannels;

    bool isLooping = false;
    if (activeSound->group) isLooping = activeSound->group->isLoop;
    
    bool isOneShot = activeSound->isOneShot;
    if (activeSound->group)
    {
        isOneShot = activeSound->group->isOneShot;
    }

    if (isOneShot) isLooping = false;
    
    for (int s = 0; s < numSamples; ++s)
    {
        // Envelope processing
        if (state == State::Attack)
        {
            envelopeVal += (float) attackRate;
            if (envelopeVal >= 1.0f)
            {
                envelopeVal = 1.0f;
                state = State::Decay;
            }
        }
        else if (state == State::Decay)
        {
            envelopeVal -= (float) decayRate;
            if (envelopeVal <= (float) sustainLevel)
            {
                envelopeVal = (float) sustainLevel;
                state = State::Sustain;
            }
        }
        else if (state == State::Sustain)
        {
            if (envelopeVal <= 0.0001f) // Optimization for silence
            {
                stop();
                return;
            }
        }
        else if (state == State::Release)
        {
            envelopeVal -= (float) releaseRate;
            if (envelopeVal <= 0.0f)
            {
                envelopeVal = 0.0f;
                stop();
                return;
            }
        }

        // Sample playback
        int pos = (int) currentPosition;
        float alpha = (float) (currentPosition - pos);

        if (!isLooping && pos >= activeSound->sampleEnd)
        {
            stop();
            return;
        }

        for (size_t i = 0; i + 1 < targetChannels.size(); i += 2)
        {
            int srcCh = targetChannels[i];
            int outCh = targetChannels[i + 1];

            if (outCh >= 0 && outCh < numOutputChannels && srcCh >= 0 && srcCh < numSourceChannels && pos >= 0 && pos < sourceBuffer.getNumSamples())
            {
                float sample = sourceBuffer.getSample (srcCh, pos);
                
                float nextSample = 0.0f;
                if (isLooping && pos >= activeSound->loopEnd)
                {
                    nextSample = sourceBuffer.getSample (srcCh, activeSound->loopStart);
                }
                else if (pos + 1 < sourceBuffer.getNumSamples())
                {
                    nextSample = sourceBuffer.getSample (srcCh, pos + 1);
                }
                float interpolated = sample + alpha * (nextSample - sample);

                outputBuffer.addSample (outCh, startSample + s, interpolated * envelopeVal * currentVelocity);
            }
        }

        currentPosition += increment;
        
        if (isLooping)
        {
            if (currentPosition >= (double)activeSound->loopEnd + 1.0) // Passed the end of the loop
            {
                double loopLen = (double)activeSound->loopEnd - (double)activeSound->loopStart + 1.0;
                double over = currentPosition - ((double)activeSound->loopEnd + 1.0);
                if (loopLen > 0.0)
                    currentPosition = (double)activeSound->loopStart + std::fmod(over, loopLen);
                else
                    currentPosition = (double)activeSound->loopStart;
            }
        }
    }
}

void Sampler::handleMidiEvent (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        int note = message.getNoteNumber();
        int velocity = message.getVelocity();
        float velocityFloat = message.getFloatVelocity();
        int channel = message.getChannel();

        for (const auto& sound : sounds)
        {
            if (sound.midiNoteRange.contains (note) && sound.velocityRange.contains (velocity))
            {
                if (sound.group && sound.group->midiChannel != 0 && sound.group->midiChannel != channel)
                    continue;

                // Handle Mute Groups
                if (sound.muteGroup > 0) // Choke group 0 corresponds to no mute behaviour
                {
                    for (auto& v : voices)
                    {
                        if (v->isActive() && v->getSound() && v->getSound()->muteGroup == sound.muteGroup)
                            v->choke();
                    }
                }

                if (auto* voice = findFreeVoice())
                {
                    voice->start (&sound, note, velocityFloat, currentSampleRate);
                }
            }
        }
    }
    else if (message.isNoteOff())
    {
        int note = message.getNoteNumber();
        int channel = message.getChannel();
        for (auto& voice : voices)
        {
            if (voice->isActive() && voice->getSound())
            {
                if (voice->getSound()->group && voice->getSound()->group->midiChannel != 0 && voice->getSound()->group->midiChannel != channel)
                    continue;

                bool isOneShot = voice->getSound()->isOneShot;
                if (voice->getSound()->group) isOneShot = voice->getSound()->group->isOneShot;
                
                if (!isOneShot && voice->getNote() == note)
                {
                    voice->noteOff();
                }
            }
        }
    }
}

//==============================================================================
Sampler::Sampler()
{
    formatManager.registerBasicFormats();
    
    for (int i = 0; i < maxVoices; ++i)
        voices.push_back (std::make_unique<Voice>());
}

Sampler::~Sampler()
{
}

void Sampler::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::ignoreUnused (samplesPerBlock);
}   

void Sampler::addSound (const Sound& sound)
{
    sounds.push_back (sound);
    // Load the audio data into the buffer for the newly added sound
    loadSound (sounds.back());
}

void Sampler::loadSamplesFromXml (const void* xmlData, int xmlSize)
{
    if (xmlData == nullptr || xmlSize <= 0)
        return;

    juce::XmlDocument doc (juce::String::createStringFromData (xmlData, xmlSize));
    auto root = doc.getDocumentElement();

    if (root == nullptr || ! root->hasTagName ("Mappings"))
        return;

    sampleCache.clear(); // Clear cache when loading new mapping

    std::vector<Sound> newSounds;
    std::vector<std::unique_ptr<SampleGroup>> newSampleGroups;
    int newNumOutputChannels = 2;

    // Parse Master settings
    auto* master = root->getChildByName ("Master");
    if (master != nullptr)
    {
        newNumOutputChannels = master->getStringAttribute ("channels", "2").getIntValue();
    }

    // Parse SampleGroups
    std::map<juce::String, SampleGroup*> groups;
    for (auto* child : root->getChildIterator())
    {
        if (child->hasTagName ("SampleGroup"))
        {
            auto group = std::make_unique<SampleGroup>();
            group->name = child->getStringAttribute ("name");
            group->muteGroup = child->getIntAttribute ("muteGroup");
            
            juce::String mCh = child->getStringAttribute ("midiChannel", "0");
            if (mCh.equalsIgnoreCase ("omni")) group->midiChannel = 0;
            else group->midiChannel = mCh.getIntValue();

            group->isOneShot = child->getBoolAttribute ("oneShot", true);
            group->isLoop = child->getBoolAttribute ("loop", false);
            group->attack = child->getDoubleAttribute ("attack", 0.001);
            group->decay = child->getDoubleAttribute ("decay", 0.0);
            group->sustain = child->getDoubleAttribute ("sustain", 1.0);
            group->release = child->getDoubleAttribute ("release", 0.1);
            group->detune = child->getDoubleAttribute ("detune", 0.0);
            group->randomDetune = child->getDoubleAttribute ("randomDetune", 0.0);
            group->groupLevel = child->getDoubleAttribute ("groupLevel", 0.0);
            group->minVelocityGain = child->getDoubleAttribute ("minVelocityGain", -40.0);

            // Parse output channels for the group
            group->outputChannels.clear();
            juce::String channelList = child->getStringAttribute ("channels", "0,1");
            auto tokens = juce::StringArray::fromTokens (channelList, ", ", "");
            for (auto& t : tokens)
            {
                if (t.trim().isEmpty())
                    continue;

                if (t.contains (":"))
                {
                    auto parts = juce::StringArray::fromTokens (t, ":", "");
                    if (parts.size() == 2)
                    {
                        group->outputChannels.push_back (parts[0].getIntValue()); // Source
                        group->outputChannels.push_back (parts[1].getIntValue()); // Dest
                    }
                }
                else
                {
                    group->outputChannels.push_back (-1); // Auto Source
                    group->outputChannels.push_back (t.getIntValue()); // Dest
                }
            }

            if (group->outputChannels.empty()) { group->outputChannels = { -1, 0, -1, 1 }; }
            groups[group->name] = group.get();
            newSampleGroups.push_back(std::move(group));
        }
    }

    for (auto* child : root->getChildIterator())
    {
        if (child->hasTagName ("Sound"))
        {
            Sound sound;
            sound.name = child->getStringAttribute ("name");
            
            sound.resourceName = child->getStringAttribute ("resource");
            juce::String resourceName = child->getStringAttribute ("resource").replaceCharacter ('.', '_').replaceCharacter (' ', '_');
            
            if (resourceName.isNotEmpty() && juce::CharacterFunctions::isDigit(resourceName[0]))
                resourceName = "_" + resourceName;

            sound.data = BinaryData::getNamedResource (resourceName.toRawUTF8(), sound.dataSize);

            if (sound.data == nullptr)
            {
                for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
                {
                    if (resourceName.equalsIgnoreCase (BinaryData::namedResourceList[i]))
                    {
                        sound.data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], sound.dataSize);
                        break;
                    }
                }
            }
            
            if (sound.data == nullptr)
            {
                DBG ("Warning: Could not find resource for sound: " << sound.name << " (resource: " << resourceName << ")");
            }

            sound.midiNoteRange = juce::Range<int> (child->getIntAttribute ("noteLow"), child->getIntAttribute ("noteHigh") + 1);
            sound.velocityRange = juce::Range<int> (child->getIntAttribute ("velLow"), child->getIntAttribute ("velHigh") + 1);
            sound.basePitch = child->getIntAttribute ("basePitch");

            sound.sampleStart = child->getIntAttribute ("sampleStart", 0);
            sound.sampleEnd = child->getIntAttribute ("sampleEnd", -1);
            sound.loopStart = child->getIntAttribute ("loopStart", 0);
            sound.loopEnd = child->getIntAttribute ("loopEnd", -1);
            
            // Apply Group settings
            juce::String groupName = child->getStringAttribute ("group");
            if (groups.count (groupName))
            {
                const auto* g = groups[groupName];
                sound.muteGroup = g->muteGroup;
                sound.isOneShot = g->isOneShot;
                sound.attack = g->attack;
                sound.decay = g->decay;
                sound.sustain = g->sustain;
                sound.release = g->release;
                sound.outputChannels = g->outputChannels;
                sound.group = groups[groupName];
            }
            else
            {
                // Fallback defaults if no group specified
                sound.outputChannels = { -1, 0, -1, 1 };
            }

            loadSound (sound);

            // Resolve defaults for end points if they were -1 or invalid
            if (sound.audioBuffer)
            {
                int numSamples = sound.audioBuffer->getNumSamples();
                if (sound.sampleEnd == -1 || sound.sampleEnd > numSamples) sound.sampleEnd = numSamples;
                if (sound.sampleStart < 0) sound.sampleStart = 0;
                if (sound.sampleStart > sound.sampleEnd) sound.sampleStart = sound.sampleEnd;

                if (sound.loopEnd == -1 || sound.loopEnd >= numSamples) sound.loopEnd = sound.sampleEnd - 1;
                if (sound.loopStart < 0) sound.loopStart = 0;
                if (sound.loopStart > sound.loopEnd) sound.loopStart = sound.loopEnd;
                
                if (sound.sampleEnd < 0) sound.sampleEnd = 0;
                if (sound.loopEnd < 0) sound.loopEnd = 0;
            }

            // Resolve auto-source channels (-1)
            int numSourceChannels = sound.audioBuffer ? sound.audioBuffer->getNumChannels() : 2;
            for (size_t i = 0; i < sound.outputChannels.size(); i += 2)
            {
                if (sound.outputChannels[i] == -1)
                {
                    int pairIndex = (int)(i / 2);
                    sound.outputChannels[i] = (numSourceChannels > 1) ? pairIndex : 0;
                }
            }

            newSounds.push_back (sound);
        }
    }

    for (auto& v : voices) v->stop();

    sounds = std::move(newSounds);
    sampleGroups = std::move(newSampleGroups);
    numOutputChannels = newNumOutputChannels;
}

void Sampler::assignParameters (juce::AudioProcessorValueTreeState& apvts)
{
    for (auto& group : sampleGroups)
    {
        juce::String prefix = group->getName() + "_";
        group->oneShotParam = apvts.getRawParameterValue (prefix + "OneShot");
        group->midiChannelParam = apvts.getRawParameterValue (prefix + "MidiChannel");
        group->attackParam = apvts.getRawParameterValue (prefix + "Attack");
        group->decayParam = apvts.getRawParameterValue (prefix + "Decay");
        group->sustainParam = apvts.getRawParameterValue (prefix + "Sustain");
        group->releaseParam = apvts.getRawParameterValue (prefix + "Release");
        group->detuneParam = apvts.getRawParameterValue (prefix + "Detune");
        group->randomDetuneParam = apvts.getRawParameterValue (prefix + "RandomDetune");
        group->minVelocityGainParam = apvts.getRawParameterValue (prefix + "MinVelGain");
        group->groupLevelParam = apvts.getRawParameterValue (prefix + "GroupLevel");
    }
}

void Sampler::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    for (auto& group : sampleGroups)
        group->addParameters (params);
}

void SampleGroup::addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params)
{
    juce::String prefix = name + "_";
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { prefix + "OneShot", 1 }, name + " One Shot", isOneShot));
    params.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { prefix + "MidiChannel", 1 }, name + " MIDI Channel", 0, 16, midiChannel));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "Attack", 1 }, name + " Attack", 0.0f, 5.0f, (float)attack));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "Decay", 1 }, name + " Decay", 0.0f, 5.0f, (float)decay));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "Sustain", 1 }, name + " Sustain", 0.0f, 1.0f, (float)sustain));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "Release", 1 }, name + " Release", 0.0f, 5.0f, (float)release));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "Detune", 1 }, name + " Detune", -12.0f, 12.0f, (float)detune));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "RandomDetune", 1 }, name + " Random Detune", 0.0f, 100.0f, (float)randomDetune));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "MinVelGain", 1 }, name + " Min Vel Gain", -40.0f, 0.0f, (float)minVelocityGain));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "GroupLevel", 1 }, name + " Group Level", -12.0f, 6.0f, (float)groupLevel));
}

void Sampler::updateParams()
{
    for (auto& group : sampleGroups)
    {
        if (group->oneShotParam) group->isOneShot = *group->oneShotParam > 0.5f;
        if (group->midiChannelParam) group->midiChannel = (int)*group->midiChannelParam;
        if (group->attackParam) group->attack = *group->attackParam;
        if (group->decayParam) group->decay = *group->decayParam;
        if (group->sustainParam) group->sustain = *group->sustainParam;
        if (group->releaseParam) group->release = *group->releaseParam;
        if (group->detuneParam) group->detune = *group->detuneParam;
        if (group->randomDetuneParam) group->randomDetune = *group->randomDetuneParam;
        if (group->groupLevelParam) group->groupLevel = *group->groupLevelParam;
        if (group->minVelocityGainParam) group->minVelocityGain = *group->minVelocityGainParam;
    }
}

Voice* Sampler::findFreeVoice()
{
    for (auto& v : voices)
    {
        if (!v->isActive())
            return v.get();
    }
    // If all voices are busy, steal the first one (simplest voice stealing)
    return voices[0].get();
}

void Sampler::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const int numSamples = buffer.getNumSamples();
    int currentSample = 0;

    juce::MidiBuffer::Iterator midiIterator (midiMessages);
    juce::MidiMessage message;
    int samplePosition;

    while (midiIterator.getNextEvent (message, samplePosition))
    {
        const int samplesToProcess = samplePosition - currentSample;

        if (samplesToProcess > 0)
        {
            for (auto& voice : voices)
            {
                if (voice->isActive())
                    voice->renderNextBlock (buffer, currentSample, samplesToProcess);
            }
        }

        currentSample = samplePosition;
        handleMidiEvent (message);
    }

    if (currentSample < numSamples)
    {
        const int samplesToProcess = numSamples - currentSample;
        for (auto& voice : voices)
        {
            if (voice->isActive())
                voice->renderNextBlock (buffer, currentSample, samplesToProcess);
        }
    }
}
