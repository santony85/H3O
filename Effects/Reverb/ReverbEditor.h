#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ReverbProcessor.h"

class ReverbEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit ReverbEditor (ReverbProcessor&);
    ~ReverbEditor() override = default;

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

    // Garde le menu de presets aligné si Logic change de preset, et redessine la courbe
    void timerCallback() override;

    ReverbProcessor& reverb;

    juce::ComboBox presetBox;

    // En haut : la pièce ; en bas : son caractère et sa place dans le mix
    juce::ComboBox typeBox;
    juce::Label typeLabel;
    std::unique_ptr<ComboBoxAttachment> typeAttachment;
    Knob preDelay, size, decay, damping, diffusion;
    Knob modulation, lowCut, highCut, width, mix;

    juce::Rectangle<int> curveDisplay;          // courbe d'extinction
    ReverbEngine::Settings drawnSettings;       // réglages actuellement dessinés

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbEditor)
};
