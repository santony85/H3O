#include "ChainPresets.h"
#include "ChainProcessor.h"

juce::File ChainPresets::defaultFolder()
{
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto folder = music.getChildFile ("H3O").getChildFile ("Chain Presets");

    // Avant le nom H3O, les presets étaient dans ~/Music/AudioFX/Simple Chain Presets : on les déplace une fois
    const auto old = music.getChildFile ("AudioFX").getChildFile ("Simple Chain Presets");
    if (! folder.exists() && old.isDirectory())
    {
        folder.getParentDirectory().createDirectory();
        if (old.moveFileTo (folder))
            if (old.getParentDirectory().getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
                old.getParentDirectory().deleteFile();
    }
    return folder;
}

juce::Array<juce::File> ChainPresets::list() const
{
    auto files = folder.findChildFiles (juce::File::findFiles, true, juce::String ("*") + extension);

    // Les presets de la racine d'abord, puis les sous-dossiers
    struct ByPath
    {
        const juce::File& root;
        int compareElements (const juce::File& a, const juce::File& b) const
        {
            const bool topA = a.getParentDirectory() == root, topB = b.getParentDirectory() == root;
            if (topA != topB)
                return topA ? -1 : 1;
            return a.getRelativePathFrom (root).compareNatural (b.getRelativePathFrom (root));
        }
    } byPath { folder };
    files.sort (byPath);
    return files;
}

juce::String ChainPresets::nameOf (const juce::File& file) const
{
    const auto path = file.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
    return path.dropLastCharacters (file.getFileExtension().length());
}

juce::File ChainPresets::current() const
{
    if (chain.presetName.isEmpty())
        return {};
    const auto file = folder.getChildFile (chain.presetName + extension);
    return file.isAChildOf (folder) ? file : juce::File();
}

namespace
{
    // « Lead / Solo » → « Lead - Solo » : les caractères interdits dans un nom de fichier deviennent des tirets
    juce::String legalName (const juce::String& name)
    {
        return juce::File::createLegalFileName (name.trim().replaceCharacters ("/\\:*?\"<>|", "---------")).trim();
    }
}

juce::File ChainPresets::fileFor (const juce::String& name) const
{
    return folder.getChildFile (legalName (name) + extension);
}

juce::String ChainPresets::save (const juce::String& name)
{
    if (legalName (name).isEmpty())
        return "Give the preset a name";

    return saveTo (fileFor (name));
}

juce::String ChainPresets::saveTo (const juce::File& file)
{
    if (! file.isAChildOf (folder))
        return "Presets live in " + folder.getFullPathName();

    if (const auto result = file.getParentDirectory().createDirectory(); result.failed())
        return "Can't create the presets folder: " + result.getErrorMessage();

    chain.presetName = nameOf (file);

    auto state = chain.createState();
    state.setProperty ("presetversion", 1, nullptr);

    const auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return "Can't write " + file.getFullPathName();

    return {};
}

juce::String ChainPresets::load (const juce::File& file)
{
    const auto xml = juce::parseXML (file);
    if (xml == nullptr || ! chain.restoreState (juce::ValueTree::fromXml (*xml), true))
        return file.getFileNameWithoutExtension() + " is not a H3O Chain preset";

    chain.presetName = nameOf (file);
    chain.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    return {};
}

juce::File ChainPresets::neighbour (int direction) const
{
    const auto files = list();
    if (files.isEmpty())
        return {};

    int index = -1;
    for (int i = 0; i < files.size(); ++i)
        if (nameOf (files[i]) == chain.presetName)
            index = i;

    if (index < 0)
        return direction > 0 ? files.getFirst() : files.getLast();

    return files[(index + direction + files.size()) % files.size()];
}

bool ChainPresets::remove (const juce::File& file)
{
    if (! file.existsAsFile() || ! file.moveToTrash())
        return false;

    if (nameOf (file) == chain.presetName)
        chain.presetName.clear();
    return true;
}
