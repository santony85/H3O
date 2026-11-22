#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ReverseProcessor.h"

class ReverseEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit ReverseEditor (ReverseProcessor&);
    ~ReverseEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    // Garde le menu de presets aligné si Logic change de preset
    void timerCallback() override;

    ReverseProcessor& reverse;

    juce::ComboBox presetBox;

    // 2 rangées : la tranche inversée (longueur, transposition), puis le son
    Knob length, shift, fine, feedback;
    Knob spread, tone, mix;

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseEditor)
};
