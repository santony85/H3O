#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "WhammyProcessor.h"

class WhammyEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit WhammyEditor (WhammyProcessor&);
    ~WhammyEditor() override = default;

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

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tooltip);

    // Presets, affichage de la transposition, réglages grisés selon le mode
    void timerCallback() override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>);

    WhammyProcessor& whammy;

    juce::ComboBox presetBox, modeBox;
    juce::Label modeLabel;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;

    Knob pedal;   // la pédale d'expression : une grande glissière verticale
    Knob heel, toe, glide, detune, mix;

    std::unique_ptr<juce::TooltipWindow> tooltips;   // seul : dans H3O Chain, la chaîne a déjà les siennes

    juce::Rectangle<int> displayArea;

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhammyEditor)
};
