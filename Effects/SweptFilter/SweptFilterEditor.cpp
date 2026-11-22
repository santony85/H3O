#include "SweptFilterEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xff90d040);

    // Réponse (en dB) d'un filtre du 2e ordre de fréquence fc et de facteur Q, compensation comprise
    float responseDb (SweptFilterEngine::Type type, float f, float fc, float q)
    {
        const double w = f / fc, re = 1.0 - w * w, im = w / q;
        const double den = std::sqrt (re * re + im * im);
        double mag = 1.0;
        switch (type)
        {
            case SweptFilterEngine::Type::lowPass:  mag = 1.0 / den; break;
            case SweptFilterEngine::Type::bandPass: mag = (w / q) / den; break;
            case SweptFilterEngine::Type::highPass: mag = (w * w) / den; break;
        }
        mag *= SweptFilterEngine::makeupFor (type, q);
        return (float) (20.0 * std::log10 (std::max (mag, 1.0e-6)));
    }
}

SweptFilterEditor::SweptFilterEditor (SweptFilterProcessor& p)
    : AudioProcessorEditor (&p), swept (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < swept.getNumPrograms(); ++i)
        presetBox.addItem (swept.getProgramName (i), i + 1);

    presetBox.setSelectedId (swept.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { swept.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupChoice (source,    "source",    "Source");
    setupChoice (type,      "type",      "Type");
    setupChoice (shape,     "shape",     "Shape");
    setupChoice (direction, "direction", "Direction");

    setupKnob (low,         "low",         "Low");
    setupKnob (high,        "high",        "High");
    setupKnob (resonance,   "resonance",   "Resonance");
    setupKnob (mix,         "mix",         "Mix");
    setupKnob (rate,        "rate",        "Rate");
    setupKnob (phase,       "phase",       "Stereo Phase");
    setupKnob (sensitivity, "sensitivity", "Sensitivity");
    setupKnob (attack,      "attack",      "Attack");
    setupKnob (release,     "release",     "Release");

    sensitivity.slider.setTooltip ("Raise it if the filter barely opens when you play softly");

    const char* rowNames[] = { "FILTER", "LFO", "ENVELOPE (AUTO-WAH)" };
    for (int r = 0; r < 3; ++r)
    {
        rowLabels[r].setText (rowNames[r], juce::dontSendNotification);
        rowLabels[r].setColour (juce::Label::textColourId, accent);
        rowLabels[r].setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (rowLabels[r]);
    }

    if (! swept.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (swept.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    setSize (600, 500);
    timerCallback();
    startTimerHz (30);
}

void SweptFilterEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (swept.parameters, paramId, knob.slider);
}

void SweptFilterEditor::setupChoice (Choice& choice, const juce::String& paramId, const juce::String& name)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (swept.parameters.getParameter (paramId)))
        choice.box.addItemList (param->choices, 1);
    addAndMakeVisible (choice.box);

    choice.label.setText (name, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (swept.parameters, paramId, choice.box);
}

void SweptFilterEditor::timerCallback()
{
    const int id = swept.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    // Seuls les réglages de la source choisie sont mis en avant
    const bool lfo = swept.currentSettings().source == SweptFilterEngine::Source::lfo;
    for (juce::Component* c : std::initializer_list<juce::Component*> { (juce::Component*) &shape.box, (juce::Component*) &shape.label, &rate.slider, &rate.label, &phase.slider, &phase.label,
                     (juce::Component*) &rowLabels[1] })
        c->setAlpha (lfo ? 1.0f : 0.35f);
    for (juce::Component* c : std::initializer_list<juce::Component*> { (juce::Component*) &direction.box, (juce::Component*) &direction.label, &sensitivity.slider, &sensitivity.label,
                     &attack.slider, &attack.label, &release.slider, &release.label, (juce::Component*) &rowLabels[2] })
        c->setAlpha (lfo ? 0.35f : 1.0f);

    // La courbe suit le filtre
    const float p0 = swept.getPosition (0), p1 = swept.getPosition (1);
    if (std::abs (p0 - drawnPosition[0]) > 0.002f || std::abs (p1 - drawnPosition[1]) > 0.002f)
    {
        drawnPosition[0] = p0;
        drawnPosition[1] = p1;
        repaint (curveDisplay);
    }
}

void SweptFilterEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O SWEPT FILTER", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    // Réponse du filtre à sa position actuelle, sur la plage balayée
    auto area = curveDisplay.toFloat();
    g.setColour (juce::Colour (0xff141518));
    g.fillRoundedRectangle (area, 4.0f);

    const auto s = swept.currentSettings();
    const float q = SweptFilterEngine::qFor (s.resonance);
    auto inner = area.reduced (10.0f, 8.0f);
    const float minHz = 20.0f, maxHz = 20000.0f, topDb = 18.0f, bottomDb = -36.0f;
    auto xFor = [&] (float f) { return inner.getX() + inner.getWidth() * std::log (f / minHz) / std::log (maxHz / minHz); };
    auto yFor = [&] (float db) { return inner.getY() + inner.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };

    // La plage balayée
    const float lo = SweptFilterEngine::frequencyFor (s, 0.0f), hi = SweptFilterEngine::frequencyFor (s, 1.0f);
    g.setColour (accent.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (lo), inner.getY(), xFor (hi), inner.getBottom()));

    g.setColour (juce::Colours::grey.withAlpha (0.35f));
    g.drawHorizontalLine ((int) yFor (0.0f), inner.getX(), inner.getRight());
    g.setFont (juce::FontOptions (10.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        g.setColour (juce::Colours::grey.withAlpha (0.25f));
        g.drawVerticalLine ((int) xFor (f), inner.getY(), inner.getBottom());
        g.setColour (juce::Colours::grey);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (xFor (f) + 3.0f, inner.getBottom() - 12.0f, 30.0f, 11.0f), juce::Justification::left);
    }

    // Une courbe par côté (la droite en plus clair quand elle est décalée)
    for (int c = 1; c >= 0; --c)
    {
        if (c == 1 && std::abs (drawnPosition[1] - drawnPosition[0]) < 0.01f)
            continue;

        const float fc = SweptFilterEngine::frequencyFor (s, std::max (0.0f, drawnPosition[c]));
        juce::Path curve;
        for (float x = inner.getX(); x <= inner.getRight(); x += 2.0f)
        {
            const float f = minHz * std::pow (maxHz / minHz, (x - inner.getX()) / inner.getWidth());
            const float y = yFor (responseDb (s.type, f, fc, q));
            if (x <= inner.getX()) curve.startNewSubPath (x, y);
            else                   curve.lineTo (x, y);
        }
        g.setColour (c == 0 ? accent : accent.brighter (0.6f).withAlpha (0.6f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (juce::roundToInt (SweptFilterEngine::frequencyFor (s, std::max (0.0f, drawnPosition[0])))) + " Hz",
                area.reduced (8.0f, 4.0f), juce::Justification::topRight);
}

void SweptFilterEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (6);

    curveDisplay = area.removeFromTop (110);
    area.removeFromTop (4);

    auto placeKnob = [] (Knob& knob, juce::Rectangle<int> col)
    {
        knob.label.setBounds (col.removeFromTop (18));
        knob.slider.setBounds (col.reduced (4, 0));
    };
    auto placeChoice = [] (Choice& choice, juce::Rectangle<int> col)
    {
        col = col.reduced (4, 0);
        choice.label.setBounds (col.removeFromTop (18));
        choice.box.setBounds (col.withSizeKeepingCentre (col.getWidth(), 26));
    };

    const int rowHeight = area.getHeight() / 3;
    const int w = area.getWidth() / 6;

    // FILTER : Source | Type | Low | High | Resonance | Mix
    auto row = area.removeFromTop (rowHeight);
    rowLabels[0].setBounds (row.removeFromTop (16));
    placeChoice (source, row.removeFromLeft (w));
    placeChoice (type, row.removeFromLeft (w));
    for (auto* k : { &low, &high, &resonance, &mix })
        placeKnob (*k, row.removeFromLeft (w));

    // LFO : Shape | Rate | Stereo Phase
    row = area.removeFromTop (rowHeight);
    rowLabels[1].setBounds (row.removeFromTop (16));
    placeChoice (shape, row.removeFromLeft (w));
    placeKnob (rate, row.removeFromLeft (w));
    placeKnob (phase, row.removeFromLeft (w));

    // ENVELOPE : Direction | Sensitivity | Attack | Release
    row = area.removeFromTop (rowHeight);
    rowLabels[2].setBounds (row.removeFromTop (16));
    placeChoice (direction, row.removeFromLeft (w));
    for (auto* k : { &sensitivity, &attack, &release })
        placeKnob (*k, row.removeFromLeft (w));
}
