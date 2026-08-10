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
    delaySamplesRemaining = 0;
    crossfadeSamples = 0.0;   // adopted from the group at the first block
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

        // Start offset. Positive owes the voice some silence before the sample
        // begins; negative discards that much of its head instead.
        //
        // The delay is counted in output samples because it is a wall-clock
        // shift, while the skip is counted in source samples because it is an
        // amount of recorded material. Deliberately not implemented as a
        // negative read position: everything before sampleStart is the tail of
        // the previous stroke in the take file, not silence.
        if (activeSound->group != nullptr && activeSound->group->startOffset != 0.0)
        {
            const double offsetMs = activeSound->group->startOffset;

            if (offsetMs > 0.0)
            {
                delaySamplesRemaining = juce::roundToInt (offsetMs * 0.001 * currentSampleRate);
            }
            else
            {
                const double skip = -offsetMs * 0.001 * activeSound->sourceSampleRate;
                // Never skip the slice away entirely, or the voice would start
                // already past its end and never sound at all.
                currentPosition = juce::jlimit ((double) activeSound->sampleStart,
                                                juce::jmax ((double) activeSound->sampleStart,
                                                            (double) activeSound->sampleEnd - 1.0),
                                                (double) activeSound->sampleStart + skip);
            }
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
    delaySamplesRemaining = 0;
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

bool Voice::isLoopingNow() const
{
    // A one-shot ignores the loop points entirely. Looping is a group setting;
    // a sound with no group can never loop.
    if (activeSound == nullptr || activeSound->group == nullptr)
        return false;
    if (activeSound->group->isOneShot)
        return false;
    return activeSound->group->isLoop;
}

double Voice::maxCrossfadeSamples() const
{
    if (activeSound == nullptr)
        return 0.0;

    const double loopLen = (double) activeSound->loopEnd - (double) activeSound->loopStart;
    if (loopLen <= 1.0)
        return 0.0;

    // The second read head runs from loopStart - X to loopStart, so the fade
    // is limited by how much attack region there is in front of the loop.
    const double attackRoom = (double) activeSound->loopStart - (double) activeSound->sampleStart;

    return juce::jmax (0.0, juce::jmin (loopLen - 1.0, attackRoom));
}

double Voice::requestedCrossfadeSamples() const
{
    if (activeSound == nullptr || activeSound->group == nullptr)
        return 0.0;

    const double ms = activeSound->group->crossfadeMs;
    if (ms <= 0.0)
        return 0.0;

    return juce::jmin (ms * 0.001 * activeSound->sourceSampleRate, maxCrossfadeSamples());
}

bool Voice::crossfadeGains (double position, double& gainMain, double& gainNext) const
{
    if (crossfadeSamples <= 0.0 || activeSound == nullptr)
        return false;

    const double fadeFrom = (double) activeSound->loopEnd - crossfadeSamples;
    if (position < fadeFrom)
        return false;

    const double g = juce::jlimit (0.0, 1.0, (position - fadeFrom) / crossfadeSamples);

    if (activeSound->group != nullptr
        && activeSound->group->crossfadeShape == CrossfadeShape::Linear)
    {
        // Constant summed amplitude: right when the two sides are nearly the
        // same waveform, where equal power would bulge by 3 dB in the middle.
        gainMain = 1.0 - g;
        gainNext = g;
    }
    else
    {
        // Constant summed power, the default: right when the two sides are
        // only loosely correlated (room tails, ensemble, noise).
        //
        // Same approximation the pan law in MixerStrips uses, and about four
        // times cheaper than libm here, which matters because this runs per
        // sample per voice while a fade is open. Over 0..pi/2 the error is
        // ~7e-9, an order of magnitude under a 24-bit LSB, and a^2 + b^2 stays
        // within 1e-9 of unity so the fade really is constant power.
        //
        // Keep these doubles. The Pade coefficients reach 1.15e10, well past
        // float's exact-integer range, and evaluating them in float costs two
        // decimal digits of accuracy.
        const double angle = g * juce::MathConstants<double>::halfPi;
        gainMain = juce::dsp::FastMathApproximations::cos (angle);
        gainNext = juce::dsp::FastMathApproximations::sin (angle);
    }

    return true;
}

float Voice::readInterpolated (const juce::AudioBuffer<float>& source, int channel,
                               double position, bool wrapAtLoop) const
{
    const int pos = (int) position;

    if (pos < 0 || pos >= source.getNumSamples())
        return 0.0f;

    const float s0 = source.getSample (channel, pos);
    float s1 = 0.0f;

    // loopEnd is exclusive, like sampleEnd: the last sample of the loop is
    // loopEnd - 1, and the one after it is loopStart.
    if (wrapAtLoop && pos + 1 >= activeSound->loopEnd)
        s1 = source.getSample (channel, activeSound->loopStart);
    else if (pos + 1 < source.getNumSamples())
        s1 = source.getSample (channel, pos + 1);

    return s0 + (float) (position - pos) * (s1 - s0);
}

bool Voice::advanceEnvelope()
{
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
            return false;
        }
    }
    else if (state == State::Release)
    {
        envelopeVal -= (float) releaseRate;
        if (envelopeVal <= 0.0f)
        {
            envelopeVal = 0.0f;
            stop();
            return false;
        }
    }

    return true;
}

