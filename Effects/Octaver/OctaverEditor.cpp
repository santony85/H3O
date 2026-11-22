#include "OctaverEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour panel      (0xff15161a);
    const juce::Colour accent     (0xff7a68f0);   // indigo : la couleur de l'octaver
}

OctaverEditor::OctaverEditor (OctaverProcessor& p)
    : AudioProcessorEditor (&p), octaver (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < octaver.getNumPrograms(); ++i)
        presetBox.addItem (octaver.getProgramName (i), i + 1);

    presetBox.setSelectedId (octaver.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { octaver.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    modeBox.addItemList (OctaverProcessor::modeNames, 1);
    modeBox.setTooltip ("Analog: one note at a time, with the growl of analog octave pedals.  Poly: chords stay in tune");
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<ComboBoxAttachment> (octaver.parameters, "mode", modeBox);
    modeLabel.setText ("Mode", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (modeLabel);

    setupKnob (sub2, "sub2", juce::String::fromUTF8 ("\xe2\x88\x92" "2 Oct"), "Two octaves below your note");
    setupKnob (sub1, "sub1", juce::String::fromUTF8 ("\xe2\x88\x92" "1 Oct"), "One octave below your note");
    setupKnob (dry,  "dry",  "Dry",   "Your note, untouched");
    setupKnob (up,   "up",   "+1 Oct", "One octave above your note");
    setupKnob (tone, "tone", "Tone",  "Low-pass on the octaves only (not on your note): lower = rounder, higher = more bite");

    if (! octaver.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (octaver.parameters, accent);
        addAndMakeVisible (*inputBox);
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    }

    setSize (600, 370);
    timerCallback();
    startTimerHz (30);
}

void OctaverEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (octaver.parameters, paramId, knob.slider);
}

void OctaverEditor::timerCallback()
{
    const int id = octaver.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    // Les réglages eux-mêmes (pas la valeur modulée, mise à jour seulement pendant le traitement)
    auto raw = [this] (const char* id) { return octaver.parameters.getRawParameterValue (id)->load(); };
    OctaverEngine::Settings s;
    s.mode = (int) raw ("mode");
    s.dry = raw ("dry") * 0.01f;
    s.sub1 = raw ("sub1") * 0.01f;
    s.sub2 = raw ("sub2") * 0.01f;
    s.up = raw ("up") * 0.01f;
    s.toneHz = raw ("tone");
    if (s.mode != shown.mode || s.dry != shown.dry || s.sub1 != shown.sub1 || s.sub2 != shown.sub2 || s.up != shown.up || s.toneHz != shown.toneHz)
    {
        shown = s;
        repaint (displayArea.expanded (2));
    }
}

void OctaverEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    const auto& s = shown;
    auto inner = area.reduced (14.0f, 10.0f);

    auto title = inner.removeFromTop (14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (s.mode == OctaverEngine::poly ? "POLY: CHORDS STAY IN TUNE" : "ANALOG: ONE NOTE AT A TIME", title, juce::Justification::centredLeft);
    g.drawText ("Tone " + (s.toneHz >= 1000.0f ? juce::String (s.toneHz / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (s.toneHz)) + " Hz"),
                title, juce::Justification::centredRight);
    inner.removeFromTop (8.0f);

    // Les quatre voix, de la plus grave à la plus aiguë : une colonne par octave, sa hauteur = son niveau
    struct Voice { const char* name; float level; bool octave; };
    const Voice voices[] = { { "-2 OCT", s.sub2, true }, { "-1 OCT", s.sub1, true }, { "DRY", s.dry, false }, { "+1 OCT", s.up, true } };

    auto labels = inner.removeFromBottom (16.0f);
    const float columnWidth = inner.getWidth() / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        auto column = inner.withX (inner.getX() + columnWidth * (float) i).withWidth (columnWidth).reduced (columnWidth * 0.22f, 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (column, 3.0f);
        const auto colour = voices[i].octave ? accent : juce::Colours::white.withAlpha (0.7f);
        g.setColour (colour);
        g.fillRoundedRectangle (column.withTrimmedTop (column.getHeight() * (1.0f - voices[i].level)), 3.0f);

        g.setColour (juce::Colours::white.withAlpha (voices[i].level > 0.0f ? 0.8f : 0.35f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (voices[i].name).replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92")),
                    labels.withX (inner.getX() + columnWidth * (float) i).withWidth (columnWidth), juce::Justification::centred);
    }
}

void OctaverEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O OCTAVER", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    drawDisplay (g, displayArea.toFloat());
}

void OctaverEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (8);

    displayArea = area.removeFromTop (150);
    area.removeFromTop (10);

    auto options = area.removeFromTop (28);
    modeLabel.setBounds (options.removeFromLeft (46));
    modeBox.setBounds (options.removeFromLeft (130).reduced (4, 2));
    area.removeFromTop (10);

    const int w = area.getWidth() / 5;
    for (auto* knob : { &sub2, &sub1, &dry, &up, &tone })
    {
        auto col = area.removeFromLeft (w);
        knob->label.setBounds (col.removeFromTop (18));
        knob->slider.setBounds (col.reduced (5, 0));
    }
}
