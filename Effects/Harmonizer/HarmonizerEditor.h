#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "HarmonizerProcessor.h"

class HarmonizerEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit HarmonizerEditor (HarmonizerProcessor&);
    ~HarmonizerEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void setupChoice (Choice&, const juce::String& paramId, const juce::String& name);

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    // Garde le menu de presets aligné si Logic change de preset,
    // échange Shift ↔ Interval selon le mode et affiche la note reconnue
    void timerCallback() override;

    HarmonizerProcessor& harmonizer;

    juce::ComboBox presetBox;

    // 3 rangées de 4 boutons : voix A, voix B, réglages globaux
    Knob aShift, aFine, aDelay, aLevel;
    Knob bShift, bFine, bDelay, bLevel;
    Knob splice, feedback, spread, mix;

    // Diatonic Shift : à la place de Shift, l'intervalle en crans de gamme
    Knob aInterval, bInterval;
    Choice mode, key, scale;
    juce::Label noteDisplay;
    bool showingDiatonic = false;

    juce::Label rowLabels[4];

    // Menu Input : masqué quand l'effet est dans H3O Chain (c'est la chaîne qui choisit)
    std::unique_ptr<InputMode::Box> inputBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerEditor)
};
