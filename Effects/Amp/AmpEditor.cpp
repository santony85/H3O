#include "AmpEditor.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour accent     (0xffe06040);

    // Dossier proposé : celui du fichier déjà chargé, sinon ta bibliothèque
    // (~/Music/NAM pour les modèles, ~/Music/NAM/IR pour les baffles), ou Téléchargements
    juce::File startFolder (const juce::File& current, const char* library)
    {
        if (current.existsAsFile())
            return current.getParentDirectory();

        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const auto folder = home.getChildFile (library);
        return folder.isDirectory() ? folder : home.getChildFile ("Downloads");
    }
}

AmpEditor::AmpEditor (AmpProcessor& p)
    : AudioProcessorEditor (&p), amp (p)
{
    for (auto* b : { &modelButton, &irButton, &clearModelButton, &clearIrButton })
        addAndMakeVisible (b);

    modelButton.onClick = [this] { chooseModel(); };
    irButton.onClick    = [this] { chooseImpulseResponse(); };
    clearModelButton.onClick = [this] { amp.clearModel(); modelError.clear(); timerCallback(); };
    clearIrButton.onClick    = [this] { amp.clearImpulseResponse(); irError.clear(); timerCallback(); };
    clearModelButton.setTooltip ("Unload the model");
    clearIrButton.setTooltip ("Unload the impulse response");

    for (auto* l : { &modelLabel, &irLabel })
    {
        l->setColour (juce::Label::backgroundColourId, juce::Colour (0xff141518));
        l->setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (l);
    }

    setupKnob (gate,       "gate",       "Gate");
    setupKnob (inputGain,  "inputgain",  "Input");
    setupKnob (bass,       "bass",       "Bass");
    setupKnob (mid,        "mid",        "Mid");
    setupKnob (treble,     "treble",     "Treble");
    setupKnob (outputGain, "outputgain", "Output");

    addAndMakeVisible (normaliseButton);
    addAndMakeVisible (cabButton);
    normaliseButton.setTooltip ("Brings every model to the same loudness");
    cabButton.setTooltip ("Speaker cabinet: your IR if loaded, otherwise a simple cabinet filter");
    normaliseAttachment = std::make_unique<ButtonAttachment> (amp.parameters, "normalize", normaliseButton);
    cabAttachment       = std::make_unique<ButtonAttachment> (amp.parameters, "cab", cabButton);

    if (! amp.input.isInsideChain())
    {
        inputBox = std::make_unique<InputMode::Box> (amp.parameters, accent);
        addAndMakeVisible (*inputBox);
    }

    setSize (600, 330);

    timerCallback();
    startTimerHz (5);
}

void AmpEditor::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (amp.parameters, paramId, knob.slider);
}

void AmpEditor::chooseModel()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a NAM model", startFolder (amp.getModelFile(), "Music/NAM"), "*.nam");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<AmpEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        safe->modelError = safe->amp.loadModel (fc.getResult());
        safe->timerCallback();
    });
}

void AmpEditor::chooseImpulseResponse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a cabinet impulse response",
                                                   startFolder (amp.getImpulseResponseFile(), "Music/NAM/IR"), "*.wav;*.aif;*.aiff");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<AmpEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;

        safe->irError = safe->amp.loadImpulseResponse (fc.getResult());
        safe->timerCallback();
    });
}

void AmpEditor::timerCallback()
{
    // Modèle
    const auto info = amp.getModelInfo();
    juce::String modelText;
    auto modelColour = juce::Colours::white;

    // Erreur de ce chargement, ou d'un modèle de session introuvable
    const auto error = modelError.isNotEmpty() ? modelError : amp.getLastModelError();

    if (error.isNotEmpty())
    {
        modelText = error;
        modelColour = juce::Colours::orangered;
    }
    else if (info.name.isEmpty())
    {
        modelText = "No model: clean signal";
        modelColour = juce::Colours::grey;
    }
    else
    {
        modelText = info.name + "   " + juce::String (info.sampleRate / 1000.0, 1) + " kHz";
        if (info.resampled)
            modelText << " (resampled from " << juce::String (amp.getSampleRate() / 1000.0, 1) << " kHz)";
    }

    modelLabel.setText (modelText, juce::dontSendNotification);
    modelLabel.setColour (juce::Label::textColourId, modelColour);
    clearModelButton.setEnabled (info.name.isNotEmpty());

    // Baffle
    const auto ir = amp.getImpulseResponseFile();
    juce::String irText;
    auto irColour = juce::Colours::white;

    if (irError.isNotEmpty())
    {
        irText = irError;
        irColour = juce::Colours::orangered;
    }
    else if (! cabButton.getToggleState())
    {
        irText = "Cabinet off";
        irColour = juce::Colours::grey;
    }
    else if (ir == juce::File())
    {
        irText = "Simple cabinet filter (no IR loaded)";
        irColour = juce::Colours::grey;
    }
    else
    {
        irText = ir.getFileNameWithoutExtension();
    }

    irLabel.setText (irText, juce::dontSendNotification);
    irLabel.setColour (juce::Label::textColourId, irColour);
    clearIrButton.setEnabled (ir != juce::File());
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIMPLE AMP", getLocalBounds().removeFromTop (40).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Neural Amp Modeler", getLocalBounds().removeFromTop (40).withTrimmedLeft (145),
                juce::Justification::centredLeft);
}

void AmpEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    if (inputBox != nullptr)
        inputBox->setBounds (top.removeFromRight (InputMode::Box::preferredWidth));
    area.removeFromTop (6);

    // Lignes Modèle et IR : bouton | nom du fichier | x
    for (auto [button, label, clear] : { std::tuple { &modelButton, &modelLabel, &clearModelButton },
                                         std::tuple { &irButton, &irLabel, &clearIrButton } })
    {
        auto row = area.removeFromTop (28);
        button->setBounds (row.removeFromLeft (120));
        row.removeFromLeft (6);
        clear->setBounds (row.removeFromRight (28));
        row.removeFromRight (4);
        label->setBounds (row);
        area.removeFromTop (6);
    }

    auto bottom = area.removeFromBottom (26);
    normaliseButton.setBounds (bottom.removeFromLeft (110));
    cabButton.setBounds (bottom.removeFromLeft (80));
    area.removeFromBottom (4);

    const int w = area.getWidth() / 6;
    for (auto* knob : { &gate, &inputGain, &bass, &mid, &treble, &outputGain })
    {
        auto col = area.removeFromLeft (w);
        knob->label.setBounds (col.removeFromTop (18));
        knob->slider.setBounds (col.reduced (2, 0));
    }
}
