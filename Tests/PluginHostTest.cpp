// Charge le vrai composant Audio Unit installé (comme Logic), lui fait lire un .nam
// via son état, et vérifie que le son passe bien par le modèle. Cible Xcode : PluginHostTest
//   ./PluginHostTest Samp "/chemin/modele.nam"     (Simple Amp)
//   ./PluginHostTest Schn "/chemin/modele.nam"     (Simple Chain, Amp seul activé)
#include <juce_audio_processors/juce_audio_processors.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>

namespace
{
    // Côté hôte, l'état d'un Audio Unit est une liste de propriétés Apple (ClassInfo).
    // L'état du plugin JUCE y est rangé sous la clé "jucePluginState".
    const CFStringRef juceKey = CFSTR ("jucePluginState");

    juce::MemoryBlock getPluginState (juce::AudioPluginInstance& plugin)
    {
        juce::MemoryBlock plist, result;
        plugin.getStateInformation (plist);

        auto data = CFDataCreate (nullptr, (const UInt8*) plist.getData(), (CFIndex) plist.getSize());
        auto dict = (CFDictionaryRef) CFPropertyListCreateWithData (nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);

        if (dict != nullptr)
        {
            if (auto state = (CFDataRef) CFDictionaryGetValue (dict, juceKey))
                result.append (CFDataGetBytePtr (state), (size_t) CFDataGetLength (state));
            CFRelease (dict);
        }

        CFRelease (data);
        return result;
    }

    void setPluginState (juce::AudioPluginInstance& plugin, const juce::MemoryBlock& newState)
    {
        juce::MemoryBlock plist;
        plugin.getStateInformation (plist);

        auto data = CFDataCreate (nullptr, (const UInt8*) plist.getData(), (CFIndex) plist.getSize());
        auto dict = (CFDictionaryRef) CFPropertyListCreateWithData (nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
        CFRelease (data);
        if (dict == nullptr)
            return;

        auto copy = CFDictionaryCreateMutableCopy (nullptr, 0, dict);
        auto state = CFDataCreate (nullptr, (const UInt8*) newState.getData(), (CFIndex) newState.getSize());
        CFDictionarySetValue (copy, juceKey, state);

        if (auto out = CFPropertyListCreateData (nullptr, copy, kCFPropertyListBinaryFormat_v1_0, 0, nullptr))
        {
            plugin.setStateInformation (CFDataGetBytePtr (out), (int) CFDataGetLength (out));
            CFRelease (out);
        }

        CFRelease (state);
        CFRelease (copy);
        CFRelease (dict);
    }
    std::vector<float> processSine (juce::AudioPluginInstance& plugin)
    {
        const double sr = 48000.0;
        const int block = 512;
        plugin.setRateAndBufferSizeDetails (sr, block);
        plugin.prepareToPlay (sr, block);

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        std::vector<float> out;

        for (int start = 0; start < (int) sr * 3; start += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const float x = (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * (start + i) / sr));
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            plugin.processBlock (buffer, midi);

            if (start > (int) sr)
                for (int i = 0; i < block; ++i)
                    out.push_back (buffer.getSample (0, i));
        }

        plugin.releaseResources();
        return out;
    }

    double levelDb (const std::vector<float>& x)
    {
        double e = 0.0;
        for (auto v : x) e += (double) v * v;
        return 10.0 * std::log10 (e / (double) x.size() + 1e-20);
    }

