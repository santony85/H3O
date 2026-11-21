#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChainProcessor.h"

// Le panneau de la matrice de modulation de Simple Chain :
//   - en haut, les sources (LFO 1, LFO 2, Random, Envelope, Pitch) avec leurs réglages et leur valeur en direct ;
//   - en dessous, 8 lignes « source → destination × quantité ».
// La destination se choisit dans un menu, effet par effet.
class ModulationPanel : public juce::Component,
                        private juce::Timer
{
public:
    explicit ModulationPanel (ChainProcessor&);
    ~ModulationPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth  = 720;
    static constexpr int preferredHeight = 470;

    static juce::Colour accent() { return juce::Colour (0xffc080ff); }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct SourceBox
    {
        juce::String title;
        int source = 0;
        juce::ComboBox shape;
        std::unique_ptr<ComboAttachment> shapeAttachment;

        // LFO seulement : synchronisation au tempo
        juce::ToggleButton sync { "Sync" };
        std::unique_ptr<ButtonAttachment> syncAttachment;
        juce::ComboBox division;
        std::unique_ptr<ComboAttachment> divisionAttachment;
        juce::Label tempo;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds, meter;
    };

    struct Row
    {
        juce::Label number;
        juce::ComboBox source;
        std::unique_ptr<ComboAttachment> sourceAttachment;
        juce::TextButton destination;
        juce::Slider amount;
        std::unique_ptr<SliderAttachment> amountAttachment;
        juce::Rectangle<int> meter;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void showDestinationMenu (int slot);
    void timerCallback() override;

    static void drawMeter (juce::Graphics&, juce::Rectangle<int>, float value, bool bipolar, juce::Colour);

    // Barre Amount remplie depuis le centre : à gauche en négatif, à droite en positif
    struct BipolarLook : juce::LookAndFeel_V4
    {
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                               float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    };

    ChainProcessor& chain;
    BipolarLook bipolarLook;   // déclaré avant les lignes : détruit après leurs curseurs

    Knob lfo1Rate, lfo2Rate, randomRate, envAttack, envRelease;
    std::array<SourceBox, 5> boxes;
    std::array<Row, ChainProcessor::numSlots> rows;
    juce::Rectangle<int> matrixHeader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulationPanel)
};
