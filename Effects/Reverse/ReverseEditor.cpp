#include "ReverseEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xff50c878);
}

ReverseEditor::ReverseEditor (ReverseProcessor& p)
    : AudioProcessorEditor (&p), reverse (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < reverse.getNumPrograms(); ++i)
        presetBox.addItem (reverse.getProgramName (i), i + 1);

    presetBox.setSelectedId (reverse.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { reverse.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupKnob (length,   "length",   "Length");
    setupKnob (shift,    "shift",    "Shift");
    setupKnob (fine,     "fine",     "Fine");
    setupKnob (feedback, "feedback", "Feedback");
    setupKnob (spread,   "spread",   "Spread");
    setupKnob (tone,     "tone",     "Tone");
    setupKnob (mix,      "mix",      "Mix");

    if (! reverse.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (reverse.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    setSize (600, 300);
    startTimerHz (5);
}

void ReverseEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (reverse.parameters, paramId, knob.slider);
}

void ReverseEditor::timerCallback()
{
    const int id = reverse.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void ReverseEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIMPLE REVERSE", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);
}

void ReverseEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (5);

    const int rowHeight = area.getHeight() / 2;

    // 4 boutons en haut, 3 centrés en bas
    for (auto row : { std::vector<Knob*> { &length, &shift, &fine, &feedback },
                      std::vector<Knob*> { &spread, &tone, &mix } })
    {
        auto rowArea = area.removeFromTop (rowHeight);
        const int w = rowArea.getWidth() / 4;
        rowArea = rowArea.withSizeKeepingCentre (w * (int) row.size(), rowArea.getHeight());

        for (auto* knob : row)
        {
            auto col = rowArea.removeFromLeft (w);
            knob->label.setBounds (col.removeFromTop (18));
            knob->slider.setBounds (col.reduced (5, 0));
        }
    }
}
