/*
  ==============================================================================

    Sampler.h
    Created: 30 Jan 2026 3:56:29pm
    Author:  doare

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>

//==============================================================================
/**
 * @brief What happens when a looping note is released.
 */
enum class ReleaseMode
{
    /** Keep looping and let the ADSR release fade it out. */
    Loop,

    /** Jump to the sound's releaseStart and play the recorded tail once,
        crossfading out of the loop so the jump does not click.

        The ADSR release is not used: the envelope holds at its sustain level and
        the voice ends when the tail reaches sampleEnd, so what you hear is the
        instrument's own decay rather than a synthesised one. A choke still fades
        the voice out, tail and all. */
    Region
};

/**
 * @brief Gain law used to blend the two sides of a crossfade.
 *
 * EqualPower holds the summed power constant, which suits loop halves that are
 * only loosely correlated (noise, room tails, ensemble). Linear holds the
 * summed amplitude constant, which is better when the two sides are nearly the
 * same waveform, where equal-power would bulge in the middle.
 */
enum class CrossfadeShape
{
    EqualPower,
    Linear
};

//==============================================================================
/**
 * @struct SampleGroup
 * @brief Represents a group of samples sharing common properties like ADSR and routing.
 *
 * Playback regions
 * ----------------
 * A sound is described by five ordered points:
 *
 *     sampleStart <= loopStart < loopEnd <= releaseStart <= sampleEnd
 *
 *   sampleStart .. loopStart   attack, played once
 *   loopStart   .. loopEnd     loop, repeated while the note is held
 *   releaseStart.. sampleEnd   release tail, played once (ReleaseMode::Region)
 *
 * A one-shot sound ignores all of it and simply plays sampleStart..sampleEnd,
 * which is what every kit in this repository currently does.
 */
struct SampleGroup
{
    juce::String name;
    int muteGroup = 0;
    int midiChannel = 0; // 0 = omni, 1-16 = specific channel
    bool isOneShot = true;
    bool isLoop = false;
    double attack = 0.001;
    double decay = 0.0;
    double sustain = 1.0;
    double release = 0.1;
    double detune = 0.0;
    double randomDetune = 0.0;
    double groupLevel = 0.0;
    double minVelocityGain = -40.0;

    /** Playback start offset in milliseconds, -10 to +10.

        Positive holds the voice silent for that long before the sample starts,
        so the group sits later in time. Negative throws away that much of the
        head of the sample instead, so it sits earlier. Zero is the mapping's
        own sampleStart.

        Meant for sliding a close mic against an ambient one: the two are the
        same performance a metre or so apart, so a millisecond either way is
        the difference between reinforcement and cancellation. */
    double startOffset = 0.0;

    /** Loop crossfade length in milliseconds, 0 for a hard seam.

        The fade runs backwards from loopEnd and reads the material immediately
        before loopStart, so it eats into the attack region. Each voice clamps
        it at note start against what that sound can actually afford, which is
        the smaller of the loop length and the distance from sampleStart to
        loopStart.

        Also sets the length of the jump into the release region, where it gets
        a floor instead: see ReleaseMode::Region. */
    double crossfadeMs = 0.0;

    CrossfadeShape crossfadeShape = CrossfadeShape::EqualPower;
    ReleaseMode releaseMode = ReleaseMode::Loop;

    std::vector<int> outputChannels;

    // Parameter pointers
    std::atomic<float>* oneShotParam = nullptr;
    std::atomic<float>* midiChannelParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* detuneParam = nullptr;
    std::atomic<float>* randomDetuneParam = nullptr;
    std::atomic<float>* groupLevelParam = nullptr;
    std::atomic<float>* minVelocityGainParam = nullptr;
    std::atomic<float>* startOffsetParam = nullptr;
    std::atomic<float>* loopParam = nullptr;
    std::atomic<float>* crossfadeParam = nullptr;
    std::atomic<float>* releaseModeParam = nullptr;

    /**
     * @brief Gets the name of the sample group.
     * @return The name.
     */
    juce::String getName() const { return name; }
    void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);
};

/**
 * @struct Sound
 * @brief Represents a single sample sound with mapping and playback properties.
 */
struct Sound
{
    juce::String name;
    juce::String resourceName;
    const char* data = nullptr;
    int dataSize = 0;
    juce::Range<int> midiNoteRange { 0, 127 };
    int basePitch = 60;
    int muteGroup = 0;
    bool isOneShot = true;
    int sampleStart = 0;
    int sampleEnd = 0;
    int loopStart = 0;
    int loopEnd = 0;

