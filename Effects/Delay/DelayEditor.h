#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DelayProcessor.h"

class DelayEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit DelayEditor (DelayProcessor&);
    ~DelayEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    // Rafraîchit l'affichage (temps effectif, tempo, contrôles actifs)
    void timerCallback() override;

    DelayProcessor& delayProcessor;
    Knob timeKnob, feedbackKnob, lowCutKnob, highCutKnob, mixKnob;

    juce::ToggleButton syncButton { "Sync" };
    juce::ToggleButton pingPongButton { "Ping-Pong" };
    juce::ComboBox divisionBox;
    juce::ComboBox presetBox;
    juce::Label infoLabel;

    // Menu Input : masqué quand l'effet est dans Simple Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<ButtonAttachment> pingPongAttachment;
    std::unique_ptr<ComboBoxAttachment> divisionAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayEditor)
};
