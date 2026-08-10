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
 * @struct SampleGroup
 * @brief Represents a group of samples sharing common properties like ADSR and routing.
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

    const Sound* activeSound = nullptr;
    int currentNote = 0;

    /** Output samples of silence still owed before the sample starts, from a
        positive SampleGroup::startOffset. Counted in output samples rather
        than source samples so the delay is the requested wall-clock time
        whatever the playback increment is doing. */
    int delaySamplesRemaining = 0;

    double currentPosition = 0.0;
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