    /** Where the release tail begins, for ReleaseMode::Region. Defaults to
        loopEnd, making the tail everything the loop does not cover. */
    int releaseStart = 0;
    juce::Range<int> velocityRange { 0, 128 };
    std::vector<int> outputChannels;
    double attack = 0.001;  // Seconds
    double decay = 0.0;     // Seconds
    double sustain = 1.0;   // 0.0 to 1.0
    double release = 0.1;   // Seconds
    
    SampleGroup* group = nullptr;

    // Internal audio data
    std::shared_ptr<juce::AudioBuffer<float>> audioBuffer;
    double sourceSampleRate = 44100.0;
};

//==============================================================================
/**
 * @class Voice
 * @brief A single voice that plays back a Sound.
 */
class Voice
{
public:
    /**
     * @brief Default constructor.
     */
    Voice() = default;

    /**
     * @brief Starts playing a sound.
     * @param sound The sound to play.
     * @param note The MIDI note number.
     * @param velocity The velocity (0.0 to 1.0).
     * @param sampleRate The current sample rate.
     */
    void start (const Sound* sound, int note, float velocity, double sampleRate);

    /**
     * @brief Stops the voice immediately.
     */
    void stop();

    /**
     * @brief Chokes the voice (fast release).
     */
    void choke();

    /**
     * @brief Triggers the release phase of the envelope.
     */
    void noteOff();

    /**
     * @brief Checks if the voice is currently active.
     * @return True if active, false otherwise.
     */
    bool isActive() const;

    /**
     * @brief Gets the currently playing sound.
     * @return Pointer to the active sound, or nullptr.
     */
    const Sound* getSound() const { return activeSound; }

    /**
     * @brief Gets the MIDI note number currently being played.
     * @return The note number.
     */
    int getNote() const { return currentNote; }

    /**
     * @brief Renders the next block of audio for this voice.
     * @param outputBuffer The buffer to mix audio into.
     * @param startSample The start sample index in the buffer.
     * @param numSamples The number of samples to render.
     */
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);

private:
    enum class State { Attack, Decay, Sustain, Release, Idle };
    State state = State::Idle;

    /** Which part of the sound the read head is in.

        Body covers the attack and the loop, which the head cannot tell apart:
        it simply runs forward and wraps. Release is the tail after the note has
        been let go, and only ReleaseMode::Region ever reaches it.

        Deliberately separate from State, which is the envelope's business. In
        Region mode the envelope stays in Sustain while the head plays the tail,
        so one enum could not describe both. */
    enum class Region { Body, Release };
    Region region = Region::Body;

    /** Shortest jump fade into the release region, in milliseconds.

        Unlike the loop seam, this one cannot be authored away: the head leaves
        the loop at whatever phase the note-off happened to fall on, so there is
        always a step. A few milliseconds is enough to hide it. */
    static constexpr double minReleaseFadeMs = 5.0;

    /** True when this voice should wrap at the loop points rather than run to
        sampleEnd. Looping is a group setting and a one-shot never loops. */
    bool isLoopingNow() const;

    /** True when a note-off should jump to the release region rather than run
        the envelope's release over the loop. */
    bool usesReleaseRegion() const;

    /** Moves the main head to releaseStart, hands the loop over to a second
        head, and arms the fade between them. */
    void enterReleaseRegion();

    /** Longest crossfade this sound can afford, in source samples: it must be
        shorter than the loop, and it reads backwards from loopStart, so it
        cannot reach further back than sampleStart. */
    double maxCrossfadeSamples() const;

    /** The group's crossfade length clamped by maxCrossfadeSamples(). */
    double requestedCrossfadeSamples() const;

    /** The group's gain law evaluated at a fade progress of 0 to 1: gainOut
        goes 1 to 0, gainIn goes 0 to 1. Shared by the loop seam and the release
        jump so a group fades the same way wherever it fades. */
    void fadeGains (double g, double& gainOut, double& gainIn) const;

    /** Crossfade gains for a position inside the fade window. Returns false
        when the position is not in the window, leaving the gains untouched. */
    bool crossfadeGains (double position, double& gainMain, double& gainNext) const;

    /** Gains for the jump into the release region, driven by how far the main
        head has travelled past releaseStart. Returns false once the fade is
        over, which is also when the loop head stops being read. */
    bool releaseFadeGains (double& gainRelease, double& gainLoop) const;

    /** Linearly interpolated read at a fractional position, returning silence
        outside the buffer. With wrapAtLoop, the sample following loopEnd - 1 is
        loopStart, so the interpolation across the seam is continuous.

        Kept separate because the crossfade reads two positions per sample. */
    float readInterpolated (const juce::AudioBuffer<float>& source, int channel,
                            double position, bool wrapAtLoop) const;

    /** Advances the ADSR by one sample. Returns false when the voice has ended,
        in which case it has already been stopped. */
    bool advanceEnvelope();

    /** Advances a read position by one output sample, wrapping at the loop when
        asked. Takes the position by reference because the release fade runs two
        heads over the same sound at the same increment. */
    void advance (double& position, bool wrapAtLoop) const;

    const Sound* activeSound = nullptr;
    int currentNote = 0;

    /** Output samples of silence still owed before the sample starts, from a
        positive SampleGroup::startOffset. Counted in output samples rather
        than source samples so the delay is the requested wall-clock time
        whatever the playback increment is doing. */
    int delaySamplesRemaining = 0;

    double currentPosition = 0.0;

    /** Second head, alive only during the fade into the release region: it
        carries on looping where the main head left off so the loop can be faded
        out instead of cut. */
    double loopPosition = 0.0;

    /** Length of the release jump fade in source samples, frozen at note-off so
        a parameter change cannot move the gains mid-fade. */
    double releaseFadeSamples = 0.0;

    /** Active loop crossfade length in source samples, 0 for a hard seam.
        Refreshed per block from the group parameter, but never while the read
        head is inside a fade, since changing the width mid-fade would jump the
        gains and click. */
    double crossfadeSamples = 0.0;

    double increment = 1.0;
    double currentSampleRate = 44100.0;
    float currentVelocity = 0.0f;
    float envelopeVal = 0.0f;
    double attackRate = 0.0;
    double decayRate = 0.0;
    double sustainLevel = 0.0;
    double releaseRate = 0.0;
    juce::Random random;
};

