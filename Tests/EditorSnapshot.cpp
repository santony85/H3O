// Enregistre une image PNG de chaque interface, pour la vérifier sans ouvrir Logic.
//   ./EditorSnapshot <dossier>        Cible Xcode : EditorSnapshot
#include "ChainProcessor.h"
#include "BandDelay/BandDelayEditor.h"
#include "UltraTap/UltraTapEditor.h"
#include "Reverb/ReverbEditor.h"
#include "LayeredShift/LayeredShiftEditor.h"

#include <cstdio>

namespace
{
    void save (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("%s  (%d x %d)\n", file.getFullPathName().toRawUTF8(), c.getWidth(), c.getHeight());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File folder (argc > 1 ? juce::String::fromUTF8 (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    folder.createDirectory();

    {
        BandDelayProcessor fx;
        std::unique_ptr<juce::AudioProcessorEditor> editor (fx.createEditor());
        save (*editor, folder.getChildFile ("BandDelay.png"));
    }

    {
        ReverbProcessor fx;
        std::unique_ptr<juce::AudioProcessorEditor> editor (fx.createEditor());
        save (*editor, folder.getChildFile ("Reverb.png"));
    }

    {
        LayeredShiftProcessor fx;
        std::unique_ptr<juce::AudioProcessorEditor> editor (fx.createEditor());
        save (*editor, folder.getChildFile ("LayeredShift.png"));
    }

    {
        ChainProcessor chain;
        chain.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (chain.createEditor());

        // Une image de la chaîne par pédale affichée : on « clique » sur son nom
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
            {
                button->onClick();
                save (*editor, folder.getChildFile ("Chain-" + button->getButtonText().removeCharacters (" -") + ".png"));
            }
    }

    return 0;
}
