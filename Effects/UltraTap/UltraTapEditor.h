#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "UltraTapProcessor.h"

class UltraTapEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit UltraTapEditor (UltraTapProcessor&);
    ~UltraTapEditor() override = default;

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

    // Garde le menu de presets aligné si Logic change de preset, et redessine les répétitions
    void timerCallback() override;

    UltraTapProcessor& ultraTap;

    juce::ComboBox presetBox;

    // En haut : où tombent les répétitions ; en bas : leur volume, leur place et le son
    Knob preDelay, length, taps, slope, taper;
    Knob spread, feedback, tone, mix;

    juce::Rectangle<int> tapDisplay;           // dessin des répétitions
    UltraTapEngine::Settings drawnSettings;    // réglages actuellement dessinés

    // Menu Input : masqué quand l'effet est dans Simple Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltraTapEditor)
};