    // Modifie l'état du plugin : chemin du modèle, baffle coupé, Amp activé (dans la chaîne)
    void setModel (juce::AudioPluginInstance& plugin, const juce::String& code, const juce::String& modelPath)
    {
        const auto state = getPluginState (plugin);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (xml == nullptr)
        {
            std::printf ("ÉCHEC : état du plugin illisible\n");
            std::exit (1);
        }

        auto tree = juce::ValueTree::fromXml (*xml);

        auto editAmp = [&modelPath] (juce::ValueTree ampState)
        {
            ampState.setProperty ("modelPath", modelPath, nullptr);
            for (auto p : ampState)
                if (p.getProperty ("id") == "cab" || p.getProperty ("id") == "normalize")
                    p.setProperty ("value", 0.0f, nullptr);
                else if (p.getProperty ("id") == "input")
                    p.setProperty ("value", 0.0f, nullptr);   // Stereo
            return ampState;
        };

        if (code == "Samp")
        {
            tree = editAmp (tree);
        }
        else
        {
            // Simple Chain : l'état de l'Amp est rangé en base64 dans la propriété "amp"
            juce::MemoryBlock ampBlock;
            ampBlock.fromBase64Encoding (tree.getProperty ("amp").toString());
            auto ampXml = juce::AudioProcessor::getXmlFromBinary (ampBlock.getData(), (int) ampBlock.getSize());
            auto ampTree = editAmp (juce::ValueTree::fromXml (*ampXml));

            juce::MemoryBlock newAmp;
            juce::AudioProcessor::copyXmlToBinary (*ampTree.createXml(), newAmp);
            tree.setProperty ("amp", newAmp.toBase64Encoding(), nullptr);

            auto own = tree.getChildWithName ("CHAIN_PARAMETERS");
            for (auto p : own)
            {
                const auto id = p.getProperty ("id").toString();
                if (id == "input")
                    p.setProperty ("value", 0.0f, nullptr);
                else
                    p.setProperty ("value", id == "ampon" ? 1.0f : 0.0f, nullptr);   // Amp seul
            }
        }

        juce::MemoryBlock newState;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), newState);
        setPluginState (plugin, newState);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    if (argc < 3)
    {
        std::printf ("Usage : PluginHostTest <Samp|Schn> fichier.nam\n");
        return 2;
    }

    const juce::String code (argv[1]);
    const juce::String model = juce::String::fromUTF8 (argv[2]);

    juce::AudioPluginFormatManager formats;
   #if JUCE_MAJOR_VERSION >= 9
    juce::addDefaultFormatsToManager (formats);   // JUCE 9 : addDefaultFormats() a été supprimée
   #else
    formats.addDefaultFormats();
   #endif

    juce::OwnedArray<juce::PluginDescription> found;
    juce::AudioUnitPluginFormat au;
    for (const auto& id : au.searchPathsForPlugins ({}, true, false))
        if (id.contains (code) && id.contains ("Anty"))
            au.findAllTypesForFile (found, id);

    if (found.isEmpty())
    {
        std::printf ("ÉCHEC : Audio Unit %s introuvable\n", code.toRawUTF8());
        return 1;
    }

    juce::String error;
    auto make = [&] { return formats.createPluginInstance (*found[0], 48000.0, 512, error); };

    auto clean = make();
    if (clean == nullptr)
    {
        std::printf ("ÉCHEC : %s\n", error.toRawUTF8());
        return 1;
    }

    // Référence : sans modèle (son clair), avec les mêmes réglages
    setModel (*clean, code, {});
    const auto cleanOut = processSine (*clean);

    auto withModel = make();
    setModel (*withModel, code, model);

    const auto modelOut = processSine (*withModel);

    // Écart de forme d'onde entre « avec modèle » et « son clair », en dB sous le son clair.
    // Un modèle actif, même clair, déforme et colore le son : l'écart est grand (> -20 dB).
    double diff = 0.0, ref = 0.0;
    for (size_t i = 0; i < cleanOut.size(); ++i)
    {
        diff += std::pow ((double) modelOut[i] - cleanOut[i], 2.0);
        ref  += (double) cleanOut[i] * cleanOut[i];
    }
    const double diffDb = 10.0 * std::log10 (diff / ref + 1e-20);
    const double cleanDb = levelDb (cleanOut), modelDb = levelDb (modelOut);

    const bool ok = diffDb > -20.0;
    std::printf ("%s  %-40s  clair %6.1f dB  modèle %6.1f dB  écart %6.1f dB  %s\n", found[0]->name.toRawUTF8(),
                 juce::File (model).getFileName().toRawUTF8(), cleanDb, modelDb, diffDb,
                 ok ? "OK (le modèle est actif)" : "ÉCHEC (le modèle n'agit pas)");
    return ok ? 0 : 1;
}
