#include "ReverbEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xff7090f0);

    bool sameSettings (const ReverbEngine::Settings& a, const ReverbEngine::Settings& b)
    {
        using juce::exactlyEqual;
        return a.type == b.type && exactlyEqual (a.preDelayMs, b.preDelayMs) && exactlyEqual (a.size, b.size)
            && exactlyEqual (a.decaySeconds, b.decaySeconds) && exactlyEqual (a.dampHz, b.dampHz);
    }
}

ReverbEditor::ReverbEditor (ReverbProcessor& p)
    : AudioProcessorEditor (&p), reverb (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < reverb.getNumPrograms(); ++i)
        presetBox.addItem (reverb.getProgramName (i), i + 1);

    presetBox.setSelectedId (reverb.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { reverb.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    typeBox.addItemList ({ "Room", "Hall", "Plate" }, 1);
    addAndMakeVisible (typeBox);
    typeAttachment = std::make_unique<ComboBoxAttachment> (reverb.parameters, "type", typeBox);
    typeLabel.setText ("Type", juce::dontSendNotification);
    typeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (typeLabel);

    setupKnob (preDelay,   "predelay",   "Pre-Delay");
    setupKnob (size,       "size",       "Size");
    setupKnob (decay,      "decay",      "Decay");
    setupKnob (damping,    "damping",    "Damping");
    setupKnob (diffusion,  "diffusion",  "Diffusion");
    setupKnob (modulation, "modulation", "Modulation");
    setupKnob (lowCut,     "lowcut",     "Low Cut");
    setupKnob (highCut,    "highcut",    "High Cut");
    setupKnob (width,      "width",      "Width");
    setupKnob (mix,        "mix",        "Mix");

    damping.slider.setTooltip ("High frequencies die away faster above this frequency (fully right: no damping)");

    if (! reverb.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (reverb.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    drawnSettings = reverb.currentSettings();
    setSize (600, 400);
    startTimerHz (10);
}

void ReverbEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (reverb.parameters, paramId, knob.slider);
}

void ReverbEditor::timerCallback()
{
    const int id = reverb.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    const auto s = reverb.currentSettings();
    if (! sameSettings (s, drawnSettings))
    {
        drawnSettings = s;
        repaint (curveDisplay);
    }
}

void ReverbEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O REVERB", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    // Courbe d'extinction (en dB) : attente du pre-delay, premières réflexions, puis la queue
    auto area = curveDisplay.toFloat();
    g.setColour (juce::Colour (0xff141518));
    g.fillRoundedRectangle (area, 4.0f);

    auto inner = area.reduced (12.0f, 10.0f);
    const auto& s = drawnSettings;
    const float span = juce::jmax (1.0f, s.decaySeconds * 1.15f + s.preDelayMs * 0.001f);   // secondes affichées
    auto xFor = [&] (float t) { return inner.getX() + inner.getWidth() * t / span; };
    auto yFor = [&] (float db) { return inner.getY() + inner.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };

    // Repère -60 dB = fin de la Decay
    g.setColour (juce::Colours::grey.withAlpha (0.3f));
    g.drawVerticalLine ((int) xFor (s.preDelayMs * 0.001f + s.decaySeconds), inner.getY(), inner.getBottom());

    const float start = s.preDelayMs * 0.001f;
    juce::Path tail;
    tail.startNewSubPath (inner.getX(), inner.getBottom());
    tail.lineTo (xFor (start), inner.getBottom());
    tail.lineTo (xFor (start), yFor (-3.0f));
    tail.lineTo (xFor (start + s.decaySeconds), yFor (-60.0f));
    tail.lineTo (xFor (start + s.decaySeconds), inner.getBottom());
    tail.closeSubPath();

    g.setColour (accent.withAlpha (0.35f));
    g.fillPath (tail);
    g.setColour (accent);
    g.strokePath (tail, juce::PathStrokeType (1.5f));

    // Premières réflexions (Room, Hall)
    if (s.type != ReverbEngine::Type::plate)
    {
        const float scale = (s.type == ReverbEngine::Type::hall ? 1.6f : 1.0f) * s.size;
        for (float ms : { 7.1f, 11.3f, 17.9f, 23.3f, 31.7f, 41.9f })
        {
            const float x = xFor (start + ms * scale * 0.001f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawVerticalLine ((int) x, yFor (-8.0f - ms * 0.4f), inner.getBottom());
        }
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));
    const char* typeNames[] = { "Room", "Hall", "Plate" };
    g.drawText (juce::String (typeNames[(int) s.type]) + "   " + juce::String (s.decaySeconds, 1) + " s to -60 dB",
                area.reduced (8.0f, 4.0f), juce::Justification::topRight);
}

void ReverbEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (6);

    curveDisplay = area.removeFromTop (80);
    area.removeFromTop (6);

    const int rowHeight = area.getHeight() / 2;
    const int w = area.getWidth() / 6;

    auto placeKnob = [] (Knob& knob, juce::Rectangle<int> col)
    {
        knob.label.setBounds (col.removeFromTop (18));
        knob.slider.setBounds (col.reduced (4, 0));
    };

    auto row1 = area.removeFromTop (rowHeight);
    {
        auto col = row1.removeFromLeft (w).reduced (4, 0);
        typeLabel.setBounds (col.removeFromTop (18));
        typeBox.setBounds (col.withSizeKeepingCentre (col.getWidth(), 26));
    }
    for (auto* knob : { &preDelay, &size, &decay, &damping, &diffusion })
        placeKnob (*knob, row1.removeFromLeft (w));

    auto row2 = area.removeFromTop (rowHeight);
    row2 = row2.withSizeKeepingCentre (w * 5, row2.getHeight());
    for (auto* knob : { &modulation, &lowCut, &highCut, &width, &mix })
        placeKnob (*knob, row2.removeFromLeft (w));
}
