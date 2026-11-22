#include "CompressorEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour panel      (0xff15161a);
    const juce::Colour accent     (0xffc8d0e0);   // argent : un compresseur de studio
    const juce::Colour reduction  (0xfff0a040);

    constexpr float minDb = -60.0f;   // échelle de la courbe : −60 à 0 dBFS
    constexpr float maxReductionShown = 24.0f;
}

CompressorEditor::CompressorEditor (CompressorProcessor& p)
    : AudioProcessorEditor (&p), compressor (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < compressor.getNumPrograms(); ++i)
        presetBox.addItem (compressor.getProgramName (i), i + 1);

    presetBox.setSelectedId (compressor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { compressor.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    styleBox.addItemList ({ "Clean", "Opto", "Punch" }, 1);
    styleBox.setTooltip ("Clean: precise and transparent.  Opto: smooth, program-dependent, like an optical cell.  "
                         "Punch: feedback design, lets the pick attack through.");
    addAndMakeVisible (styleBox);
    styleAttachment = std::make_unique<ComboBoxAttachment> (compressor.parameters, "style", styleBox);
    styleLabel.setText ("Style", juce::dontSendNotification);
    styleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (styleLabel);

    setupToggle (autoRelease, autoReleaseAttachment, "autorelease", "Auto Release",
                 "Release follows your playing: fast after a single note, slower after sustained compression (always on in Opto)");
    setupToggle (autoMakeup, autoMakeupAttachment, "automakeup", "Auto Makeup",
                 "Brings back the level lost to compression (Makeup adds on top)");

    setupKnob (threshold, "threshold", "Threshold", "Level above which compression starts");
    setupKnob (ratio,     "ratio",     "Ratio",     "4:1 = 4 dB over the threshold come out as 1 dB.  20:1 = limiter");
    setupKnob (knee,      "knee",      "Knee",      "0 dB = hard knee.  Higher = compression eases in around the threshold");
    setupKnob (attack,    "attack",    "Attack",    "Longer attack lets the pick transient through before compressing");
    setupKnob (release,   "release",   "Release",   "Time to recover after the note");
    setupKnob (sidechain, "sidechain", "SC Low Cut", "Low cut on the detector only: low notes stop making the compressor pump");
    setupKnob (makeup,    "makeup",    "Makeup",    "Output gain (added to Auto Makeup)");
    setupKnob (mix,       "mix",       "Mix",       "Parallel compression: below 100 % the dry attack is blended back in");

    if (! compressor.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (compressor.parameters, accent);
        addAndMakeVisible (*inputBox);
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    }

    setSize (600, 470);
    startTimerHz (30);
}

void CompressorEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name, const juce::String& tooltip)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setTooltip (tooltip);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (compressor.parameters, paramId, knob.slider);
}

void CompressorEditor::setupToggle (juce::ToggleButton& button, std::unique_ptr<ButtonAttachment>& attachment,
                                    const juce::String& paramId, const juce::String& name, const juce::String& tooltip)
{
    button.setButtonText (name);
    button.setTooltip (tooltip);
    button.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (button);
    attachment = std::make_unique<ButtonAttachment> (compressor.parameters, paramId, button);
}

void CompressorEditor::timerCallback()
{
    const int id = compressor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    // Vu-mètre : monte tout de suite, redescend doucement ; crête tenue une seconde
    const float now = compressor.getGainReduction();
    shownReduction = now > shownReduction ? now : shownReduction * 0.85f + now * 0.15f;
    if (now >= peakReduction || --peakHold <= 0)
    {
        peakReduction = now;
        peakHold = 30;
    }
    shownLevel = juce::jmax (compressor.getInputLevel(), shownLevel - 1.5f);

    repaint (curveArea.getUnion (meterArea).expanded (2));
}

