#include "FlangerEditor.h"
#include <complex>

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour panel      (0xff15161a);
    const juce::Colour accent     (0xff9ad0ff);   // bleu acier : le métal du « jet »
    const juce::Colour rightSide  (0xff60c0e0);

    constexpr float minHz = 20.0f, maxHz = 20000.0f, minDb = -30.0f, maxDb = 12.0f;

    // Réponse du flanger (dB) à une fréquence, pour un retard de l'effet donné (ms)
    float responseDb (const FlangerEngine::Settings& s, float delayMs, float hz)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz;
        const auto wetDelay = std::polar (1.0, -w * delayMs * 0.001);
        const auto wet = wetDelay / (1.0 - (double) s.feedback * wetDelay);
        const auto dry = s.mode == FlangerEngine::throughZero ? std::polar (1.0, -w * s.manualMs * 0.001) : std::complex<double> (1.0);
        const auto out = (1.0 - (double) s.mix) * dry + (double) s.mix * wet;
        return (float) (20.0 * std::log10 (std::max (std::abs (out), 1.0e-6)));
    }
}

FlangerEditor::FlangerEditor (FlangerProcessor& p)
    : AudioProcessorEditor (&p), flanger (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < flanger.getNumPrograms(); ++i)
        presetBox.addItem (flanger.getProgramName (i), i + 1);

    presetBox.setSelectedId (flanger.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { flanger.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupMenu (mode, "mode", "Mode", FlangerProcessor::modeNames,
               "Classic: the sweep goes around Manual.  Through-Zero: the dry sound is delayed too, and the effect crosses it: the deep cancel of tape flanging");
    setupMenu (shape, "shape", "Shape", FlangerProcessor::shapeNames,
               "Triangle: steady sweep, like most flanger pedals.  Sine: lingers at the ends of the sweep");
    setupMenu (division, "division", "", FlangerProcessor::divisionNames,
               "One full sweep (up and down), in notes of Logic's tempo");

    sync.setButtonText ("Tempo Sync");
    sync.setTooltip ("Follows Logic's tempo, and locks the sweep onto the beats while playing");
    sync.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (sync);
    syncAttachment = std::make_unique<ButtonAttachment> (flanger.parameters, "sync", sync);

    setupKnob (rate,        "rate",        "Rate",         "Speed of the sweep (when Tempo Sync is off)");
    setupKnob (depth,       "depth",       "Depth",        "Width of the sweep.  0 = a fixed comb, set by Manual");
    setupKnob (manual,      "manual",      "Manual",       "Delay at the middle of the sweep: shorter = higher, brighter notches; longer = lower, closer notches");
    setupKnob (feedback,    "feedback",    "Feedback",     "Resonance: metallic, jet-like.  Negative = hollow, softer colour");
    setupKnob (stereoPhase, "stereophase", "Stereo Phase", "Offset of the right side.  180 = the notches go up on one side while they go down on the other");
    setupKnob (mix,         "mix",         "Mix",          "50 % = the classic flanger, deepest notches.  Lower = subtler");

    if (! flanger.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (flanger.parameters, accent);
        addAndMakeVisible (*inputBox);
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    }

    setSize (640, 410);
    timerCallback();
    startTimerHz (30);
}

void FlangerEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (flanger.parameters, paramId, knob.slider);
}

void FlangerEditor::setupMenu (Menu& menu, const juce::String& paramId, const juce::String& name,
                              const juce::StringArray& items, const juce::String& tooltip)
{
    menu.box.addItemList (items, 1);
    menu.box.setTooltip (tooltip);
    addAndMakeVisible (menu.box);
    menu.attachment = std::make_unique<ComboBoxAttachment> (flanger.parameters, paramId, menu.box);

    menu.label.setText (name, juce::dontSendNotification);
    menu.label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (menu.label);
}

void FlangerEditor::timerCallback()
{
    const int id = flanger.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    const bool synced = flanger.isSynced();
    rate.slider.setEnabled (! synced);
    rate.label.setAlpha (synced ? 0.4f : 1.0f);
    division.box.setEnabled (synced);

    repaint (responseArea.expanded (2));
}

void FlangerEditor::drawResponse (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    const auto s = flanger.currentSettings();
    auto plot = area.reduced (10.0f, 18.0f);

    auto toX = [plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz); };
    auto toY = [plot] (float d) { return juce::jmap (juce::jlimit (minDb, maxDb, d), minDb, maxDb, plot.getBottom(), plot.getY()); };

    // Grille : octaves de 100 Hz à 10 kHz, et 0 dB
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) toX (hz), plot.getY(), plot.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) toY (0.0f), plot.getX(), plot.getRight());

    FlangerEngine engine;
    engine.setSettings (s);

    // La courbe en ce moment, à gauche (orange) et à droite (bleu) si elles diffèrent
    const double phase = flanger.getPhase();
    for (bool right : { true, false })
    {
        if (right && s.stereoPhase < 0.5f)
            continue;

        const float w = FlangerEngine::wave (s.shape, phase + (right ? s.stereoPhase / 360.0 : 0.0));
        const float delayMs = engine.delayMs (w);

        juce::Path path;
        for (float x = plot.getX(); x <= plot.getRight(); x += 1.0f)
        {
            const float hz = minHz * std::pow (maxHz / minHz, (x - plot.getX()) / plot.getWidth());
            const float y = toY (responseDb (s, delayMs, hz));
            if (x <= plot.getX())
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (right ? rightSide : accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    for (auto [hz, text] : { std::pair { 100.0f, "100" }, std::pair { 1000.0f, "1k" }, std::pair { 10000.0f, "10k" } })
        g.drawText (text, juce::Rectangle<float> (toX (hz) + 3.0f, plot.getBottom() + 2.0f, 30.0f, 14.0f), juce::Justification::centredLeft);

    auto top = area.reduced (12.0f, 2.0f).removeFromTop (16.0f);
    juce::String speed = juce::String (flanger.isSynced() ? flanger.getCurrentRate() : (float) rate.slider.getValue(), 2) + " Hz";
    if (flanger.isSynced())
        speed << "  (" << juce::String (flanger.getCurrentBpm(), 1) << " BPM)";
    g.drawText (speed, top, juce::Justification::centredRight);
    const float now = engine.delayMs (FlangerEngine::wave (s.shape, flanger.getPhase()));
    g.drawText ((s.mode == FlangerEngine::throughZero ? "THROUGH-ZERO  " : "CLASSIC  ") + juce::String (now, 2) + " ms",
                top, juce::Justification::centredLeft);
}

void FlangerEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O FLANGER", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    drawResponse (g, responseArea.toFloat());
}

void FlangerEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (8);

    responseArea = area.removeFromTop (140);
    area.removeFromTop (10);

    // Étages, forme et tempo
    auto options = area.removeFromTop (28);
    mode.label.setBounds (options.removeFromLeft (46));
    mode.box.setBounds (options.removeFromLeft (130).reduced (4, 2));
    shape.label.setBounds (options.removeFromLeft (56));
    shape.box.setBounds (options.removeFromLeft (120).reduced (4, 2));
    options.removeFromLeft (16);
    sync.setBounds (options.removeFromLeft (110));
    division.box.setBounds (options.removeFromLeft (90).reduced (4, 2));
    area.removeFromTop (10);

    const int w = area.getWidth() / 6;
    for (auto* knob : { &rate, &depth, &manual, &feedback, &stereoPhase, &mix })
    {
        auto col = area.removeFromLeft (w);
        knob->label.setBounds (col.removeFromTop (18));
        knob->slider.setBounds (col.reduced (5, 0));
    }
}
