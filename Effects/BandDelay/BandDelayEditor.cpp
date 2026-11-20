#include "BandDelayEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xff40c0a0);

    bool sameSettings (const BandDelayEngine::Settings& a, const BandDelayEngine::Settings& b)
    {
        using juce::exactlyEqual;
        return a.bands == b.bands && exactlyEqual (a.timeMs, b.timeMs) && exactlyEqual (a.lowHz, b.lowHz)
            && exactlyEqual (a.highHz, b.highHz) && exactlyEqual (a.resonance, b.resonance)
            && a.pattern == b.pattern && exactlyEqual (a.spread, b.spread);
    }

    juce::String hz (float f)
    {
        return f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f));
    }
}

BandDelayEditor::BandDelayEditor (BandDelayProcessor& p)
    : AudioProcessorEditor (&p), bandDelay (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < bandDelay.getNumPrograms(); ++i)
        presetBox.addItem (bandDelay.getProgramName (i), i + 1);

    presetBox.setSelectedId (bandDelay.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { bandDelay.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupKnob (bands,     "bands",     "Bands");
    setupKnob (time,      "time",      "Time");
    setupKnob (low,       "low",       "Low");
    setupKnob (high,      "high",      "High");
    setupKnob (resonance, "resonance", "Resonance");
    setupKnob (feedback,  "feedback",  "Feedback");
    setupKnob (spread,    "spread",    "Spread");
    setupKnob (mix,       "mix",       "Mix");

    patternBox.addItemList ({ "Rising", "Falling", "Alternating" }, 1);
    patternBox.setTooltip ("Order of the echo colours: low to high, high to low, or alternating");
    addAndMakeVisible (patternBox);
    patternAttachment = std::make_unique<ComboBoxAttachment> (bandDelay.parameters, "pattern", patternBox);

    patternLabel.setText ("Pattern", juce::dontSendNotification);
    patternLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (patternLabel);

    if (! bandDelay.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (bandDelay.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    drawnSettings = bandDelay.currentSettings();
    setSize (600, 400);
    startTimerHz (10);
}

void BandDelayEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (bandDelay.parameters, paramId, knob.slider);
}

void BandDelayEditor::timerCallback()
{
    const int id = bandDelay.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    const auto s = bandDelay.currentSettings();
    if (! sameSettings (s, drawnSettings))
    {
        drawnSettings = s;
        repaint (bandDisplay);
    }
}

void BandDelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIMPLE BAND DELAY", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    // Les échos : à droite = plus tard, en haut = plus aigu.
    // Plein = à gauche, contour = à droite, les deux = au centre
    auto area = bandDisplay.toFloat();
    g.setColour (juce::Colour (0xff141518));
    g.fillRoundedRectangle (area, 4.0f);

    auto inner = area.withTrimmedLeft (22.0f).reduced (8.0f, 10.0f);   // place à gauche pour les repères
    const float minHz = 80.0f, maxHz = 10000.0f;
    auto yFor = [&] (float f) { return inner.getBottom() - inner.getHeight() * std::log (f / minHz) / std::log (maxHz / minHz); };
    auto xFor = [&] (float ms) { return inner.getX() + inner.getWidth() * std::sqrt (juce::jlimit (0.0f, 1.0f, ms / BandDelayProcessor::maxTimeMs)); };

    // Repères de fréquence
    g.setFont (juce::FontOptions (10.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float y = yFor (f);
        g.setColour (juce::Colours::grey.withAlpha (0.25f));
        g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
        g.setColour (juce::Colours::grey);
        g.drawText (hz (f), juce::Rectangle<float> (area.getX() + 2.0f, y - 6.0f, 22.0f, 11.0f).constrainedWithin (area),
                    juce::Justification::left);
    }

    // Départ (la note jouée)
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillRect (inner.getX(), inner.getY(), 2.0f, inner.getHeight());

    int count = 0;
    const auto list = BandDelayEngine::layout (drawnSettings, count);
    // Plus la résonance est forte, plus le point est petit (filtre étroit)
    const float size = juce::jmap (drawnSettings.resonance, 16.0f, 7.0f);

    for (int k = 0; k < count; ++k)
    {
        const auto& b = list[(size_t) k];
        const auto dot = juce::Rectangle<float> (size, size).withCentre ({ xFor (b.timeMs), yFor (b.frequency) });

        g.setColour (accent);
        if (b.pan <= 0.0f)
            g.fillEllipse (dot);
        if (b.pan >= 0.0f)
            g.drawEllipse (dot.expanded (b.pan > 0.0f ? 0.0f : 2.5f), 1.5f);
    }

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (count) + (count > 1 ? " echoes over " : " echo at ")
                    + juce::String (juce::roundToInt (drawnSettings.timeMs)) + " ms   (filled = left, ring = right)",
                area.withTrimmedLeft (36.0f).withTrimmedTop (2.0f), juce::Justification::topLeft);
}

void BandDelayEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (6);

    bandDisplay = area.removeFromTop (90);
    area.removeFromTop (6);

    const int rowHeight = area.getHeight() / 2;
    const int w = area.getWidth() / 5;

    auto placeKnob = [] (Knob& knob, juce::Rectangle<int> col)
    {
        knob.label.setBounds (col.removeFromTop (18));
        knob.slider.setBounds (col.reduced (5, 0));
    };

    auto row1 = area.removeFromTop (rowHeight);
    for (auto* knob : { &bands, &time, &low, &high, &resonance })
        placeKnob (*knob, row1.removeFromLeft (w));

    auto row2 = area.removeFromTop (rowHeight);
    row2 = row2.withSizeKeepingCentre (w * 4, row2.getHeight());
    {
        auto col = row2.removeFromLeft (w).reduced (6, 0);
        patternLabel.setBounds (col.removeFromTop (18));
        patternBox.setBounds (col.withSizeKeepingCentre (col.getWidth(), 26));
    }
    for (auto* knob : { &feedback, &spread, &mix })
        placeKnob (*knob, row2.removeFromLeft (w));
}
