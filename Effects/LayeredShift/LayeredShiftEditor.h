#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LayeredShiftProcessor.h"

class LayeredShiftEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit LayeredShiftEditor (LayeredShiftProcessor&);
    ~LayeredShiftEditor() override = default;

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

    LayeredShiftProcessor& layered;

    juce::ComboBox presetBox;

    // Couche A, couche B, puis le mouvement et le son
    Knob aShift, aFine, aDelay, aLevel;
    Knob bShift, bFine, bDelay, bLevel;
    Knob detune, sweepRate, sweepDepth, feedback, tone, mix;

    juce::Label rowLabels[3];

    // Menu Input : masqué quand l'effet est dans Simple Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayeredShiftEditor)
};
