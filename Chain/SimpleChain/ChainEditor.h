#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChainProcessor.h"

// Un pédalier : une rangée de pédales dans l'ordre du signal (Amp > Harmonizer > Chorus > Delay),
// chacune avec son interrupteur. Un clic sur le nom d'une pédale affiche ses réglages en dessous.
class ChainEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit ChainEditor (ChainProcessor&);
    ~ChainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Pedal
    {
        juce::String name;
        juce::Colour colour;
        std::unique_ptr<juce::AudioProcessorEditor> editor;   // l'interface de l'effet, réutilisée telle quelle
        std::unique_ptr<juce::TextButton> selectButton;       // affiche ses réglages
        std::unique_ptr<juce::ToggleButton> onButton;         // allume / coupe l'effet
        std::unique_ptr<ButtonAttachment> onAttachment;
        std::function<bool()> isOn;
    };

    void addPedal (const juce::String& name, juce::Colour, juce::AudioProcessor& effect,
                   const juce::String& onParamId, std::function<bool()> isOn);
    void select (int index);

    // Assombrit les réglages des effets coupés
    void timerCallback() override;

    ChainProcessor& chain;
    InputMode::Box inputBox { chain.parameters, juce::Colours::white };

    std::vector<Pedal> pedals;
    int selected = 0;

    static constexpr int headerHeight = 40;   // titre + Input
    static constexpr int stripHeight  = 64;   // la rangée de pédales
    static constexpr int arrowWidth   = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainEditor)
};
