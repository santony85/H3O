#include "DelayEditor.h"

DelayEditor::DelayEditor (DelayProcessor& p)
    : AudioProcessorEditor (&p), delayProcessor (p)
{
    // Menu des presets d'usine
    for (int i = 0; i < delayProcessor.getNumPrograms(); ++i)
        presetBox.addItem (delayProcessor.getProgramName (i), i + 1);

    presetBox.setSelectedId (delayProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { delayProcessor.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    setupKnob (timeKnob,     "time",     "Time");
    setupKnob (feedbackKnob, "feedback", "Feedback");
    setupKnob (lowCutKnob,   "lowcut",   "Low Cut");
    setupKnob (highCutKnob,  "highcut",  "High Cut");
    setupKnob (mixKnob,      "mix",      "Mix");

    // Bouton Sync : synchronise le délai sur le tempo de Logic
    addAndMakeVisible (syncButton);
    syncAttachment = std::make_unique<ButtonAttachment> (delayProcessor.parameters, "sync", syncButton);

    // Liste des divisions : les éléments doivent exister avant l'attachement (IDs à partir de 1)
    divisionBox.addItemList (DelayProcessor::divisionNames, 1);
    divisionBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (divisionBox);
    divisionAttachment = std::make_unique<ComboBoxAttachment> (delayProcessor.parameters, "division", divisionBox);

    // Ping-Pong : les échos alternent entre gauche et droite
    addAndMakeVisible (pingPongButton);
    pingPongAttachment = std::make_unique<ButtonAttachment> (delayProcessor.parameters, "pingpong", pingPongButton);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe0a040));
    addAndMakeVisible (infoLabel);

    if (! delayProcessor.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (delayProcessor.parameters, juce::Colour (0xffe0a040));
        addAndMakeVisible (*inputBox);
    }

    setSize (600, 270);

    timerCallback();
    startTimerHz (15);
}

void DelayEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffe0a040));
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (delayProcessor.parameters, paramId, knob.slider);
}

void DelayEditor::timerCallback()
{
    // Interroge le bouton plutôt qu'un callback : reste juste aussi quand Logic automatise Sync
    // Garde le menu de presets aligné si Logic change de preset
    const int id = delayProcessor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);

    const bool synced = syncButton.getToggleState();
    timeKnob.slider.setEnabled (! synced);
    timeKnob.label.setEnabled (! synced);
    divisionBox.setEnabled (synced);

    const auto ms  = delayProcessor.getCurrentDelayMs();
    const auto bpm = delayProcessor.getCurrentBpm();

    infoLabel.setText (synced ? juce::String (ms, 1) + " ms  @ " + juce::String (bpm, 1) + " BPM"
                              : juce::String (ms, 1) + " ms",
                       juce::dontSendNotification);
}

void DelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1f24));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("H3O DELAY", getLocalBounds().removeFromTop (40), juce::Justification::centred);
}

void DelayEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (35);
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromLeft (InputMode::Box::preferredWidth).withHeight (30));
    presetBox.setBounds (top.removeFromRight (190).withHeight (30).reduced (0, 2));

    // Rangée du bas : Sync | Division | Ping-Pong | temps effectif
    auto bottom = area.removeFromBottom (30);
    syncButton.setBounds (bottom.removeFromLeft (80));
    divisionBox.setBounds (bottom.removeFromLeft (100).reduced (0, 2));
    bottom.removeFromLeft (20);
    pingPongButton.setBounds (bottom.removeFromLeft (110));
    infoLabel.setBounds (bottom);

    area.removeFromBottom (10);

    const int w = area.getWidth() / 5;
    for (auto* knob : { &timeKnob, &feedbackKnob, &lowCutKnob, &highCutKnob, &mixKnob })
    {
        auto col = area.removeFromLeft (w);
        knob->label.setBounds (col.removeFromTop (20));
        knob->slider.setBounds (col.reduced (5));
    }
}
