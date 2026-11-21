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

    // Les presets, sous-dossiers compris (« H3000 Inspired/Reverse/… »), par ordre alphabétique du chemin
    juce::Array<juce::File> list() const;

    // Le nom d'un preset : son chemin dans le dossier, sans l'extension (« H3000 Inspired/Reverse/Backwards Lead »)
    juce::String nameOf (const juce::File&) const;

    // Le fichier d'un nouveau nom tapé par l'utilisateur (à la racine du dossier, caractères interdits → tirets)
    juce::File fileFor (const juce::String& name) const;

    // Le fichier du preset actuel (sous-dossier compris), ou rien
    juce::File current() const;
    bool exists (const juce::String& name) const { return fileFor (name).existsAsFile(); }

    // Enregistre l'état actuel sous ce nom (remplace un preset du même nom). Renvoie une erreur, ou rien.
    juce::String save (const juce::String& name);

    // Enregistre l'état actuel dans ce fichier précis (Save du preset actuel, presets d'usine)
    juce::String saveTo (const juce::File&);

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
