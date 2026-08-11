/*
  ==============================================================================

    ResourceProvider.cpp

  ==============================================================================
*/

#include "ResourceProvider.h"

namespace fxsampler
{

//==============================================================================
// A mapping may name a resource with a path ("wav/Kick.wav"); only the file
// name is ever significant, here as in juce_add_binary_data.
static juce::String fileNameOnly (const juce::String& name)
{
    return name.fromLastOccurrenceOf ("/", false, false)
               .fromLastOccurrenceOf ("\\", false, false);
}

juce::String ResourceProvider::makeIdentifier (const juce::String& fileName)
{
    // Deliberately identical to JUCE's makeBinaryDataIdentifierName: space and
    // '.' become '_' and everything outside [A-Za-z0-9_] is *dropped*, not
    // replaced, so "Room-1.wav" compiles to "Room1_wav".
    auto s = fileNameOnly (fileName)
                 .replaceCharacters (" .", "__")
                 .retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "_0123456789");

    if (s.isEmpty())
        return "unknown";

    if (juce::CharacterFunctions::isDigit (s[0]))
        s = "_" + s;

    return s;
}

const char* ResourceProvider::find (const juce::String& fileName, int& sizeOut) const
{
    if (auto* data = lookup (fileName, sizeOut))
        return data;

    if (fileName.isNotEmpty())
        misses.addIfNotAlreadyThere (fileName);

    sizeOut = 0;
    return nullptr;
}

//==============================================================================
const char* EmbeddedResources::lookup (const juce::String& fileName, int& sizeOut) const
{
    sizeOut = 0;

    if (fileName.isEmpty())
        return nullptr;

    const auto identifier = makeIdentifier (fileName);

    if (auto* data = BinaryData::getNamedResource (identifier.toRawUTF8(), sizeOut))
        return data;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (identifier.equalsIgnoreCase (BinaryData::namedResourceList[i]))
            return BinaryData::getNamedResource (BinaryData::namedResourceList[i], sizeOut);

    // Last chance: the real file names, which juce_add_binary_data keeps
    // alongside the identifiers. This is what resolves a mapping whose
    // capitalisation drifted from the file on disk.
    const auto bare = fileNameOnly (fileName);

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (bare.equalsIgnoreCase (BinaryData::originalFilenames[i]))
            return BinaryData::getNamedResource (BinaryData::namedResourceList[i], sizeOut);

    sizeOut = 0;
    return nullptr;
}

juce::String EmbeddedResources::describe() const
{
    return "embedded (" + juce::String (BinaryData::namedResourceListSize) + " resources)";
}

const ResourceProvider& embeddedResources()
{
    static EmbeddedResources instance;
    return instance;
}

//==============================================================================
FolderResources::FolderResources (const juce::File& kitFolder,
                                  const ResourceProvider* fallbackProvider)
    : folder (kitFolder), fallback (fallbackProvider)
{
    if (! folder.isDirectory())
    {
        warnings.add ("Not a directory: " + folder.getFullPathName());
        return;
    }

    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
    {
        const auto file = entry.getFile();
        const auto name = file.getFileName();

        // Editor backups, .DS_Store, and anything else the OS scattered about.
        if (name.startsWithChar ('.'))
            continue;

        const auto key = name.toLowerCase();
        const auto existing = byName.find (key);

        if (existing != byName.end())
        {
            warnings.add ("Two files named " + name + "; using "
                              + existing->second.getRelativePathFrom (folder));
            continue;
        }

        byName[key] = file;
        byIdentifier[makeIdentifier (name).toLowerCase()] = file;
    }

    if (auto it = byName.find ("mapping.xml"); it != byName.end())
        mappingFile = it->second;
    else
        warnings.add ("No mapping.xml in " + folder.getFullPathName());

    for (const auto& dir : folder.findChildFiles (juce::File::findDirectories, false))
        if (dir.getFileName().equalsIgnoreCase ("presets"))
            presetFolder = dir;

    // Not an error: the first preset saved from the dev host creates it.
    if (presetFolder == juce::File())
        presetFolder = folder.getChildFile ("presets");
}

const char* FolderResources::read (const juce::File& file, int& sizeOut) const
{
    const auto path = file.getFullPathName();
    auto it = cache.find (path);

    if (it == cache.end())
    {
        juce::MemoryBlock block;

        if (! file.loadFileAsData (block))
        {
            sizeOut = 0;
            return nullptr;
        }

        it = cache.emplace (path, std::move (block)).first;
    }

    sizeOut = (int) it->second.getSize();
    return static_cast<const char*> (it->second.getData());
}

const char* FolderResources::lookup (const juce::String& fileName, int& sizeOut) const
{
    sizeOut = 0;

    if (fileName.isEmpty())
        return nullptr;

    const auto bare = fileNameOnly (fileName);

    if (auto it = byName.find (bare.toLowerCase()); it != byName.end())
        return read (it->second, sizeOut);

    // A mapping written against an embedded kit may name a resource the way
    // BinaryData mangled it rather than the way the file is spelled.
    if (auto it = byIdentifier.find (makeIdentifier (fileName).toLowerCase()); it != byIdentifier.end())
        return read (it->second, sizeOut);

    // Shared artwork and the impulse responses live in the binary, not in the
    // kit folder being authored.
    if (fallback != nullptr)
        if (auto* data = fallback->lookup (fileName, sizeOut))
            return data;

    sizeOut = 0;
    return nullptr;
}

juce::String FolderResources::describe() const
{
    return folder.getFullPathName() + " (" + juce::String (getNumFiles()) + " files)";
}

} // namespace fxsampler
