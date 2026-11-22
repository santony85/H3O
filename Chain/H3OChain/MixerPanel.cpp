#include "MixerPanel.h"

MixerPanel::MixerPanel (ChainProcessor& c) : chain (c)
{
    static const char* ids[]   = { "levela", "pana", "levelb", "panb" };
    static const char* names[] = { "Level A", "Pan A", "Level B", "Pan B" };
    static const char* tips[]  = {
        "Level of input A: 100 % = unchanged, 200 % = +6 dB",
        "Pan of input A: -100 % = only left, +100 % = only right",
        "Level of input B: 100 % = unchanged, 200 % = +6 dB",
        "Pan of input B: -100 % = only left, +100 % = only right"
    };

    for (int m = 0; m < ChainPatch::numMixers; ++m)
        for (int k = 0; k < 4; ++k)
        {
            auto& knob = knobs[(size_t) m][(size_t) k];
            knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
            knob.slider.setColour (juce::Slider::rotarySliderFillColourId, k % 2 == 0 ? accent() : juce::Colour (0xff70c0ff));
            knob.slider.setTooltip (tips[k]);
            addAndMakeVisible (knob.slider);
            knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                chain.parameters, ChainProcessor::mixerParamId (m, ids[k]), knob.slider);
            knob.slider.setNumDecimalPlacesToDisplay (0);

            knob.label.setText (names[k], juce::dontSendNotification);
            knob.label.setJustificationType (juce::Justification::centred);
            knob.label.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (knob.label);
        }

    setSize (760, 330);
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1f24));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MIXERS", getLocalBounds().removeFromTop (40).withTrimmedLeft (15), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("Each mixer adds its inputs A and B. Pan sends a path to one side: a left path and a right path, like the H3000.",
                getLocalBounds().removeFromTop (40).withTrimmedLeft (110).withTrimmedRight (15), juce::Justification::centredRight);

    for (int m = 0; m < ChainPatch::numMixers; ++m)
    {
        const auto col = columns[(size_t) m].toFloat();
        g.setColour (juce::Colour (0xff15161a));
        g.fillRoundedRectangle (col, 6.0f);
        g.setColour (accent());
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("Mixer " + juce::String (m + 1), col.withHeight (26.0f), juce::Justification::centred);
    }
}

void MixerPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (44).reduced (10, 6);
    const int w = area.getWidth() / ChainPatch::numMixers;

    for (int m = 0; m < ChainPatch::numMixers; ++m)
    {
        auto col = area.removeFromLeft (w).reduced (5, 0);
        columns[(size_t) m] = col;
        col.removeFromTop (26);
        const int rowHeight = col.getHeight() / 2;
        for (int row = 0; row < 2; ++row)
        {
            auto r = col.removeFromTop (rowHeight);
            for (int k = 0; k < 2; ++k)
            {
                auto& knob = knobs[(size_t) m][(size_t) (row * 2 + k)];
                auto cell = r.removeFromLeft (r.getWidth() / (2 - k));
                knob.label.setBounds (cell.removeFromTop (16));
                knob.slider.setBounds (cell.reduced (4, 0));
            }
        }
    }
}
