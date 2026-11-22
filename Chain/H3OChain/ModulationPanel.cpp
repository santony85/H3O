#include "ModulationPanel.h"

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour boxColour  (0xff26282e);
    const juce::Colour dim        (0xff8a8d96);
}

ModulationPanel::ModulationPanel (ChainProcessor& p)
    : chain (p)
{
    setupKnob (lfo1Rate,   "lfo1rate",   "Rate");
    setupKnob (lfo2Rate,   "lfo2rate",   "Rate");
    setupKnob (randomRate, "randomrate", "Rate");
    setupKnob (envAttack,  "envattack",  "Attack");
    setupKnob (envRelease, "envrelease", "Release");

    boxes[0].title = "LFO 1";    boxes[0].source = ModulationSources::lfo1;     boxes[0].knobs = { &lfo1Rate };
    boxes[1].title = "LFO 2";    boxes[1].source = ModulationSources::lfo2;     boxes[1].knobs = { &lfo2Rate };
    boxes[2].title = "Random";   boxes[2].source = ModulationSources::random;   boxes[2].knobs = { &randomRate };
    boxes[3].title = "Envelope"; boxes[3].source = ModulationSources::envelope; boxes[3].knobs = { &envAttack, &envRelease };
    boxes[4].title = "Pitch";    boxes[4].source = ModulationSources::pitch;

    for (int l = 0; l < 2; ++l)
    {
        auto& box = boxes[(size_t) l];
        for (int s = 0; s < ModulationSources::numShapes; ++s)
            box.shape.addItem (ModulationSources::shapeName (s), s + 1);
        addAndMakeVisible (box.shape);
        box.shapeAttachment = std::make_unique<ComboAttachment> (chain.parameters, "lfo" + juce::String (l + 1) + "shape", box.shape);

        const juce::String prefix = "lfo" + juce::String (l + 1);
        box.sync.setColour (juce::ToggleButton::tickColourId, accent());
        box.sync.setTooltip ("Follows the tempo of your song, locked to its bars while it plays");
        addAndMakeVisible (box.sync);
        box.syncAttachment = std::make_unique<ButtonAttachment> (chain.parameters, prefix + "sync", box.sync);

        for (int d = 0; d < ModulationSources::numDivisions; ++d)
            box.division.addItem (ModulationSources::divisionName (d), d + 1);
        box.division.setJustificationType (juce::Justification::centred);
        box.division.setTooltip ("One LFO cycle lasts this long");
        addChildComponent (box.division);
        box.divisionAttachment = std::make_unique<ComboAttachment> (chain.parameters, prefix + "division", box.division);

        box.tempo.setJustificationType (juce::Justification::centred);
        box.tempo.setFont (juce::FontOptions (12.0f));
        box.tempo.setColour (juce::Label::textColourId, dim);
        addChildComponent (box.tempo);
    }

    juce::StringArray sourceNames;
    for (int s = 0; s < ModulationSources::numSources; ++s)
        sourceNames.add (ModulationSources::sourceName (s));

    for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
    {
        auto& row = rows[(size_t) slot];

        row.number.setText (juce::String (slot + 1), juce::dontSendNotification);
        row.number.setJustificationType (juce::Justification::centred);
        row.number.setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (row.number);

        row.source.addItemList (sourceNames, 1);
        addAndMakeVisible (row.source);
        row.sourceAttachment = std::make_unique<ComboAttachment> (chain.parameters, ChainProcessor::slotParamId (slot, "source"), row.source);

        row.destination.onClick = [this, slot] { showDestinationMenu (slot); };
        row.destination.setTooltip ("The setting this line moves");
        addAndMakeVisible (row.destination);

        row.amount.setSliderStyle (juce::Slider::LinearBar);
        row.amount.setLookAndFeel (&bipolarLook);
        row.amount.setDoubleClickReturnValue (true, 0.0);
        row.amount.setColour (juce::Slider::trackColourId, accent().withAlpha (0.45f));
        row.amount.setTooltip ("How far the setting moves: 100 % = the whole travel of its knob. Negative = the other way.");
        addAndMakeVisible (row.amount);
        row.amountAttachment = std::make_unique<SliderAttachment> (chain.parameters, ChainProcessor::slotParamId (slot, "amount"), row.amount);

        // Après l'attachement (qui pose ses propres fonctions) : quantité en % de la course,
        // ou en crans pour un réglage par crans (« +7 st » = une quinte). On peut taper l'un ou l'autre.
        row.amount.textFromValueFunction = [this, slot] (double value)
        {
            if (const auto* d = steppedDestination (slot))
            {
                const double steps = value * 0.01 * d->numSteps;
                const bool whole = std::abs (steps - std::round (steps)) < 0.05;
                return (steps > 0.0 ? "+" : "") + (whole ? juce::String (juce::roundToInt (steps)) : juce::String (steps, 1))
                       + " " + (d->stepUnit.isNotEmpty() ? d->stepUnit : juce::String ("steps"));
            }
            return juce::String (juce::roundToInt (value)) + " %";
        };

        row.amount.valueFromTextFunction = [this, slot] (const juce::String& text)
        {
            const double number = text.retainCharacters ("-+0123456789.").getDoubleValue();
            if (const auto* d = steppedDestination (slot))
                return number * 100.0 / d->numSteps;
            return number;
        };
        row.amount.updateText();
    }

    setSize (preferredWidth, preferredHeight);
    timerCallback();
    startTimerHz (20);
}