//==============================================================================
/**
 * @class Sampler
 * @brief The main sampler engine managing sounds and voices.
 *
 * Threading: the sound and group collections are built once by
 * loadSamplesFromXml/assignParameters from the processor constructor, before
 * the processor is live, and are only read afterwards. processBlock and
 * updateParams therefore run lock-free on the audio thread.
 *
 * If a kit ever needs reloading at runtime, do NOT reintroduce a lock around
 * processBlock: publish the new sounds/groups with an atomic swap (or a
 * message-thread suspendProcessing) instead, so the audio thread never blocks.
 */
class Sampler
{
public:
    /**
     * @brief Constructor.
     */
    Sampler();

    /**
     * @brief Destructor.
     */
    ~Sampler();

    /**
     * @brief Prepares the sampler for playback.
     * @param sampleRate The sample rate.
     * @param samplesPerBlock The expected block size.
     */
    void prepareToPlay (double sampleRate, int samplesPerBlock);

    /**
     * @brief Adds a sound to the sampler.
     * @param sound The sound to add.
     */
    void addSound (const Sound& sound);

    /**
     * @brief Processes a block of audio and MIDI messages.
     * @param buffer The audio buffer to fill.
     * @param midiMessages The MIDI messages to process.
     */
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    /**
     * @brief Loads sample configuration from XML data.
     * @param xmlData Pointer to the XML data.
     * @param xmlSize Size of the XML data.
     */
    void loadSamplesFromXml (const void* xmlData, int xmlSize);

    /**
     * @brief Assigns parameters from APVTS to sample groups.
     * @param apvts The AudioProcessorValueTreeState.
     */
    void assignParameters (juce::AudioProcessorValueTreeState& apvts);

    /**
     * @brief Updates parameters for all sample groups.
     */
    void updateParams();
    
    /**
     * @brief Gets the number of output channels required.
     * @return The number of channels.
     */
    int getNumOutputChannels() const { return numOutputChannels; }

    /**
     * @brief Gets the list of sample groups.
     * @return Vector of unique pointers to SampleGroups.
     */
    const std::vector<std::unique_ptr<SampleGroup>>& getSampleGroups() const { return sampleGroups; }
    void addParameters (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params);

private:
    std::vector<Sound> sounds;
    std::vector<std::unique_ptr<Voice>> voices;
    static const int maxVoices = 64;
    double currentSampleRate = 44100.0;
    juce::AudioFormatManager formatManager;
    
    int numOutputChannels = 2;
    std::vector<std::unique_ptr<SampleGroup>> sampleGroups;
    
    // Cache to share audio buffers among sounds using the same resource
    std::map<juce::String, std::pair<std::shared_ptr<juce::AudioBuffer<float>>, double>> sampleCache;

    Voice* findFreeVoice();
    void loadSound (Sound& sound);
    void handleMidiEvent (const juce::MidiMessage& message);
};
