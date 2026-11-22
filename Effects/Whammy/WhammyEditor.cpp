#include "WhammyEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour panel      (0xff15161a);
    const juce::Colour accent     (0xffe03848);   // rouge : la couleur des pédales whammy

    // Nom de l'intervalle le plus proche (en demi-tons)
    juce::String intervalName (float semitones)
    {
        static const char* const names[] = { "Unison", "Minor 2nd", "Major 2nd", "Minor 3rd", "Major 3rd", "4th", "Tritone",
                                              "5th", "Minor 6th", "Major 6th", "Minor 7th", "Major 7th", "Octave" };
        const int n = juce::roundToInt (semitones);
        const int a = std::abs (n);
        juce::String name = a == 24 ? "2 Octaves" : (a > 12 ? juce::String ("Octave + ") + names[a - 12] : names[a]);
        if (n != 0)
            name << (n > 0 ? " up" : " down");
        return name;
    }
}

WhammyEditor::WhammyEditor (WhammyProcessor& p)
    : AudioProcessorEditor (&p), whammy (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < whammy.getNumPrograms(); ++i)
        presetBox.addItem (whammy.getProgramName (i), i + 1);

    presetBox.setSelectedId (whammy.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { whammy.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    modeBox.addItemList (WhammyProcessor::modeNames, 1);
    modeBox.setTooltip ("Whammy: the pedal sweeps the pitch from Heel to Toe.  Detune: two slightly detuned voices, left and right, for a thick doubling");
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<ComboBoxAttachment> (whammy.parameters, "mode", modeBox);
    modeLabel.setText ("Mode", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (modeLabel);

    setupKnob (pedal,  "pedal",  "Pedal",  "The expression pedal: heel (0 %) to toe (100 %).  Automate it in Logic, or modulate it in H3O Chain");
    pedal.slider.setSliderStyle (juce::Slider::LinearVertical);
    pedal.slider.setColour (juce::Slider::trackColourId, accent);
    pedal.slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);

    setupKnob (heel,   "heel",   "Heel",   "Pitch with the pedal up, in semitones");
    setupKnob (toe,    "toe",    "Toe",    "Pitch with the pedal down, in semitones (12 = one octave)");
    setupKnob (glide,  "glide",  "Glide",  "How long the pitch takes to follow the pedal: 0 = instant, more = a smooth bend");
    setupKnob (detune, "detune", "Detune", "Detune mode: how far each voice is detuned (the left one up, the right one down)");
    setupKnob (mix,    "mix",    "Mix",    "100 % = only the shifted note (whammy).  50 % = your note plus the harmony");

    if (! whammy.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (whammy.parameters, accent);
        addAndMakeVisible (*inputBox);
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    }

    setSize (640, 410);
    timerCallback();
    startTimerHz (30);
}

void WhammyEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (whammy.parameters, paramId, knob.slider);
}

void WhammyEditor::timerCallback()
{
    const int id = whammy.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    // Detune : la pédale, le talon, la pointe et le glide ne servent pas ; Whammy : le detune ne sert pas
    const bool detuning = whammy.currentSettings().mode == WhammyEngine::detune;
    for (auto* knob : { &pedal, &heel, &toe, &glide })
    {
        knob->slider.setEnabled (! detuning);
        knob->label.setAlpha (detuning ? 0.4f : 1.0f);
    }
    detune.slider.setEnabled (detuning);
    detune.label.setAlpha (detuning ? 1.0f : 0.4f);

    repaint (displayArea.expanded (2));
}

void WhammyEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    const auto s = whammy.currentSettings();
    auto inner = area.reduced (14.0f, 10.0f);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (s.mode == WhammyEngine::detune ? "DETUNE" : "WHAMMY", inner.removeFromTop (14.0f), juce::Justification::centredLeft);

    if (s.mode == WhammyEngine::detune)
    {
        g.setColour (accent);
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (s.detuneCents, 1) + " ct", inner.removeFromTop (60.0f), juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Left up, right down", inner.removeFromTop (20.0f), juce::Justification::centred);
        return;
    }

    // La transposition entendue en ce moment (avec le glissé), et l'intervalle le plus proche
    const float now = whammy.getCurrentSemitones();
    g.setColour (accent);
    g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    g.drawText ((now > 0.05f ? "+" : "") + juce::String (now, 1) + " st", inner.removeFromTop (54.0f), juce::Justification::centred);
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (intervalName (now), inner.removeFromTop (20.0f), juce::Justification::centred);

    // La course de la pédale : du talon à la pointe, et où elle en est
    inner.removeFromTop (12.0f);
    auto bar = inner.removeFromTop (14.0f).reduced (30.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * s.pedal), 4.0f);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (11.0f));
    auto labels = inner.removeFromTop (16.0f).reduced (30.0f, 0.0f);
    auto st = [] (float v) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)); };
    g.drawText ("Heel " + st (s.heel), labels, juce::Justification::centredLeft);
    g.drawText ("Toe " + st (s.toe), labels, juce::Justification::centredRight);
}

void WhammyEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O WHAMMY", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    drawDisplay (g, displayArea.toFloat());
}

void WhammyEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (8);

    // À gauche, la pédale ; à droite, l'affichage puis les réglages
    auto left = area.removeFromLeft (110);
    pedal.label.setBounds (left.removeFromTop (18));
    pedal.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    pedal.slider.setBounds (left.reduced (20, 0));
    area.removeFromLeft (10);

    displayArea = area.removeFromTop (150);
    area.removeFromTop (10);

    auto options = area.removeFromTop (28);
    modeLabel.setBounds (options.removeFromLeft (46));
    modeBox.setBounds (options.removeFromLeft (130).reduced (4, 2));
    area.removeFromTop (10);

    const int w = area.getWidth() / 5;
    for (auto* knob : { &heel, &toe, &glide, &detune, &mix })
    {
        auto col = area.removeFromLeft (w);
        knob->label.setBounds (col.removeFromTop (18));
        knob->slider.setBounds (col.reduced (5, 0));
    }
}