ModulationPanel::~ModulationPanel()
{
    for (auto& row : rows)
        row.amount.setLookAndFeel (nullptr);
}

void ModulationPanel::BipolarLook::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                                    float, float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRect (area);

    const float centre = (float) slider.getPositionOfValue (0.0);
    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centre, sliderPos), area.getY(),
                                                            juce::jmax (centre, sliderPos), area.getBottom()));

    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawVerticalLine ((int) centre, area.getY(), area.getBottom());
}

void ModulationPanel::setupKnob (Knob& knob, const juce::String& paramId, const juce::String& name)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent());
    addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (chain.parameters, paramId, knob.slider);
}

const ChainProcessor::Destination* ModulationPanel::steppedDestination (int slot) const
{
    const auto& list = chain.getDestinations();
    const int d = chain.getSlotDestination (slot);
    return juce::isPositiveAndBelow (d, (int) list.size()) && list[(size_t) d].numSteps > 0 ? &list[(size_t) d] : nullptr;
}

void ModulationPanel::showDestinationMenu (int slot)
{
    const auto& list = chain.getDestinations();
    const int current = chain.getSlotDestination (slot);

    // Un sous-menu par effet
    juce::PopupMenu menu;
    constexpr int noneId = 100000;
    menu.addItem (noneId, "None", true, current < 0);
    menu.addSeparator();

    juce::PopupMenu sub;
    juce::String effectName;
    bool subHasCurrent = false;

    for (size_t i = 0; i <= list.size(); ++i)
    {
        if (i == list.size() || list[i].effectName != effectName)
        {
            if (effectName.isNotEmpty())
                menu.addSubMenu (effectName, sub, true, nullptr, subHasCurrent);

            if (i == list.size())
                break;

            sub = {};
            effectName = list[i].effectName;
            subHasCurrent = false;
        }

        sub.addItem ((int) i + 1, list[i].paramName + (list[i].numSteps > 0 ? juce::String::fromUTF8 (" (steps)") : juce::String()),
                     true, (int) i == current);
        subHasCurrent = subHasCurrent || (int) i == current;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rows[(size_t) slot].destination),
                        [safe = juce::Component::SafePointer<ModulationPanel> (this), slot] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        safe->chain.setSlotDestination (slot, result == noneId ? -1 : result - 1);
        safe->timerCallback();
    });
}

void ModulationPanel::timerCallback()
{
    const auto& list = chain.getDestinations();

    // LFO synchronisé : la division et le tempo remplacent le bouton Rate
    for (int l = 0; l < 2; ++l)
    {
        auto& box = boxes[(size_t) l];
        const bool synced = box.sync.getToggleState();
        box.division.setVisible (synced);
        box.tempo.setVisible (synced);
        const float bpm = chain.getTempo();
        const bool whole = std::abs (bpm - std::round (bpm)) < 0.05f;
        box.tempo.setText ((whole ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 1)) + " BPM", juce::dontSendNotification);
        for (auto* knob : box.knobs)
        {
            knob->slider.setVisible (! synced);
            knob->label.setVisible (! synced);
        }
    }

    for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
    {
        auto& row = rows[(size_t) slot];
        const int d = chain.getSlotDestination (slot);
        const auto text = juce::isPositiveAndBelow (d, (int) list.size())
                              ? list[(size_t) d].effectName + juce::String::fromUTF8 (" \xc2\xb7 ") + list[(size_t) d].paramName
                              : juce::String::fromUTF8 ("Choose a destination\xe2\x80\xa6");

        if (row.destination.getButtonText() != text)
        {
            row.destination.setButtonText (text);
            row.amount.updateText();   // % ou crans, selon la destination
        }

        row.destination.setColour (juce::TextButton::textColourOffId, d >= 0 ? juce::Colours::white : dim);

        const bool active = chain.isSlotActive (slot);
        row.amount.setAlpha (active ? 1.0f : 0.5f);
        row.number.setColour (juce::Label::textColourId, active ? accent() : dim);
    }

    repaint();
}

