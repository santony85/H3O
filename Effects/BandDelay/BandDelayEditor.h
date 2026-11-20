#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BandDelayProcessor.h"

class BandDelayEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit BandDelayEditor (BandDelayProcessor&);
    ~BandDelayEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    // Garde le menu de presets aligné si Logic change de preset, et redessine les bandes
    void timerCallback() override;

    BandDelayProcessor& bandDelay;

    juce::ComboBox presetBox;

    // En haut : les bandes (nombre, temps, couleurs, résonance) ; en bas : ordre, retour, espace, mix
    Knob bands, time, low, high, resonance;
    Knob feedback, spread, mix;

    juce::ComboBox patternBox;
    juce::Label patternLabel;
    std::unique_ptr<ComboBoxAttachment> patternAttachment;

    juce::Rectangle<int> bandDisplay;            // dessin des échos : temps × fréquence
    BandDelayEngine::Settings drawnSettings;     // réglages actuellement dessinés

    // Menu Input : masqué quand l'effet est dans Simple Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandDelayEditor)
};
