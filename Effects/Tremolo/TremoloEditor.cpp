#include "TremoloEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour panel      (0xff15161a);
    const juce::Colour accent     (0xffc89060);   // tolex brun : les vieux amplis « brownface » à trémolo
    const juce::Colour rightSide  (0xff60c0e0);

    constexpr int cyclesShown = 2;
}

TremoloEditor::TremoloEditor (TremoloProcessor& p)
    : AudioProcessorEditor (&p), tremolo (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < tremolo.getNumPrograms(); ++i)
        presetBox.addItem (tremolo.getProgramName (i), i + 1);

    presetBox.setSelectedId (tremolo.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { tremolo.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupMenu (mode, "mode", "Mode", TremoloProcessor::modeNames,
               "Tremolo: the volume pulses.  Harmonic: lows and highs pulse in opposition, like a 60s brownface amp.  "
               "Auto-Pan: the sound moves from left to right");
    setupMenu (shape, "shape", "Shape", TremoloProcessor::shapeNames,
               "Sine: smooth and round.  Triangle: steady.  Square: choppy, on / off.  Saw: pulsing rhythm");
    setupMenu (division, "division", "", TremoloProcessor::divisionNames,
               "One cycle of the wave, in notes of Logic's tempo");

    sync.setButtonText ("Tempo Sync");
    sync.setTooltip ("Follows Logic's tempo, and locks the wave onto the beats while playing");
    sync.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (sync);
    syncAttachment = std::make_unique<ButtonAttachment> (tremolo.parameters, "sync", sync);

    setupKnob (rate,        "rate",        "Rate",         "Speed of the wave (when Tempo Sync is off)");
    setupKnob (depth,       "depth",       "Depth",        "How deep the volume (or the panning) goes");
    setupKnob (smooth,      "smooth",      "Smooth",       "Rounds off the corners of the wave: square and saw get softer");
    setupKnob (stereoPhase, "stereophase", "Stereo Phase", "Offset between left and right.  180 = ping-pong, one side up while the other is down");
    setupKnob (crossover,   "crossover",   "Crossover",    "Harmonic mode: where the lows and the highs are split");

    if (! tremolo.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (tremolo.parameters, accent);
        addAndMakeVisible (*inputBox);
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    }

    setSize (600, 400);
    timerCallback();
    startTimerHz (30);
}

void TremoloEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (tremolo.parameters, paramId, knob.slider);
}

void TremoloEditor::setupMenu (Menu& menu, const juce::String& paramId, const juce::String& name,
                               const juce::StringArray& items, const juce::String& tooltip)
{
    menu.box.addItemList (items, 1);
    menu.box.setTooltip (tooltip);
    addAndMakeVisible (menu.box);
    menu.attachment = std::make_unique<ComboBoxAttachment> (tremolo.parameters, paramId, menu.box);

    menu.label.setText (name, juce::dontSendNotification);
    menu.label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (menu.label);
}

void TremoloEditor::timerCallback()
{
    const int id = tremolo.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    // Seuls les réglages utiles au mode choisi restent actifs
    const auto s = tremolo.currentSettings();
    const bool synced = tremolo.isSynced();
    rate.slider.setEnabled (! synced);
    division.box.setEnabled (synced);
    stereoPhase.slider.setEnabled (s.mode != TremoloEngine::autoPan);
    crossover.slider.setEnabled (s.mode == TremoloEngine::harmonic);

    for (auto* knob : { &rate, &stereoPhase, &crossover })
        knob->label.setAlpha (knob->slider.isEnabled() ? 1.0f : 0.4f);

    repaint (waveArea.expanded (2));
}

