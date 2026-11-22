#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorProcessor.h"

class CompressorEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit CompressorEditor (CompressorProcessor&);
    ~CompressorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tooltip);
    void setupToggle (juce::ToggleButton&, std::unique_ptr<ButtonAttachment>&, const juce::String& paramId,
                      const juce::String& name, const juce::String& tooltip);

    // Presets, courbe et vu-mètre de réduction de gain en direct
    void timerCallback() override;

    void drawCurve (juce::Graphics&, juce::Rectangle<float>);
    void drawMeter (juce::Graphics&, juce::Rectangle<float>);

    CompressorProcessor& compressor;

    juce::ComboBox presetBox;

    juce::ComboBox styleBox;
    juce::Label styleLabel;
    std::unique_ptr<ComboBoxAttachment> styleAttachment;

    juce::ToggleButton autoRelease, autoMakeup;
    std::unique_ptr<ButtonAttachment> autoReleaseAttachment, autoMakeupAttachment;

    Knob threshold, ratio, knee, attack;
    Knob release, sidechain, makeup, mix;

    std::unique_ptr<juce::TooltipWindow> tooltips;   // seul : dans H3O Chain, la chaîne a déjà les siennes

    juce::Rectangle<int> curveArea, meterArea;
    float shownReduction = 0.0f, peakReduction = 0.0f, shownLevel = -120.0f;
    int peakHold = 0;

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorEditor)
};
