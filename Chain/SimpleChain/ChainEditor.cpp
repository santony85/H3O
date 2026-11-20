#include "ChainEditor.h"

ChainEditor::ChainEditor (ChainProcessor& p)
    : AudioProcessorEditor (&p), chain (p)
{
    addAndMakeVisible (inputBox);

    // Même ordre que le signal, mêmes couleurs que les interfaces des effets
    addPedal ("Amp",        juce::Colour (0xffe06040), chain.amp,        "ampon",        [this] { return chain.isAmpOn(); });
    addPedal ("Harmonizer", juce::Colour (0xff40b0e0), chain.harmonizer, "harmonizeron", [this] { return chain.isHarmonizerOn(); });
    addPedal ("Chorus",     juce::Colour (0xffb070e0), chain.chorus,     "choruson",     [this] { return chain.isChorusOn(); });
    addPedal ("Delay",      juce::Colour (0xffe0a040), chain.delay,      "delayon",      [this] { return chain.isDelayOn(); });

    // Assez de place pour la plus grande des interfaces
    int width = 0, height = 0;
    for (auto& pedal : pedals)
    {
        width  = juce::jmax (width, pedal.editor->getWidth());
        height = juce::jmax (height, pedal.editor->getHeight());
    }

    setSize (width, headerHeight + stripHeight + height);
    select (0);

    timerCallback();
    startTimerHz (10);
}

ChainEditor::~ChainEditor()
{
    // Les interfaces des effets doivent disparaître avant le reste
    for (auto& pedal : pedals)
        pedal.editor.reset();
}

void ChainEditor::addPedal (const juce::String& name, juce::Colour colour, juce::AudioProcessor& effect,
                            const juce::String& onParamId, std::function<bool()> isOn)
{
    Pedal pedal;
    pedal.name   = name;
    pedal.colour = colour;
    pedal.isOn   = std::move (isOn);

    pedal.editor.reset (effect.createEditorIfNeeded());
    addChildComponent (*pedal.editor);

    const int index = (int) pedals.size();

    pedal.selectButton = std::make_unique<juce::TextButton> (name);
    pedal.selectButton->setClickingTogglesState (false);
    pedal.selectButton->setColour (juce::TextButton::buttonOnColourId, colour.darker (0.3f));
    pedal.selectButton->onClick = [this, index] { select (index); };
    addAndMakeVisible (*pedal.selectButton);

    pedal.onButton = std::make_unique<juce::ToggleButton> ("On");
    pedal.onButton->setColour (juce::ToggleButton::tickColourId, colour);
    addAndMakeVisible (*pedal.onButton);
    pedal.onAttachment = std::make_unique<ButtonAttachment> (chain.parameters, onParamId, *pedal.onButton);

    pedals.push_back (std::move (pedal));
}

void ChainEditor::select (int index)
{
    selected = index;

    for (int i = 0; i < (int) pedals.size(); ++i)
    {
        pedals[(size_t) i].editor->setVisible (i == index);
        pedals[(size_t) i].selectButton->setToggleState (i == index, juce::dontSendNotification);
    }

    repaint();
}

void ChainEditor::timerCallback()
{
    for (auto& pedal : pedals)
        pedal.editor->setAlpha (pedal.isOn() ? 1.0f : 0.35f);

    repaint (0, headerHeight, getWidth(), stripHeight);
}

void ChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141518));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("SIMPLE CHAIN", getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    // Les flèches du signal entre les pédales
    for (size_t i = 0; i + 1 < pedals.size(); ++i)
    {
        const auto b    = pedals[i].selectButton->getBounds().toFloat();
        const auto next = pedals[i + 1].selectButton->getBounds().toFloat();
        const auto y    = b.getCentreY();
        g.setColour (juce::Colours::grey);
        g.drawArrow ({ b.getRight() + 4.0f, y, next.getX() - 4.0f, y }, 2.0f, 9.0f, 8.0f);
    }

    // Fine ligne de la couleur de la pédale affichée, au-dessus de ses réglages
    if (juce::isPositiveAndBelow (selected, (int) pedals.size()))
    {
        g.setColour (pedals[(size_t) selected].colour);
        g.fillRect (0, headerHeight + stripHeight - 3, getWidth(), 3);
    }
}

void ChainEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight);
    inputBox.setBounds (header.removeFromRight (InputMode::Box::preferredWidth + 15).withTrimmedRight (15).reduced (0, 5));

    // Rangée de pédales : nom (cliquable) au-dessus, interrupteur en dessous
    auto strip = getLocalBounds().withTrimmedTop (headerHeight).removeFromTop (stripHeight - 3).reduced (10, 4);
    const int n = (int) pedals.size();
    const int pedalWidth = (strip.getWidth() - (n - 1) * arrowWidth) / n;

    for (int i = 0; i < n; ++i)
    {
        auto col = strip.removeFromLeft (pedalWidth);
        if (i + 1 < n)
            strip.removeFromLeft (arrowWidth);

        auto& pedal = pedals[(size_t) i];
        pedal.selectButton->setBounds (col.removeFromTop (30));
        pedal.onButton->setBounds (col.withSizeKeepingCentre (60, col.getHeight()));
    }

    // Réglages de la pédale affichée
    for (auto& pedal : pedals)
        pedal.editor->setTopLeftPosition ((getWidth() - pedal.editor->getWidth()) / 2, headerHeight + stripHeight);
}
