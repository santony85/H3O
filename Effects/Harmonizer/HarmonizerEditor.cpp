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

    setupKnob (aInterval, "adegrees", "Interval");
    setupKnob (bInterval, "bdegrees", "Interval");

    setupChoice (mode,  "mode",  "Mode");
    setupChoice (key,   "key",   "Key");
    setupChoice (scale, "scale", "Scale");

    noteDisplay.setJustificationType (juce::Justification::centred);
    noteDisplay.setColour (juce::Label::backgroundColourId, juce::Colour (0xff141518));
    noteDisplay.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    noteDisplay.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (noteDisplay);

    setupKnob (splice,   "splice",   "Splice");
    setupKnob (feedback, "feedback", "Feedback");
    setupKnob (spread,   "spread",   "Spread");
    setupKnob (mix,      "mix",      "Mix");

    const char* rowNames[] = { "DIATONIC SHIFT", "VOICE A (L)", "VOICE B (R)", "GLOBAL" };
    for (int r = 0; r < 4; ++r)
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

    setSize (600, 580);
    timerCallback();
    startTimerHz (10);
}

void HarmonizerEditor::setupChoice (Choice& choice, const juce::String& paramId, const juce::String& name)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (harmonizer.parameters.getParameter (paramId)))
        choice.box.addItemList (param->choices, 1);

    addAndMakeVisible (choice.box);

    choice.label.setText (name, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (harmonizer.parameters, paramId, choice.box);
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

    // Shift (demi-tons fixes) ou Interval (crans de gamme) selon le mode
    const bool diatonic = harmonizer.isDiatonic();
    if (diatonic != showingDiatonic || aShift.slider.isVisible() == aInterval.slider.isVisible())
    {
        showingDiatonic = diatonic;
        for (auto* k : { &aShift, &bShift })
        {
            k->slider.setVisible (! diatonic);
            k->label.setVisible (! diatonic);
        }
        for (auto* k : { &aInterval, &bInterval })
        {
            k->slider.setVisible (diatonic);
            k->label.setVisible (diatonic);
        }
        key.box.setEnabled (diatonic);
        scale.box.setEnabled (diatonic);
    }

    // Note reconnue et notes produites par les voix : « E3  →  A: G3   B: B3 »
    auto noteText = [] (int n) { return juce::String (DiatonicScale::noteName (n)) + juce::String (n / 12 - 1); };
    juce::String text;
    auto colour = juce::Colours::white;

    if (! diatonic)
    {
        text = "Chromatic: fixed intervals";
        colour = juce::Colours::grey;
    }
    else if (const int note = harmonizer.getDetectedNote(); note < 0)
    {
        text = "Play a note...";
        colour = juce::Colours::grey;
    }
    else
    {
        text << noteText (note) << "   ->   A: " << noteText (note + harmonizer.getVoiceShift (0))
             << "   B: " << noteText (note + harmonizer.getVoiceShift (1));
        colour = accent.brighter (0.4f);
    }

    noteDisplay.setText (text, juce::dontSendNotification);
    noteDisplay.setColour (juce::Label::textColourId, colour);
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

    // Rangée Diatonic Shift : Mode | Key | Scale | note reconnue
    {
        auto row = area.removeFromTop (70);
        rowLabels[0].setBounds (row.removeFromTop (18));

        const int w = row.getWidth() / 4;
        for (auto* choice : { &mode, &key, &scale })
        {
            auto col = row.removeFromLeft (w).reduced (8, 0);
            choice->label.setBounds (col.removeFromTop (18));
            choice->box.setBounds (col.removeFromTop (26));
        }
        noteDisplay.setBounds (row.reduced (4, 0).withTrimmedTop (14).removeFromTop (34));
        area.removeFromTop (6);
    }

    // Interval occupe la place de Shift (un seul des deux est visible)
    Knob* rows[3][4] = {
        { &aShift, &aFine, &aDelay, &aLevel },
        { &bShift, &bFine, &bDelay, &bLevel },
        { &splice, &feedback, &spread, &mix },
    };

    const int rowHeight = area.getHeight() / 3;
    for (int r = 0; r < 3; ++r)
    {
        auto row = area.removeFromTop (rowHeight);
        rowLabels[r + 1].setBounds (row.removeFromTop (18));

        const int w = row.getWidth() / 4;
        for (auto* knob : rows[r])
        {
            auto col = row.removeFromLeft (w);
            knob->label.setBounds (col.removeFromTop (18));
            knob->slider.setBounds (col.reduced (4, 0));
        }
    }

    aInterval.label.setBounds (aShift.label.getBounds());
    aInterval.slider.setBounds (aShift.slider.getBounds());
    bInterval.label.setBounds (bShift.label.getBounds());
    bInterval.slider.setBounds (bShift.slider.getBounds());
}
