#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChorusProcessor.h"

class ChorusEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit ChorusEditor (ChorusProcessor&);
    ~ChorusEditor() override = default;

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

    ChorusProcessor& chorus;

    juce::ComboBox presetBox;

    // 2 rangées de 4 boutons : modulation, puis son
    Knob rate, depth, delay, voices;
    Knob feedback, width, tone, mix;

    // Menu Input : masqué quand l'effet est dans Simple Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusEditor)
};
