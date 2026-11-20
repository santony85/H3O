#include "HarmonizerEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xff40b0e0);
}

HarmonizerEditor::HarmonizerEditor (HarmonizerProcessor& p)
    : AudioProcessorEditor (&p), harmonizer (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < harmonizer.getNumPrograms(); ++i)
        presetBox.addItem (harmonizer.getProgramName (i), i + 1);

    presetBox.setSelectedId (harmonizer.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { harmonizer.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupKnob (aShift, "asemi",  "Shift");
    setupKnob (aFine,  "acents", "Fine");
    setupKnob (aDelay, "adelay", "Delay");
    setupKnob (aLevel, "alevel", "Level");

    setupKnob (bShift, "bsemi",  "Shift");
    setupKnob (bFine,  "bcents", "Fine");
    setupKnob (bDelay, "bdelay", "Delay");
    setupKnob (bLevel, "blevel", "Level");

    setupKnob (splice,   "splice",   "Splice");
    setupKnob (feedback, "feedback", "Feedback");
    setupKnob (spread,   "spread",   "Spread");
    setupKnob (mix,      "mix",      "Mix");

    const char* rowNames[] = { "VOICE A (L)", "VOICE B (R)", "GLOBAL" };
    for (int r = 0; r < 3; ++r)
    {
        rowLabels[r].setText (rowNames[r], juce::dontSendNotification);
        rowLabels[r].setColour (juce::Label::textColourId, accent);
        rowLabels[r].setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (rowLabels[r]);
    }

    if (! harmonizer.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (harmonizer.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    setSize (600, 500);
    startTimerHz (5);
}

void HarmonizerEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (harmonizer.parameters, paramId, knob.slider);
}

void HarmonizerEditor::timerCallback()
{
    const int id = harmonizer.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void HarmonizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIMPLE HARMONIZER", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);
}

void HarmonizerEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto header = area.removeFromTop (30);
    presetBox.setBounds (header.removeFromRight (200).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (header.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (5);

    Knob* rows[3][4] = {
        { &aShift, &aFine, &aDelay, &aLevel },
        { &bShift, &bFine, &bDelay, &bLevel },
        { &splice, &feedback, &spread, &mix },
    };

    const int rowHeight = area.getHeight() / 3;
    for (int r = 0; r < 3; ++r)
    {
        auto row = area.removeFromTop (rowHeight);
        rowLabels[r].setBounds (row.removeFromTop (18));

        const int w = row.getWidth() / 4;
        for (auto* knob : rows[r])
        {
            auto col = row.removeFromLeft (w);
            knob->label.setBounds (col.removeFromTop (18));
            knob->slider.setBounds (col.reduced (4, 0));
        }
    }
}
