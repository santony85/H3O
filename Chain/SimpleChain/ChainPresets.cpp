#include "ChainPresets.h"
#include "ChainProcessor.h"

juce::File ChainPresets::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("AudioFX").getChildFile ("Simple Chain Presets");
}

juce::Array<juce::File> ChainPresets::list() const
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);

    struct ByName
    {
        static int compareElements (const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension());
        }
    } byName;
    files.sort (byName);
    return files;
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

    if (const auto result = folder.createDirectory(); result.failed())
        return "Can't create the presets folder: " + result.getErrorMessage();

    const auto file = fileFor (name);
    chain.presetName = file.getFileNameWithoutExtension();

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
        return file.getFileNameWithoutExtension() + " is not a Simple Chain preset";

    chain.presetName = file.getFileNameWithoutExtension();
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
        if (files[i].getFileNameWithoutExtension() == chain.presetName)
            index = i;

    if (index < 0)
        return direction > 0 ? files.getFirst() : files.getLast();

    return files[(index + direction + files.size()) % files.size()];
}

bool ChainPresets::remove (const juce::File& file)
{
    if (! file.existsAsFile() || ! file.moveToTrash())
        return false;

    if (file.getFileNameWithoutExtension() == chain.presetName)
        chain.presetName.clear();
    return true;
}