void Voice::advancePosition (bool isLooping)
{
    currentPosition += increment;

    if (! isLooping || currentPosition < (double) activeSound->loopEnd)
        return;

    const double loopLen = (double) activeSound->loopEnd - (double) activeSound->loopStart;
    const double over = currentPosition - (double) activeSound->loopEnd;

    if (loopLen > 0.0)
        currentPosition = (double) activeSound->loopStart + std::fmod (over, loopLen);
    else
        currentPosition = (double) activeSound->loopStart;
}

void Voice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (state == State::Idle || activeSound == nullptr || activeSound->audioBuffer == nullptr)
        return;

    const auto& sourceBuffer = *activeSound->audioBuffer;
    const int numSourceChannels = sourceBuffer.getNumChannels();
    const int numOutputChannels = outputBuffer.getNumChannels();
    const auto& targetChannels = activeSound->outputChannels;

    const bool isLooping = isLoopingNow();

    // Track the group's crossfade knob at block rate, but never adopt a new
    // width while the read head is already inside a fade: the gains are a
    // function of the width, so changing it mid-fade would step them.
    if (isLooping)
    {
        double unusedA = 0.0, unusedB = 0.0;
        if (! crossfadeGains (currentPosition, unusedA, unusedB))
            crossfadeSamples = requestedCrossfadeSamples();
    }
    else
    {
        crossfadeSamples = 0.0;
    }

    const double loopLen = (double) activeSound->loopEnd - (double) activeSound->loopStart;

    // With a crossfade running, the main head's interpolation partner is the
    // real next sample rather than loopStart: the fade is what joins the seam,
    // so wrapping here as well would apply the join twice.
    const bool wrapForInterpolation = isLooping && crossfadeSamples <= 0.0;

    for (int s = 0; s < numSamples; ++s)
    {
        // Silence still owed by a positive start offset. The envelope is held
        // at its very beginning rather than advanced, so the wait delays the
        // attack instead of eating it.
        if (delaySamplesRemaining > 0)
        {
            // A choke or note-off arriving during the wait ends the voice
            // outright: nothing is sounding yet, so there is nothing to fade.
            if (state == State::Release)
            {
                stop();
                return;
            }

            --delaySamplesRemaining;
            continue;
        }

        if (! advanceEnvelope())
            return;

        if (! isLooping && (int) currentPosition >= activeSound->sampleEnd)
        {
            stop();
            return;
        }

        // Hoisted out of the channel loop: it does not depend on the channel,
        // and skipping the whole loop leaves the output buffer untouched rather
        // than adding silence to it.
        const int pos = (int) currentPosition;

        // Once per sample, not per channel: the fade gains depend only on the
        // read position.
        double gainMain = 1.0, gainNext = 0.0;
        const bool fading = isLooping && crossfadeGains (currentPosition, gainMain, gainNext);

        if (pos >= 0 && pos < sourceBuffer.getNumSamples())
        {
            for (size_t i = 0; i + 1 < targetChannels.size(); i += 2)
            {
                const int srcCh = targetChannels[i];
                const int outCh = targetChannels[i + 1];

                if (outCh < 0 || outCh >= numOutputChannels
                    || srcCh < 0 || srcCh >= numSourceChannels)
                    continue;

                float sample = readInterpolated (sourceBuffer, srcCh, currentPosition,
                                                 wrapForInterpolation);

                if (fading)
                {
                    // The second head is simply one loop behind. As the main
                    // head sweeps loopEnd-X to loopEnd, this one sweeps
                    // loopStart-X to loopStart, which is exactly the material
                    // the loop is about to jump into. No second accumulator is
                    // needed, and it never wraps: it is reading pre-loop
                    // material, not looping over it.
                    const float behind = readInterpolated (sourceBuffer, srcCh,
                                                           currentPosition - loopLen, false);
                    sample = (float) (gainMain * sample + gainNext * behind);
                }

                outputBuffer.addSample (outCh, startSample + s, sample * envelopeVal * currentVelocity);
            }
        }

        advancePosition (isLooping);
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

    // The root tag is <Mapping>, matching the file name. <Mappings> is the
    // older spelling and is still accepted so an unconverted kit keeps loading.
    if (root == nullptr || ! (root->hasTagName ("Mapping") || root->hasTagName ("Mappings")))
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
            group->crossfadeMs = child->getDoubleAttribute ("crossfade", 0.0);

            group->crossfadeShape = child->getStringAttribute ("crossfadeShape", "equalPower")
                                        .equalsIgnoreCase ("linear")
                                            ? CrossfadeShape::Linear
                                            : CrossfadeShape::EqualPower;

            group->releaseMode = child->getStringAttribute ("releaseMode", "loop")
                                     .equalsIgnoreCase ("region")
                                         ? ReleaseMode::Region
                                         : ReleaseMode::Loop;

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
            sound.loopStart = child->getIntAttribute ("loopStart", -1);
            sound.loopEnd = child->getIntAttribute ("loopEnd", -1);
            sound.releaseStart = child->getIntAttribute ("releaseStart", -1);
            
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

            // Resolve the five region points and force them into order:
            //
            //     sampleStart <= loopStart < loopEnd <= releaseStart <= sampleEnd
            //
            // Unset values (-1) take a sensible default rather than 0. That
            // matters for these kits: a sound is a slice of a longer take, so
            // clamping loopStart to 0 rather than to sampleStart would let a
            // loop read backwards into the previous stroke.
            if (sound.audioBuffer)
            {
                const int numSamples = sound.audioBuffer->getNumSamples();

                if (sound.sampleEnd == -1 || sound.sampleEnd > numSamples) sound.sampleEnd = numSamples;
                sound.sampleEnd = juce::jlimit (0, numSamples, sound.sampleEnd);
                sound.sampleStart = juce::jlimit (0, sound.sampleEnd, sound.sampleStart);

                if (sound.loopStart < 0) sound.loopStart = sound.sampleStart;
                if (sound.loopEnd < 0 || sound.loopEnd > sound.sampleEnd) sound.loopEnd = sound.sampleEnd;
                if (sound.releaseStart < 0) sound.releaseStart = sound.loopEnd;

                const int requested[3] = { sound.loopStart, sound.loopEnd, sound.releaseStart };

                sound.loopStart    = juce::jlimit (sound.sampleStart, sound.sampleEnd, sound.loopStart);
                sound.loopEnd      = juce::jlimit (sound.loopStart,   sound.sampleEnd, sound.loopEnd);
                sound.releaseStart = juce::jlimit (sound.loopEnd,     sound.sampleEnd, sound.releaseStart);

                if (requested[0] != sound.loopStart || requested[1] != sound.loopEnd
                    || requested[2] != sound.releaseStart)
                {
                    DBG ("Warning: out-of-order region points in sound " << sound.name
                         << " (" << sound.resourceName << "): loopStart/loopEnd/releaseStart "
                         << requested[0] << "/" << requested[1] << "/" << requested[2]
                         << " clamped to " << sound.loopStart << "/" << sound.loopEnd
                         << "/" << sound.releaseStart
                         << " within " << sound.sampleStart << ".." << sound.sampleEnd);
                }
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
        group->startOffsetParam = apvts.getRawParameterValue (prefix + "StartOffset");
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
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "StartOffset", 1 }, name + " Start Offset", -10.0f, 10.0f, (float)startOffset));
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
        if (group->startOffsetParam) group->startOffset = *group->startOffsetParam;
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
