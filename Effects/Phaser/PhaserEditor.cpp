#include "PhaserEditor.h"
#include <complex>

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour panel      (0xff15161a);
    const juce::Colour accent     (0xfff08030);   // orange : la couleur des phasers des années 70
    const juce::Colour rightSide  (0xff60c0e0);

    constexpr float minHz = 20.0f, maxHz = 20000.0f, minDb = -30.0f, maxDb = 12.0f;

    // Réponse du phaser (dB) à une fréquence, pour une fréquence des passe-tout donnée
    float responseDb (const PhaserEngine::Settings& s, float sweepHz, float hz, double sampleRate)
    {
        const double t = std::tan (juce::MathConstants<double>::pi * sweepHz / sampleRate);
        const double a = (t - 1.0) / (t + 1.0);
        const auto z1 = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz / sampleRate);   // z⁻¹
        const auto stage = (a + z1) / (1.0 + a * z1);
        const auto chain = std::pow (stage, s.stages);
        const auto wet = chain / (1.0 - (double) s.feedback * z1 * chain);
        const auto out = (1.0 - (double) s.mix) + (double) s.mix * wet;
        return (float) (20.0 * std::log10 (std::max (std::abs (out), 1.0e-6)));
    }
}

PhaserEditor::PhaserEditor (PhaserProcessor& p)
    : AudioProcessorEditor (&p), phaser (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < phaser.getNumPrograms(); ++i)
        presetBox.addItem (phaser.getProgramName (i), i + 1);

    presetBox.setSelectedId (phaser.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { phaser.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupMenu (stages, "stages", "Stages", PhaserProcessor::stageNames,
               "Number of all-pass stages: 4 = the classic pedal (2 notches), more stages = more notches, a deeper swirl");
    setupMenu (shape, "shape", "Shape", PhaserProcessor::shapeNames,
               "Triangle: steady sweep, like most phaser pedals.  Sine: lingers at the top and bottom");
    setupMenu (division, "division", "", PhaserProcessor::divisionNames,
               "One full sweep (up and down), in notes of Logic's tempo");

    sync.setButtonText ("Tempo Sync");
    sync.setTooltip ("Follows Logic's tempo, and locks the sweep onto the beats while playing");
    sync.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (sync);
    syncAttachment = std::make_unique<ButtonAttachment> (phaser.parameters, "sync", sync);

    setupKnob (rate,        "rate",        "Rate",         "Speed of the sweep (when Tempo Sync is off)");
    setupKnob (depth,       "depth",       "Depth",        "Width of the sweep: up to 3 octaves each way around the centre");
    setupKnob (centre,      "centre",      "Centre",       "Middle of the sweep: lower = darker and throatier, higher = brighter");
    setupKnob (feedback,    "feedback",    "Feedback",     "Resonance: sharper, more vocal notches.  Negative = hollow, nasal colour");
    setupKnob (stereoPhase, "stereophase", "Stereo Phase", "Offset of the right side.  180 = the notches go up on one side while they go down on the other");
    setupKnob (mix,         "mix",         "Mix",          "50 % = the classic phaser, deepest notches.  Lower = subtler");

    if (! phaser.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (phaser.parameters, accent);
        addAndMakeVisible (*inputBox);
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    }

    setSize (640, 410);
    timerCallback();
    startTimerHz (30);
}

void PhaserEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (phaser.parameters, paramId, knob.slider);
}

void PhaserEditor::setupMenu (Menu& menu, const juce::String& paramId, const juce::String& name,
                              const juce::StringArray& items, const juce::String& tooltip)
{
    menu.box.addItemList (items, 1);
    menu.box.setTooltip (tooltip);
    addAndMakeVisible (menu.box);
    menu.attachment = std::make_unique<ComboBoxAttachment> (phaser.parameters, paramId, menu.box);

    menu.label.setText (name, juce::dontSendNotification);
    menu.label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (menu.label);
}

void PhaserEditor::timerCallback()
{
    const int id = phaser.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    const bool synced = phaser.isSynced();
    rate.slider.setEnabled (! synced);
    rate.label.setAlpha (synced ? 0.4f : 1.0f);
    division.box.setEnabled (synced);

    repaint (responseArea.expanded (2));
}

void PhaserEditor::drawResponse (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    const auto s = phaser.currentSettings();
    const double sampleRate = phaser.getSampleRate() > 0.0 ? phaser.getSampleRate() : 48000.0;
    auto plot = area.reduced (10.0f, 18.0f);

    auto toX = [plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz); };
    auto toY = [plot] (float d) { return juce::jmap (juce::jlimit (minDb, maxDb, d), minDb, maxDb, plot.getBottom(), plot.getY()); };

    // Grille : octaves de 100 Hz à 10 kHz, et 0 dB
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) toX (hz), plot.getY(), plot.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawHorizontalLine ((int) toY (0.0f), plot.getX(), plot.getRight());

    // Les limites du balayage
    PhaserEngine engine;
    engine.prepare (sampleRate);
    engine.setSettings (s);
    g.setColour (accent.withAlpha (0.08f));
    const float lowX = toX (engine.sweepFrequency (0.0f)), highX = toX (engine.sweepFrequency (1.0f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (lowX, plot.getY(), highX, plot.getBottom()));

    // La courbe en ce moment, à gauche (orange) et à droite (bleu) si elles diffèrent
    const double phase = phaser.getPhase();
    for (bool right : { true, false })
    {
        if (right && s.stereoPhase < 0.5f)
            continue;

        const float w = PhaserEngine::wave (s.shape, phase + (right ? s.stereoPhase / 360.0 : 0.0));
        const float sweepHz = engine.sweepFrequency (w);

        juce::Path path;
        for (float x = plot.getX(); x <= plot.getRight(); x += 1.0f)
        {
            const float hz = minHz * std::pow (maxHz / minHz, (x - plot.getX()) / plot.getWidth());
            const float y = toY (responseDb (s, sweepHz, hz, sampleRate));
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
    juce::String speed = juce::String (phaser.isSynced() ? phaser.getCurrentRate() : (float) rate.slider.getValue(), 2) + " Hz";
    if (phaser.isSynced())
        speed << "  (" << juce::String (phaser.getCurrentBpm(), 1) << " BPM)";
    g.drawText (speed, top, juce::Justification::centredRight);
    g.drawText (juce::String (s.stages) + " STAGES", top, juce::Justification::centredLeft);
}

void PhaserEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O PHASER", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    drawResponse (g, responseArea.toFloat());
}

void PhaserEditor::resized()
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
    stages.label.setBounds (options.removeFromLeft (52));
    stages.box.setBounds (options.removeFromLeft (80).reduced (4, 2));
    shape.label.setBounds (options.removeFromLeft (56));
    shape.box.setBounds (options.removeFromLeft (120).reduced (4, 2));
    options.removeFromLeft (16);
    sync.setBounds (options.removeFromLeft (110));
    division.box.setBounds (options.removeFromLeft (90).reduced (4, 2));
    area.removeFromTop (10);

    const int w = area.getWidth() / 6;
    for (auto* knob : { &rate, &depth, &centre, &feedback, &stereoPhase, &mix })
    {
        auto col = area.removeFromLeft (w);
        knob->label.setBounds (col.removeFromTop (18));
        knob->slider.setBounds (col.reduced (5, 0));
    }
}
