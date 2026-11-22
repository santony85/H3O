#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ChainProcessor;

// Les presets d'usine « H3000 Inspired » : 100 sons de H3O Chain dans l'esprit de l'Eventide H3000
// (harmonies diatoniques, Micro-Pitch, reverse, Ultra-Tap, band delays, filtres balayés, rigs Wet/Dry/Wet).
// Ce sont des sons originaux, construits avec les effets de H3O : pas des copies de la banque d'Eventide.
// Ils ne contiennent pas d'Amp : charger un de ces presets garde ton Amp (modèle NAM, IR, réglages).
namespace FactoryPresets
{
    static constexpr const char* folderName = "H3000 Inspired";
    static constexpr const char* multiFolderName = "Multi-FX";   // 100 rigs multi-effets, classiques et originaux, par style
    static constexpr const char* patchFolderName = "Patch Bay";  // 50 rigs câblés : splits à plusieurs niveaux, mixeurs, boucles

    int count();
    juce::String bank (int index);
    juce::String category (int index);
    juce::String name (int index);

    // Applique la recette à une chaîne neuve (tous les réglages d'usine). Renvoie une erreur, ou rien.
    juce::String apply (int index, ChainProcessor&);

    // Écrit les presets dans <dossier>/<Banque>/<Catégorie>/<Nom>.scpreset. Renvoie une erreur, ou rien.
    juce::String writeAll (const juce::File& presetsFolder);
}
