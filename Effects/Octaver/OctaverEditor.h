#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "OctaverProcessor.h"

class OctaverEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit OctaverEditor (OctaverProcessor&);
    ~OctaverEditor() override = default;

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

    // Presets, affichage des quatre voix (−2, −1, direct, +1)
    void timerCallback() override;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>);

    OctaverProcessor& octaver;

    juce::ComboBox presetBox, modeBox;
    juce::Label modeLabel;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;

    Knob sub2, sub1, dry, up, tone;

    std::unique_ptr<juce::TooltipWindow> tooltips;   // seul : dans H3O Chain, la chaîne a déjà les siennes

    juce::Rectangle<int> displayArea;
    OctaverEngine::Settings shown;

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaverEditor)
};