void ModulationPanel::drawMeter (juce::Graphics& g, juce::Rectangle<int> area, float value, bool bipolar, juce::Colour colour)
{
    const auto r = area.toFloat();
    g.setColour (juce::Colour (0xff141518));
    g.fillRoundedRectangle (r, 3.0f);

    value = juce::jlimit (-1.0f, 1.0f, value);
    juce::Rectangle<float> bar;
    if (bipolar)
    {
        const float centre = r.getCentreX();
        const float x = centre + value * r.getWidth() * 0.5f;
        bar = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centre, x), r.getY(), juce::jmax (centre, x), r.getBottom());
        g.setColour (dim.withAlpha (0.5f));
        g.drawVerticalLine ((int) centre, r.getY(), r.getBottom());
    }
    else
    {
        bar = r.withWidth (r.getWidth() * juce::jmax (0.0f, value));
    }

    g.setColour (colour);
    g.fillRoundedRectangle (bar.reduced (0.0f, 1.0f), 2.0f);
}

void ModulationPanel::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MODULATION", getLocalBounds().removeFromTop (40).withTrimmedLeft (15), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Sources move the settings of your pedals, like the H3000's modulation matrix",
                getLocalBounds().removeFromTop (40).withTrimmedLeft (160), juce::Justification::centredLeft);

    // Les sources
    for (auto& box : boxes)
    {
        g.setColour (boxColour);
        g.fillRoundedRectangle (box.bounds.toFloat(), 6.0f);
        g.setColour (accent());
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (box.title, box.bounds.withHeight (24).withTrimmedLeft (8), juce::Justification::centredLeft);

        drawMeter (g, box.meter, chain.getSourceValue (box.source), ModulationSources::isBipolar (box.source), accent());
    }

    // Pitch : la plage de notes suivie
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText ("Follows the note you play,\nfrom E2 (low) to E6 (high)",
                      boxes[4].bounds.withTrimmedTop (28).withTrimmedBottom (30).reduced (6, 0), juce::Justification::centred, 3);

    // La matrice
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    auto header = matrixHeader;
    header.removeFromLeft (30);
    g.drawText ("Source", header.removeFromLeft (120), juce::Justification::centredLeft);
    header.removeFromLeft (8);
    g.drawText ("Destination", header.removeFromLeft (240), juce::Justification::centredLeft);
    header.removeFromLeft (8);
    g.drawText ("Amount", header.removeFromLeft (180), juce::Justification::centredLeft);
    header.removeFromLeft (8);
    g.drawText ("Now", header, juce::Justification::centredLeft);

    for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
        drawMeter (g, rows[(size_t) slot].meter, chain.getSlotOutput (slot), true,
                   chain.isSlotActive (slot) ? accent() : dim.withAlpha (0.3f));
}

void ModulationPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 0);
    area.removeFromTop (40);

    // Sources : 5 cases côte à côte
    auto sourcesArea = area.removeFromTop (150);
    const int gap = 8;
    const int boxWidth = (sourcesArea.getWidth() - 4 * gap) / 5;

    for (auto& box : boxes)
    {
        box.bounds = sourcesArea.removeFromLeft (boxWidth);
        sourcesArea.removeFromLeft (gap);

        auto inner = box.bounds.reduced (6, 0);
        inner.removeFromTop (24);
        box.meter = inner.removeFromBottom (18).withTrimmedBottom (8);

        if (box.shape.getNumItems() > 0)
            box.shape.setBounds (inner.removeFromTop (22));

        if (box.shape.getNumItems() > 0)
        {
            // Sync à droite du titre ; division et tempo à la place du bouton Rate
            box.sync.setBounds (box.bounds.withHeight (24).removeFromRight (64));
            auto syncArea = inner.withTrimmedTop (6);
            box.division.setBounds (syncArea.removeFromTop (24));
            box.tempo.setBounds (syncArea.removeFromTop (20));
        }

        if (! box.knobs.empty())
        {
            const int w = inner.getWidth() / (int) box.knobs.size();
            for (auto* knob : box.knobs)
            {
                auto col = inner.removeFromLeft (w);
                knob->label.setBounds (col.removeFromTop (16));
                knob->slider.setBounds (col);
            }
        }
    }

    area.removeFromTop (12);
    matrixHeader = area.removeFromTop (18);

    for (auto& row : rows)
    {
        auto line = area.removeFromTop (30).reduced (0, 3);
        row.number.setBounds (line.removeFromLeft (30));
        row.source.setBounds (line.removeFromLeft (120));
        line.removeFromLeft (8);
        row.destination.setBounds (line.removeFromLeft (240));
        line.removeFromLeft (8);
        row.amount.setBounds (line.removeFromLeft (180));
        line.removeFromLeft (8);
        row.meter = line.reduced (0, 4);
    }
}
