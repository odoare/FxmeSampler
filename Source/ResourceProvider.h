/*
  ==============================================================================

    ResourceProvider.h

    Where a kit's mapping, samples and artwork come from.

    A mapping refers to its files by plain name — resource="Kick.wav",
    img="Room.jpg" — and everything that resolves one of those names goes
    through a ResourceProvider. Two implementations exist:

      * EmbeddedResources reads the BinaryData arrays compiled into the plugin.
        This is what every shipping kit uses, and it is the default everywhere,
        so a kit target needs no knowledge of this file.

      * FolderResources reads a directory on disk. It is what the standalone
        dev host (Dev/Main.cpp) uses to load a kit that has never been
        compiled, and it falls back to the embedded set for anything the
        folder does not hold — shared artwork, the impulse responses — so a
        test folder only has to contain what is actually being authored.

    The point of the split is that the *same* mapping.xml resolves identically
    either way: names are matched against real file names first and against
    juce_add_binary_data's mangled identifiers second, so a kit that plays in
    the dev host plays the same once embedded.

    Threading: providers are built and used from the message thread while a
    processor is being constructed, and are read-only afterwards. Nothing here
    is safe to call from the audio thread.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <map>

namespace fxsampler
{

//==============================================================================
/**
 * @brief Resolves a resource name from a mapping to raw file bytes.
 *
 * Lifetime: the bytes a find() returns stay valid for as long as the provider
 * does, and Sound::data holds on to them, so a provider must outlive every
 * Sampler and Mixer built from it.
 */
class ResourceProvider
{
public:
    virtual ~ResourceProvider() = default;

    /**
     * @brief Looks up a resource by the name a mapping uses for it.
     * @param fileName Name as written in mapping.xml, e.g. "Room IR1.wav".
     * @param sizeOut Receives the size in bytes; set to 0 on failure.
     * @return Pointer to the bytes, or nullptr if the name is not known.
     *
     * A failure is recorded in getMisses(). This is the call every loader
     * makes.
     */
    const char* find (const juce::String& fileName, int& sizeOut) const;

    /**
     * @brief find() without the miss being recorded.
     *
     * Public only so one provider can chain to another: a folder that has no
     * match asks the embedded set, and a name the folder simply does not carry
     * is not a miss until both have failed.
     */
    virtual const char* lookup (const juce::String& fileName, int& sizeOut) const = 0;

    /** @brief One line naming the source, for the dev host's status bar. */
    virtual juce::String describe() const = 0;

    /** @brief Names that were asked for and not found, in request order.
     *
     *  Collected here rather than at each call site because a missing sample
     *  is otherwise invisible outside a debug build: Sampler and Mixer both
     *  just carry on with silence and no image. The dev host shows this list. */
    const juce::StringArray& getMisses() const noexcept { return misses; }

    /**
     * @brief The identifier juce_add_binary_data would generate for a file.
     *
     * Mirrors JUCE's makeBinaryDataIdentifierName: space and '.' become '_',
     * every other non-alphanumeric character is dropped, and a leading digit
     * gets an underscore in front of it. "1 Bass A2.wav" becomes
     * "_1_Bass_A2_wav".
     */
    static juce::String makeIdentifier (const juce::String& fileName);

private:
    mutable juce::StringArray misses;
};

//==============================================================================
/**
 * @brief The BinaryData arrays compiled into this binary.
 *
 * Matching is done three ways, in order: the mangled identifier exactly, the
 * mangled identifier ignoring case, then the original file name ignoring case
 * (juce_add_binary_data keeps those in BinaryData::originalFilenames). The
 * last one is what lets a mapping write "Forest Short.wav" for a file actually
 * named "Forest short.wav".
 */
class EmbeddedResources final : public ResourceProvider
{
public:
    const char* lookup (const juce::String& fileName, int& sizeOut) const override;
    juce::String describe() const override;
};

/** @brief The shared embedded provider. Default for every kit. */
const ResourceProvider& embeddedResources();

//==============================================================================
/**
 * @brief A kit folder on disk: mapping.xml plus wav/, img/ and presets/.
 *
 * The folder is indexed once, recursively, at construction; files are read and
 * cached the first time they are asked for. Sub-directory layout is not
 * enforced — only file names matter — so a flat folder works just as well as
 * the wav/img/presets convention.
 */
class FolderResources final : public ResourceProvider
{
public:
    /**
     * @param kitFolder Directory to index.
     * @param fallbackProvider Consulted when the folder has no match, for
     *        artwork and impulse responses shared across kits. Pass nullptr
     *        to make the folder the only source.
     */
    explicit FolderResources (const juce::File& kitFolder,
                              const ResourceProvider* fallbackProvider = &embeddedResources());

    const char* lookup (const juce::String& fileName, int& sizeOut) const override;
    juce::String describe() const override;

    /** @brief True when the folder holds a mapping.xml and can be loaded. */
    bool isValid() const noexcept { return mappingFile.existsAsFile(); }

    juce::File getFolder() const { return folder; }
    juce::File getMappingFile() const { return mappingFile; }

    /** @brief The folder's preset directory, whatever its capitalisation.
     *  Handed to the processor as its *user* preset directory, so presets
     *  saved while testing land in the kit folder ready to be embedded. */
    juce::File getPresetFolder() const { return presetFolder; }

    /** @brief Problems found while indexing: no mapping, duplicate names. */
    const juce::StringArray& getWarnings() const noexcept { return warnings; }

    /** @brief How many files were indexed. */
    int getNumFiles() const noexcept { return (int) byName.size(); }

private:
    const char* read (const juce::File& file, int& sizeOut) const;

    juce::File folder, mappingFile, presetFolder;
    const ResourceProvider* fallback = nullptr;

    // Both keyed lowercase: file names as they appear on disk, and the
    // BinaryData identifiers those names would compile to.
    std::map<juce::String, juce::File> byName, byIdentifier;

    // Keyed by full path. A std::map because find() hands out pointers into
    // the blocks and they must not move when another file is read.
    mutable std::map<juce::String, juce::MemoryBlock> cache;

    juce::StringArray warnings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FolderResources)
};

} // namespace fxsampler
