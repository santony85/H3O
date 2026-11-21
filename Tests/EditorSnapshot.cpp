// Enregistre une image PNG de chaque interface, pour la vérifier sans ouvrir Logic.
//   ./EditorSnapshot <dossier>        Cible Xcode : EditorSnapshot
#include "ChainProcessor.h"
#include "BandDelay/BandDelayEditor.h"
#include "UltraTap/UltraTapEditor.h"
#include "Reverb/ReverbEditor.h"
#include "LayeredShift/LayeredShiftEditor.h"
#include "SweptFilter/SweptFilterEditor.h"
#include "Compressor/CompressorEditor.h"

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
        SweptFilterProcessor fx;
        std::unique_ptr<juce::AudioProcessorEditor> editor (fx.createEditor());
        save (*editor, folder.getChildFile ("SweptFilter.png"));
    }

    {
        CompressorProcessor fx;
        std::unique_ptr<juce::AudioProcessorEditor> editor (fx.createEditor());
        save (*editor, folder.getChildFile ("Compressor.png"));
    }

    {
        ChainProcessor chain;
        chain.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (chain.createEditor());

        // Une image de la chaîne par pédale affichée : on « clique » sur son nom
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getName() != "routing" && button->getName() != "preset")
            {
                button->onClick();
                save (*editor, folder.getChildFile ("Chain-" + button->getButtonText().removeCharacters (" -") + ".png"));
            }
    }

    // La matrice de modulation, avec deux lignes actives et un peu de son passé dedans
    {
        ChainProcessor chain;
        chain.prepareToPlay (48000.0, 512);
        chain.setRouting (ChainRouting::templates()[1].routing);

        auto set = [&chain] (const juce::String& id, float value)
        {
            auto* param = chain.parameters.getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        };
        set ("mod1source", (float) ModulationSources::lfo1);
        set ("mod1amount", 40.0f);
        set ("lfo1sync", 1.0f);
        set ("lfo1division", 8.0f);   // 1/4
        chain.setSlotDestination (0, chain.findDestination ("delay/time"));
        set ("mod2source", (float) ModulationSources::envelope);
        set ("mod2amount", -60.0f);
        chain.setSlotDestination (1, chain.findDestination ("branches/branchspread"));
        set ("mod4source", (float) ModulationSources::lfo2);
        chain.setSlotDestination (3, chain.findDestination ("harmonizer/asemi"));
        set ("mod4amount", 100.0f * 7.0f / 48.0f);   // +7 demi-tons
        set ("mod3source", (float) ModulationSources::random);
        chain.setSlotDestination (2, chain.findDestination ("layered/sweepdepth"));

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 30; ++b)
        {
            for (int i = 0; i < 512; ++i)
                buffer.setSample (0, i, 0.3f * std::sin (0.03f * (float) (b * 512 + i))), buffer.setSample (1, i, buffer.getSample (0, i));
            chain.processBlock (buffer, midi);
        }

        chain.presetName = "Lead Solo LA";   // nom de preset affiché dans l'en-tête
        std::unique_ptr<juce::AudioProcessorEditor> editor (chain.createEditor());
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == "Modulation")
                button->onClick();
        editor->repaint();
        save (*editor, folder.getChildFile ("Modulation.png"));
    }

    // Les routages tout prêts : le schéma avec ses branches
    const auto& templates = ChainRouting::templates();
    for (size_t i = 1; i < templates.size(); ++i)
    {
        ChainProcessor chain;
        chain.prepareToPlay (48000.0, 512);
        chain.setRouting (templates[i].routing);
        if (juce::String (templates[i].name) == "Wet/Dry/Wet")
            chain.parameters.getParameter ("wetonly")->setValueNotifyingHost (1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> editor (chain.createEditor());
        save (*editor, folder.getChildFile ("Routing-" + juce::String (templates[i].name).removeCharacters (" |/") + ".png"));
    }

    return 0;
}
