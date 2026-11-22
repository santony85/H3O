#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TremoloProcessor.h"

class TremoloEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit TremoloEditor (TremoloProcessor&);
    ~TremoloEditor() override = default;

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

    struct Menu
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, const juce::String& tooltip);
    void setupMenu (Menu&, const juce::String& paramId, const juce::String& name, const juce::StringArray& items,
                    const juce::String& tooltip);

    // Presets, onde animée, réglages grisés selon le mode
    void timerCallback() override;
    void drawWave (juce::Graphics&, juce::Rectangle<float>);

    TremoloProcessor& tremolo;

    juce::ComboBox presetBox;
    Menu mode, shape, division;

    juce::ToggleButton sync;
    std::unique_ptr<ButtonAttachment> syncAttachment;

    Knob rate, depth, smooth, stereoPhase, crossover;

    std::unique_ptr<juce::TooltipWindow> tooltips;   // seul : dans H3O Chain, la chaîne a déjà les siennes

    juce::Rectangle<int> waveArea;

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TremoloEditor)
};