void CompressorEditor::drawCurve (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    const auto s = compressor.currentSettings();
    const float kneeDb = s.kneeDb + (s.style == CompressorEngine::opto ? 6.0f : 0.0f);
    const float makeupDb = s.makeupDb + (s.autoMakeup ? CompressorEngine::autoMakeupDb (s) : 0.0f);

    auto plot = area.reduced (8.0f);
    auto toX = [plot] (float d) { return juce::jmap (d, minDb, 0.0f, plot.getX(), plot.getRight()); };
    auto toY = [plot] (float d) { return juce::jmap (juce::jlimit (minDb, 6.0f, d), minDb, 6.0f, plot.getBottom(), plot.getY()); };

    // Grille tous les 12 dB, et la diagonale « sans compression »
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (float d = -48.0f; d < 0.0f; d += 12.0f)
    {
        g.drawVerticalLine ((int) toX (d), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) toY (d), plot.getX(), plot.getRight());
    }
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine (toX (minDb), toY (minDb), toX (0.0f), toY (0.0f), 1.0f);

    // Seuil
    g.setColour (accent.withAlpha (0.35f));
    g.drawVerticalLine ((int) toX (s.thresholdDb), plot.getY(), plot.getBottom());

    // Courbe (gain compris)
    juce::Path curve;
    for (float d = minDb; d <= 0.0f; d += 0.5f)
    {
        const float out = CompressorEngine::curve (d, s.thresholdDb, juce::jmax (1.0f, s.ratio), kneeDb) + makeupDb;
        if (d <= minDb)
            curve.startNewSubPath (toX (d), toY (out));
        else
            curve.lineTo (toX (d), toY (out));
    }
    g.setColour (accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // Où tu joues en ce moment
    if (shownLevel > minDb)
    {
        const float level = juce::jmin (shownLevel, 0.0f);
        const float out = CompressorEngine::curve (level, s.thresholdDb, juce::jmax (1.0f, s.ratio), kneeDb) + makeupDb;
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ toX (level), toY (out) }));
    }

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("IN", plot.removeFromBottom (14.0f).removeFromRight (20.0f), juce::Justification::centredRight);
    g.drawText ("OUT", area.reduced (8.0f).removeFromTop (14.0f).removeFromLeft (30.0f), juce::Justification::centredLeft);
}

void CompressorEditor::drawMeter (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    auto text = area.removeFromBottom (20.0f);
    auto title = area.removeFromTop (18.0f);
    auto bar = area.reduced (12.0f, 4.0f);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("GR", title, juce::Justification::centred);

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRect (bar);

    // La réduction se lit de haut en bas
    auto toY = [bar] (float d) { return bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, d / maxReductionShown); };
    g.setColour (reduction);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bar.getX(), bar.getY(), bar.getRight(), toY (shownReduction)));
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawHorizontalLine ((int) toY (peakReduction), bar.getX(), bar.getRight());

    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.setFont (juce::FontOptions (9.0f));
    for (float d : { 3.0f, 6.0f, 12.0f, 18.0f })
    {
        g.drawHorizontalLine ((int) toY (d), bar.getX() - 4.0f, bar.getX());
        g.drawText (juce::String ((int) d), juce::Rectangle<float> (bar.getRight() + 1.0f, toY (d) - 6.0f, 14.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (-shownReduction, 1) + " dB", text, juce::Justification::centred);
}

void CompressorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O COMPRESSOR", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    drawCurve (g, curveArea.toFloat());
    drawMeter (g, meterArea.toFloat());
}

void CompressorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromRight (220).reduced (0, 2));
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth + 10).withTrimmedRight (10));
    area.removeFromTop (8);

    // Courbe et vu-mètre
    auto display = area.removeFromTop (150);
    meterArea = display.removeFromRight (64);
    display.removeFromRight (8);
    curveArea = display;
    area.removeFromTop (8);

    // Style et automatismes
    auto options = area.removeFromTop (28);
    styleLabel.setBounds (options.removeFromLeft (50));
    styleBox.setBounds (options.removeFromLeft (130).reduced (4, 2));
    options.removeFromLeft (20);
    autoRelease.setBounds (options.removeFromLeft (130));
    autoMakeup.setBounds (options.removeFromLeft (130));
    area.removeFromTop (6);

    const int rowHeight = area.getHeight() / 2;
    for (auto row : { std::array<Knob*, 4> { &threshold, &ratio, &knee, &attack },
                      std::array<Knob*, 4> { &release, &sidechain, &makeup, &mix } })
    {
        auto rowArea = area.removeFromTop (rowHeight);
        const int w = rowArea.getWidth() / 4;

        for (auto* knob : row)
        {
            auto col = rowArea.removeFromLeft (w);
            knob->label.setBounds (col.removeFromTop (18));
            knob->slider.setBounds (col.reduced (5, 0));
        }
    }
}
