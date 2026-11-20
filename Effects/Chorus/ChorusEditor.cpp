#include "ChorusEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xffb070e0);
}

ChorusEditor::ChorusEditor (ChorusProcessor& p)
    : AudioProcessorEditor (&p), chorus (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < chorus.getNumPrograms(); ++i)
        presetBox.addItem (chorus.getProgramName (i), i + 1);

    presetBox.setSelectedId (chorus.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { chorus.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupKnob (rate,     "rate",     "Rate");
    setupKnob (depth,    "depth",    "Depth");
    setupKnob (delay,    "delay",    "Delay");
    setupKnob (voices,   "voices",   "Voices");
    setupKnob (feedback, "feedback", "Feedback");
    setupKnob (width,    "width",    "Width");
    setupKnob (tone,     "tone",     "Tone");
    setupKnob (mix,      "mix",      "Mix");

    if (! chorus.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (chorus.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    setSize (600, 300);
    startTimerHz (5);
}

void ChorusEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (chorus.parameters, paramId, knob.slider);
}

void ChorusEditor::timerCallback()
{
    const int id = chorus.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void ChorusEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIMPLE CHORUS", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);
}

void ChorusEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (5);

    const int rowHeight = area.getHeight() / 2;

    for (auto row : { std::array<Knob*, 4> { &rate, &depth, &delay, &voices },
                      std::array<Knob*, 4> { &feedback, &width, &tone, &mix } })
    {
        auto rowArea = area.removeFromTop (rowHeight);
        const int w = rowArea.getWidth() / 4;

        for (auto* knob : row)
        {
            auto col = rowArea.removeFromLeft (w);
            knob->label.setBounds (col.removeFromTop (18));
            knob->slider.setBounds (col.reduced (5, 0));
        }
    }
}
