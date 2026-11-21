#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ChainProcessor;

// Les presets de Simple Chain : un fichier par preset, dans ~/Music/AudioFX/Simple Chain Presets.
// Chaque fichier est l'état complet de la chaîne en XML lisible (réglages des pédales, routage,
// modulation, branches) : on peut les copier, les sauvegarder, les envoyer à quelqu'un.
// Les mêmes fichiers servent dans Logic et dans l'application autonome.
class ChainPresets
{
public:
    explicit ChainPresets (ChainProcessor& p) : chain (p) {}

    static constexpr const char* extension = ".scpreset";
    static juce::File defaultFolder();

    void setFolder (const juce::File& f) { folder = f; }   // pour les tests
    const juce::File& getFolder() const  { return folder; }

    // Les presets, par ordre alphabétique
    juce::Array<juce::File> list() const;

    juce::File fileFor (const juce::String& name) const;
    bool exists (const juce::String& name) const { return fileFor (name).existsAsFile(); }

    // Enregistre l'état actuel sous ce nom (remplace un preset du même nom). Renvoie une erreur, ou rien.
    juce::String save (const juce::String& name);

    // Charge un preset (l'entrée Stereo / Mono ne change pas). Renvoie une erreur, ou rien.
    juce::String load (const juce::File&);

    // Le preset suivant (+1) ou précédent (-1) dans la liste, en boucle
    juce::File neighbour (int direction) const;

    // Met le preset à la corbeille (récupérable)
    bool remove (const juce::File&);

private:
    ChainProcessor& chain;
    juce::File folder = defaultFolder();
};
