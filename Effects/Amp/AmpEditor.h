#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmpProcessor.h"

class AmpEditor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit AmpEditor (AmpProcessor&);
    ~AmpEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void chooseModel();
    void chooseImpulseResponse();

    // Met à jour les textes (modèle chargé, IR, conversion de fréquence)
    void timerCallback() override;

    AmpProcessor& amp;

    juce::TextButton modelButton { "Load Model..." };
    juce::TextButton irButton { "Load IR..." };
    juce::TextButton clearModelButton { "x" }, clearIrButton { "x" };
    juce::Label modelLabel, irLabel;
    juce::String modelError, irError;

    Knob gate, inputGain, bass, mid, treble, outputGain;

    juce::ToggleButton normaliseButton { "Normalize" };
    juce::ToggleButton cabButton { "Cab" };
    std::unique_ptr<ButtonAttachment> normaliseAttachment, cabAttachment;

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};
