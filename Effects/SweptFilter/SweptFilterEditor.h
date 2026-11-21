#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SweptFilterProcessor.h"

class SweptFilterEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SweptFilterEditor (SweptFilterProcessor&);
    ~SweptFilterEditor() override = default;

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

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void setupChoice (Choice&, const juce::String& paramId, const juce::String& name);

    // Presets, réglages LFO / enveloppe actifs, et la courbe qui suit le filtre en direct
    void timerCallback() override;

    SweptFilterProcessor& swept;

    juce::ComboBox presetBox;

    Choice source, type, shape, direction;
    Knob low, high, resonance, mix;
    Knob rate, phase;
    Knob sensitivity, attack, release;

    juce::Label rowLabels[3];

    juce::Rectangle<int> curveDisplay;
    float drawnPosition[2] = { -1.0f, -1.0f };

    // Menu Input : masqué quand l'effet est dans Simple Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweptFilterEditor)
};
