#include "LayeredShiftEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xffe0e050);
}

LayeredShiftEditor::LayeredShiftEditor (LayeredShiftProcessor& p)
    : AudioProcessorEditor (&p), layered (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < layered.getNumPrograms(); ++i)
        presetBox.addItem (layered.getProgramName (i), i + 1);

    presetBox.setSelectedId (layered.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { layered.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupKnob (aShift, "asemi",  "Shift");
    setupKnob (aFine,  "acents", "Fine");
    setupKnob (aDelay, "adelay", "Delay");
    setupKnob (aLevel, "alevel", "Level");

    setupKnob (bShift, "bsemi",  "Shift");
    setupKnob (bFine,  "bcents", "Fine");
    setupKnob (bDelay, "bdelay", "Delay");
    setupKnob (bLevel, "blevel", "Level");

    setupKnob (detune,     "detune",     "Detune");
    setupKnob (sweepRate,  "sweeprate",  "Sweep Rate");
    setupKnob (sweepDepth, "sweepdepth", "Sweep Depth");
    setupKnob (feedback,   "feedback",   "Feedback");
    setupKnob (tone,       "tone",       "Tone");
    setupKnob (mix,        "mix",        "Mix");

    detune.slider.setTooltip ("Left copy goes up, right copy goes down: wider and thicker");
    sweepDepth.slider.setTooltip ("Slow pitch drift of the four voices, each at a different moment");

    const char* rowNames[] = { "LAYER A", "LAYER B", "MOTION & TONE" };
    for (int r = 0; r < 3; ++r)
    {
        rowLabels[r].setText (rowNames[r], juce::dontSendNotification);
        rowLabels[r].setColour (juce::Label::textColourId, accent);
        rowLabels[r].setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (rowLabels[r]);
    }

    if (! layered.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (layered.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    setSize (600, 500);
    startTimerHz (5);
}

void LayeredShiftEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (layered.parameters, paramId, knob.slider);
}

void LayeredShiftEditor::timerCallback()
{
    const int id = layered.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void LayeredShiftEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIMPLE LAYERED SHIFT", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);
}

void LayeredShiftEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (200).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (5);

    const std::vector<Knob*> rows[3] = {
        { &aShift, &aFine, &aDelay, &aLevel },
        { &bShift, &bFine, &bDelay, &bLevel },
        { &detune, &sweepRate, &sweepDepth, &feedback, &tone, &mix },
    };

    const int rowHeight = area.getHeight() / 3;
    for (int r = 0; r < 3; ++r)
    {
        auto row = area.removeFromTop (rowHeight);
        rowLabels[r].setBounds (row.removeFromTop (18));

        // Couches : 4 colonnes ; mouvement et son : 6 colonnes
        const int w = row.getWidth() / (r < 2 ? 4 : 6);
        for (auto* knob : rows[r])
        {
            auto col = row.removeFromLeft (w);
            knob->label.setBounds (col.removeFromTop (18));
            knob->slider.setBounds (col.reduced (4, 0));
        }
    }
}
