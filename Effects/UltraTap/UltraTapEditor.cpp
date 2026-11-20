#include "UltraTapEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xffe05090);

    bool sameSettings (const UltraTapEngine::Settings& a, const UltraTapEngine::Settings& b)
    {
        using juce::exactlyEqual;
        return exactlyEqual (a.preDelayMs, b.preDelayMs) && exactlyEqual (a.lengthMs, b.lengthMs)
            && a.taps == b.taps && exactlyEqual (a.slope, b.slope)
            && exactlyEqual (a.taper, b.taper) && exactlyEqual (a.spread, b.spread);
    }
}

UltraTapEditor::UltraTapEditor (UltraTapProcessor& p)
    : AudioProcessorEditor (&p), ultraTap (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < ultraTap.getNumPrograms(); ++i)
        presetBox.addItem (ultraTap.getProgramName (i), i + 1);

    presetBox.setSelectedId (ultraTap.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { ultraTap.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupKnob (preDelay, "predelay", "Pre-Delay");
    setupKnob (length,   "length",   "Length");
    setupKnob (taps,     "taps",     "Taps");
    setupKnob (slope,    "slope",    "Slope");
    setupKnob (taper,    "taper",    "Taper");
    setupKnob (spread,   "spread",   "Spread");
    setupKnob (feedback, "feedback", "Feedback");
    setupKnob (tone,     "tone",     "Tone");
    setupKnob (mix,      "mix",      "Mix");

    slope.slider.setTooltip ("Positive: echoes speed up (bouncing ball). Negative: they slow down.");
    taper.slider.setTooltip ("Negative: echoes fade out. Positive: they swell in.");

    if (! ultraTap.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (ultraTap.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    drawnSettings = ultraTap.currentSettings();
    setSize (600, 400);
    startTimerHz (10);
}

void UltraTapEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (ultraTap.parameters, paramId, knob.slider);
}

void UltraTapEditor::timerCallback()
{
    const int id = ultraTap.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    const auto s = ultraTap.currentSettings();
    if (! sameSettings (s, drawnSettings))
    {
        drawnSettings = s;
        repaint (tapDisplay);
    }
}

void UltraTapEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIMPLE ULTRA-TAP", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    // Les répétitions : position dans le temps, hauteur = volume,
    // vers le haut = à gauche, vers le bas = à droite (au centre : les deux)
    auto area = tapDisplay.toFloat();
    g.setColour (juce::Colour (0xff141518));
    g.fillRoundedRectangle (area, 4.0f);

    auto inner = area.reduced (10.0f, 6.0f);
    const float midY = inner.getCentreY();
    const float half = inner.getHeight() * 0.5f;

    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawHorizontalLine ((int) midY, inner.getX(), inner.getRight());

    int count = 0;
    const auto tapList = UltraTapEngine::layout (drawnSettings, count);
    const float span = UltraTapProcessor::maxPreDelayMs + UltraTapProcessor::maxLengthMs;

    // Repère du départ (la note jouée)
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillRect (inner.getX(), midY - half, 2.0f, 2.0f * half);

    for (int k = 0; k < count; ++k)
    {
        const auto& t = tapList[(size_t) k];
        // Échelle racine carrée : les répétitions courtes restent lisibles
        const float x = inner.getX() + inner.getWidth() * std::sqrt (juce::jlimit (0.0f, 1.0f, t.timeMs / span));
        const float up   = half * t.level * juce::jmin (1.0f, 1.0f - t.pan);
        const float down = half * t.level * juce::jmin (1.0f, 1.0f + t.pan);

        g.setColour (accent);
        g.fillRect (x - 1.5f, midY - up, 3.0f, up + down);
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("L", area.withTrimmedRight (6.0f), juce::Justification::topRight);
    g.drawText ("R", area.withTrimmedRight (6.0f), juce::Justification::bottomRight);
    g.drawText (juce::String (count) + (count > 1 ? " echoes over " : " echo at ")
                    + juce::String (juce::roundToInt (drawnSettings.preDelayMs + drawnSettings.lengthMs)) + " ms",
                area.withTrimmedLeft (14.0f).withTrimmedTop (2.0f), juce::Justification::topLeft);
}

void UltraTapEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (6);

    tapDisplay = area.removeFromTop (80);
    area.removeFromTop (6);

    const int rowHeight = area.getHeight() / 2;

    for (auto row : { std::vector<Knob*> { &preDelay, &length, &taps, &slope, &taper },
                      std::vector<Knob*> { &spread, &feedback, &tone, &mix } })
    {
        auto rowArea = area.removeFromTop (rowHeight);
        const int w = rowArea.getWidth() / 5;
        rowArea = rowArea.withSizeKeepingCentre (w * (int) row.size(), rowArea.getHeight());

        for (auto* knob : row)
        {
            auto col = rowArea.removeFromLeft (w);
            knob->label.setBounds (col.removeFromTop (18));
            knob->slider.setBounds (col.reduced (5, 0));
        }
    }
}