void TremoloEditor::drawWave (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    const auto s = tremolo.currentSettings();
    auto plot = area.reduced (12.0f, 22.0f);

    // Ce que fait l'effet, à gauche (ambre) et à droite (bleu) : volume ou position, deux cycles
    const double offset = s.stereoPhase / 360.0;
    auto level = [&] (double p, bool right)
    {
        const float w = TremoloEngine::wave (s.shape, p);
        if (s.mode == TremoloEngine::autoPan)
            return right ? 0.5f + 0.5f * s.depth * (1.0f - 2.0f * w) : 0.5f - 0.5f * s.depth * (1.0f - 2.0f * w);
        const float wr = TremoloEngine::wave (s.shape, p + (right ? offset : 0.0));
        return 1.0f - s.depth * (1.0f - wr);
    };

    auto toY = [plot] (float v) { return plot.getBottom() - plot.getHeight() * v; };

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int c = 1; c < cyclesShown; ++c)
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * c / cyclesShown), plot.getY(), plot.getBottom());
    g.drawHorizontalLine ((int) toY (0.5f), plot.getX(), plot.getRight());

    const bool twoLines = s.mode == TremoloEngine::autoPan || s.stereoPhase > 0.5f;
    for (bool right : { true, false })
    {
        if (right && ! twoLines)
            continue;

        juce::Path path;
        const int steps = (int) plot.getWidth();
        for (int i = 0; i <= steps; ++i)
        {
            const double p = (double) i / steps * cyclesShown;
            const float x = plot.getX() + (float) i;
            const float y = toY (level (p, right));
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (right ? rightSide : accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    // Où en est l'onde en ce moment
    const double phase = tremolo.getPhase();
    const float x = plot.getX() + (float) (phase / cyclesShown) * plot.getWidth();
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, toY (level (phase, false)) }));

    // Légende et vitesse
    g.setFont (juce::FontOptions (11.0f));
    auto top = area.reduced (12.0f, 4.0f).removeFromTop (16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    juce::String speed = juce::String (tremolo.isSynced() ? tremolo.getCurrentRate() : (float) rate.slider.getValue(), 2) + " Hz";
    if (tremolo.isSynced())
        speed << "  (" << juce::String (tremolo.getCurrentBpm(), 1) << " BPM)";
    g.drawText (speed, top, juce::Justification::centredRight);

    if (s.mode == TremoloEngine::autoPan)
    {
        g.drawText ("R", area.reduced (12.0f, 4.0f).removeFromTop (16.0f), juce::Justification::centredLeft);
        g.drawText ("L", area.reduced (12.0f, 4.0f).removeFromBottom (16.0f), juce::Justification::centredLeft);
    }
    else if (twoLines)
    {
        g.setColour (accent);
        g.drawText ("L", top, juce::Justification::centredLeft);
        g.setColour (rightSide);
        g.drawText ("R", top.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
    }
    else if (s.mode == TremoloEngine::harmonic)
    {
        g.drawText ("LOWS (HIGHS OPPOSITE)", top, juce::Justification::centredLeft);
    }
}

void TremoloEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O TREMOLO", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    drawWave (g, waveArea.toFloat());
}

void TremoloEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (8);

    waveArea = area.removeFromTop (130);
    area.removeFromTop (10);

    // Mode, forme et tempo
    auto options = area.removeFromTop (28);
    mode.label.setBounds (options.removeFromLeft (46));
    mode.box.setBounds (options.removeFromLeft (120).reduced (4, 2));
    shape.label.setBounds (options.removeFromLeft (56));
    shape.box.setBounds (options.removeFromLeft (120).reduced (4, 2));
    options.removeFromLeft (16);
    sync.setBounds (options.removeFromLeft (110));
    division.box.setBounds (options.removeFromLeft (90).reduced (4, 2));
    area.removeFromTop (10);

    const int w = area.getWidth() / 5;
    for (auto* knob : { &rate, &depth, &smooth, &stereoPhase, &crossover })
    {
        auto col = area.removeFromLeft (w);
        knob->label.setBounds (col.removeFromTop (18));
        knob->slider.setBounds (col.reduced (5, 0));
    }
}
