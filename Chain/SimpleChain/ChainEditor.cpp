#include "ChainEditor.h"

namespace
{
    const juce::Colour background (0xff141518);
    const juce::Colour wire       (0xff8a8d96);   // les fils du schéma
    const juce::Colour colourA    (0xff70c0ff);
    const juce::Colour colourB    (0xffffa060);

    const char* laneName (int lane)
    {
        static const char* names[] = { "Before split", "Branch A", "Branch B", "After merge" };
        return names[lane & 3];
    }
}

ChainEditor::ChainEditor (ChainProcessor& p)
    : AudioProcessorEditor (&p), chain (p)
{
    addAndMakeVisible (inputBox);

    routingButton.setName ("routing");
    routingButton.setTooltip ("Ready-made routings. Drag the pedals to build your own.");
    routingButton.onClick = [this] { showRoutingMenu(); };
    addAndMakeVisible (routingButton);

    modulationButton.setButtonText ("Modulation");
    modulationButton.setTooltip ("The modulation matrix: LFOs, envelope, pitch and random moving your pedals' settings");
    modulationButton.setColour (juce::TextButton::buttonOnColourId, ModulationPanel::accent().darker (0.4f));
    modulationButton.onClick = [this] { select (-1); };
    addAndMakeVisible (modulationButton);

    modulationPanel = std::make_unique<ModulationPanel> (chain);
    addChildComponent (*modulationPanel);

    // Dans l'ordre de ChainRouting::Effect, mêmes couleurs que les interfaces des effets
    addPedal ("Filter",     juce::Colour (0xff90d040));
    addPedal ("Amp",        juce::Colour (0xffe06040));
    addPedal ("Harmonizer", juce::Colour (0xff40b0e0));
    addPedal ("Reverse",    juce::Colour (0xff50c878));
    addPedal ("Layered",    juce::Colour (0xffe0e050));
    addPedal ("Chorus",     juce::Colour (0xffb070e0));
    addPedal ("Delay",      juce::Colour (0xffe0a040));
    addPedal ("Band Delay", juce::Colour (0xff40c0a0));
    addPedal ("Ultra-Tap",  juce::Colour (0xffe05090));
    addPedal ("Reverb",     juce::Colour (0xff7090f0));

    setupBranchControl (balance,       "branchbalance", "Balance",  "0 %: only branch A, 100 %: only branch B");
    setupBranchControl (spread,        "branchspread",  "Spread",   "Sends branch A to the left and branch B to the right");
    setupBranchControl (branchWidth,   "branchwidth",   "B Width",  "Stereo width of branch B: 0 % mono, 200 % extra wide");
    setupBranchControl (crossFeedback, "crossfeedback", "Cross FB", "Cross feedback: feeds the output of branch B back into branch A");

    wetOnlyButton.setTooltip ("Wet/Dry/Wet, like Bob Bradshaw's racks: the pedals of branch B run side by side, 100 % wet, "
                              "next to the dry sound of branch A. At Balance 50 %, dry and wet are both at full level.");
    wetOnlyButton.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xffffa060));
    addAndMakeVisible (wetOnlyButton);
    wetOnlyAttachment = std::make_unique<ButtonAttachment> (chain.parameters, "wetonly", wetOnlyButton);

    // Assez de place pour la plus grande des interfaces
    int width = 0, height = 0;
    for (auto& pedal : pedals)
    {
        width  = juce::jmax (width, pedal.editor->getWidth());
        height = juce::jmax (height, pedal.editor->getHeight());
    }

    width  = juce::jmax (width, ModulationPanel::preferredWidth + 20);
    height = juce::jmax (height, ModulationPanel::preferredHeight);

    // …et pour le schéma le plus large : toutes les pédales en série, plus la colonne des branches vides
    const int columns = ChainRouting::numEffects + 1;
    width = juce::jmax (width, 2 * (10 + diagramMargin) + columns * minPedalWidth
                                 + (ChainRouting::numEffects - 1) * arrowWidth + 2 * splitWidth);

    shown = chain.getRouting();
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

void ChainEditor::addPedal (const juce::String& name, juce::Colour colour)
{
    const int index = (int) pedals.size();
    auto& effect = chain.effect (index);

    Pedal pedal;
    pedal.name   = name;
    pedal.colour = colour;

   #if JUCE_MAJOR_VERSION >= 9
    pedal.editor.reset (effect.createEditorAndMakeActive());   // nouveau nom dans JUCE 9
   #else
    pedal.editor.reset (effect.createEditorIfNeeded());
   #endif
    addChildComponent (*pedal.editor);

    pedal.selectButton = std::make_unique<juce::TextButton> (name);
    pedal.selectButton->setClickingTogglesState (false);
    pedal.selectButton->setLookAndFeel (&pedalLook);
    pedal.selectButton->setColour (juce::TextButton::buttonOnColourId, colour.darker (0.3f));
    pedal.selectButton->setTooltip ("Click: show its settings. Drag: move it. Right-click: routing menu.");
    pedal.selectButton->onClick = [this, index] { select (index); };
    pedal.selectButton->addMouseListener (this, false);   // glisser-déposer et clic droit
    addAndMakeVisible (*pedal.selectButton);

    pedal.onButton = std::make_unique<juce::ToggleButton> ("On");
    pedal.onButton->setColour (juce::ToggleButton::tickColourId, colour);
    addAndMakeVisible (*pedal.onButton);
    pedal.onAttachment = std::make_unique<ButtonAttachment> (chain.parameters, ChainProcessor::onParamId (index), *pedal.onButton);

    pedals.push_back (std::move (pedal));
}

void ChainEditor::setupBranchControl (BranchControl& control, const juce::String& paramId,
                                      const juce::String& name, const juce::String& tooltip)
{
    control.label.setText (name, juce::dontSendNotification);
    control.label.setJustificationType (juce::Justification::centredRight);
    control.label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (control.label);

    control.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    control.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
    control.slider.setColour (juce::Slider::trackColourId, juce::Colours::white.withAlpha (0.7f));
    control.slider.setTooltip (tooltip);
    control.slider.setTextValueSuffix (" %");
    addAndMakeVisible (control.slider);

    control.attachment = std::make_unique<SliderAttachment> (chain.parameters, paramId, control.slider);
    control.slider.setNumDecimalPlacesToDisplay (0);
}

void ChainEditor::select (int index)
{
    selected = index;

    for (int i = 0; i < (int) pedals.size(); ++i)
    {
        pedals[(size_t) i].editor->setVisible (i == index);
        pedals[(size_t) i].selectButton->setToggleState (i == index, juce::dontSendNotification);
    }

    modulationPanel->setVisible (index < 0);
    modulationButton.setToggleState (index < 0, juce::dontSendNotification);
    repaint();
}

//==============================================================================
// Routage

void ChainEditor::applyRouting (const ChainRouting::Routing& r)
{
    chain.setRouting (r);
    shown = chain.getRouting();
    layoutDiagram();
    repaint();
}

juce::String ChainEditor::routingName() const
{
    for (auto& t : ChainRouting::templates())
        if (t.routing == shown)
            return t.name;
    return "Custom";
}

void ChainEditor::showRoutingMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Ready-made routings");

    const auto& list = ChainRouting::templates();
    for (size_t i = 0; i < list.size(); ++i)
        menu.addItem ((int) i + 1, list[i].name, true, list[i].routing == shown);

    menu.addSeparator();
    menu.addItem (-1, "Tip: drag a pedal to move it, right-click it for options", false, false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (routingButton),
                        [safe = juce::Component::SafePointer<ChainEditor> (this)] (int result)
    {
        const auto& templates = ChainRouting::templates();
        if (safe == nullptr || result <= 0 || result > (int) templates.size())
            return;

        // Wet/Dry/Wet : effets de B à 100 % et son sec au centre. Les autres modèles mélangent normalement.
        const auto& t = templates[(size_t) result - 1];
        const bool wdw = juce::String (t.name) == "Wet/Dry/Wet";
        safe->setParameter ("wetonly", wdw ? 1.0f : 0.0f);
        if (wdw)
            safe->setParameter ("branchspread", 0.0f);

        safe->applyRouting (t.routing);
    });
}

void ChainEditor::setParameter (const juce::String& id, float value)
{
    if (auto* param = chain.parameters.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void ChainEditor::showPedalMenu (int effect)
{
    const int lane  = shown.laneOf (effect);
    const int index = shown.indexInLane (effect);
    const int count = (int) shown.effectsIn (lane).size();

    juce::PopupMenu menu;
    menu.addSectionHeader (pedals[(size_t) effect].name);
    for (int l = 0; l < ChainRouting::numLanes; ++l)
        menu.addItem (l + 1, laneName (l), l != lane, l == lane);

    menu.addSeparator();
    menu.addItem (10, "Move left",  index > 0);
    menu.addItem (11, "Move right", index + 1 < count);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (*pedals[(size_t) effect].selectButton),
                        [safe = juce::Component::SafePointer<ChainEditor> (this), effect, lane, index] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto r = safe->shown;
        if (result >= 1 && result <= ChainRouting::numLanes)
            r.move (effect, result - 1, 99);   // à la fin de la section choisie
        else if (result == 10)
            r.move (effect, lane, index - 1);
        else if (result == 11)
            r.move (effect, lane, index + 1);

        safe->applyRouting (r);
    });
}

//==============================================================================
// Glisser-déposer des pédales

int ChainEditor::pedalForComponent (const juce::Component* c) const
{
    for (size_t i = 0; i < pedals.size(); ++i)
        if (pedals[i].selectButton.get() == c)
            return (int) i;
    return -1;
}

void ChainEditor::mouseDown (const juce::MouseEvent& e)
{
    const int effect = pedalForComponent (e.eventComponent);
    if (effect >= 0 && e.mods.isPopupMenu())
        showPedalMenu (effect);
}

void ChainEditor::mouseDrag (const juce::MouseEvent& e)
{
    const int effect = pedalForComponent (e.eventComponent);
    if (effect < 0 || e.mods.isPopupMenu())
        return;

    if (dragging < 0 && e.getDistanceFromDragStart() < 6)
        return;

    dragging = effect;
    dragPos  = e.getEventRelativeTo (this).getPosition();
    drop     = findDrop (dragPos);
    repaint (diagram.expanded (40));
}

void ChainEditor::mouseUp (const juce::MouseEvent& e)
{
    if (dragging < 0 || pedalForComponent (e.eventComponent) < 0)
        return;

    const int effect = dragging;
    dragging = -1;

    if (diagram.expanded (20).contains (e.getEventRelativeTo (this).getPosition()))
    {
        auto r = shown;
        r.move (effect, drop.lane, drop.index);
        applyRouting (r);
    }

    repaint();
}

int ChainEditor::rowY (int lane) const
{
    const int top = diagram.getY() + 8;
    const int row = lane == ChainRouting::branchA ? 0 : (lane == ChainRouting::branchB ? 2 : 1);
    return top + row * (cellHeight + rowGap) + nameHeight / 2;
}

ChainEditor::Drop ChainEditor::findDrop (juce::Point<int> p) const
{
    // La rangée la plus proche : A en haut, la série au milieu, B en bas
    Drop d;
    const int distA = std::abs (p.y - rowY (ChainRouting::branchA));
    const int distM = std::abs (p.y - rowY (ChainRouting::pre));
    const int distB = std::abs (p.y - rowY (ChainRouting::branchB));

    if (distA < distM && distA <= distB)
        d.lane = ChainRouting::branchA;
    else if (distB < distM)
        d.lane = ChainRouting::branchB;
    else
        d.lane = p.x < (splitX + mergeX) / 2 ? ChainRouting::pre : ChainRouting::post;

    // La place : après chaque pédale de la section dont le centre est à gauche du pointeur
    d.index = 0;
    for (auto e : shown.effectsIn (d.lane))
        if (e != dragging && pedals[(size_t) e].cell.getCentreX() < p.x)
            ++d.index;

    return d;
}

float ChainEditor::markerX (const Drop& d) const
{
    std::vector<int> list;
    for (auto e : shown.effectsIn (d.lane))
        if (e != dragging)
            list.push_back (e);

    if (d.index < (int) list.size())
        return (float) pedals[(size_t) list[(size_t) d.index]].cell.getX() - arrowWidth * 0.5f;

    if (! list.empty())
        return (float) pedals[(size_t) list.back()].cell.getRight() + arrowWidth * 0.5f;

    switch (d.lane)
    {
        case ChainRouting::branchA: return (float) emptyA.getCentreX();
        case ChainRouting::branchB: return (float) emptyB.getCentreX();
        case ChainRouting::pre:     return (float) splitX - 6.0f;
        default:                    return (float) mergeX + 6.0f;
    }
}

//==============================================================================

void ChainEditor::timerCallback()
{
    for (int i = 0; i < (int) pedals.size(); ++i)
        pedals[(size_t) i].editor->setAlpha (chain.isEffectOn (i) ? 1.0f : 0.35f);

    // Routage changé ailleurs (session rechargée, automation…)
    const auto current = chain.getRouting();
    if (current != shown && dragging < 0)
    {
        shown = current;
        layoutDiagram();
    }

    const float branchAlpha = shown.hasParallel() ? 1.0f : 0.4f;
    for (auto* control : { &balance, &spread, &branchWidth, &crossFeedback })
    {
        control->slider.setAlpha (branchAlpha);
        control->label.setAlpha (branchAlpha);
    }
    wetOnlyButton.setAlpha (branchAlpha);

    repaint (0, headerHeight, getWidth(), stripHeight);
}

void ChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("SIMPLE CHAIN", getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    // Fond du schéma
    g.setColour (juce::Colour (0xff1b1d22));
    g.fillRoundedRectangle (diagram.toFloat(), 6.0f);

    const bool parallel = shown.hasParallel();
    const float yA = (float) rowY (ChainRouting::branchA);
    const float yM = (float) rowY (ChainRouting::pre);
    const float yB = (float) rowY (ChainRouting::branchB);

    auto arrow = [&g] (float x1, float y, float x2, juce::Colour c)
    {
        g.setColour (c);
        if (x2 - x1 > 8.0f)
            g.drawArrow ({ x1, y, x2, y }, 2.0f, 7.0f, 6.0f);
        else
            g.drawLine (x1, y, x2, y, 2.0f);
    };

    auto line = [&g] (float x1, float y1, float x2, float y2, juce::Colour c)
    {
        g.setColour (c);
        g.drawLine (x1, y1, x2, y2, 2.0f);
    };

    const float left  = (float) diagram.getX() + 6.0f;
    const float right = (float) diagram.getRight() - 6.0f;

    // Une section : flèches entre ses pédales, et entrée / sortie aux deux bouts
    auto drawLane = [&] (int lane, float y, float startX, float endX, juce::Colour c)
    {
        const auto list = shown.effectsIn (lane);
        if (list.empty())
            return;

        arrow (startX, y, (float) pedals[(size_t) list.front()].cell.getX() - 2.0f, c);
        for (size_t i = 0; i + 1 < list.size(); ++i)
            arrow ((float) pedals[(size_t) list[i]].cell.getRight() + 2.0f, y, (float) pedals[(size_t) list[i + 1]].cell.getX() - 2.0f, c);
        const float lastX = (float) pedals[(size_t) list.back()].cell.getRight() + 2.0f;
        if (endX >= right - 1.0f)
            arrow (lastX, y, endX, c);   // vers la sortie
        else
            line (lastX, y, endX, y, c);
    };

    // Avant la séparation et après la fusion
    const auto preList  = shown.effectsIn (ChainRouting::pre);
    const auto postList = shown.effectsIn (ChainRouting::post);
    drawLane (ChainRouting::pre, yM, left, (float) splitX, wire);

    if (postList.empty())
        arrow ((float) mergeX, yM, right, wire);
    else
        drawLane (ChainRouting::post, yM, (float) mergeX, right, wire);

    // Petits repères IN / OUT
    g.setColour (wire);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("IN",  juce::Rectangle<float> (left, yM - 30.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("OUT", juce::Rectangle<float> (right - 30.0f, yM - 30.0f, 30.0f, 12.0f), juce::Justification::centredRight);

    if (preList.empty())
        line (left, yM, (float) splitX, yM, wire);

    // Les branches
    if (parallel)
    {
        line ((float) splitX, yA, (float) splitX, yB, wire);
        line ((float) mergeX, yA, (float) mergeX, yB, wire);

        for (auto [lane, y, empty, colour] : { std::tuple { (int) ChainRouting::branchA, yA, emptyA, colourA },
                                               std::tuple { (int) ChainRouting::branchB, yB, emptyB, colourB } })
        {
            if (shown.effectsIn (lane).empty())
            {
                // Branche vide : le son passe sec
                arrow ((float) splitX, y, (float) mergeX - 2.0f, colour.withAlpha (0.8f));
                g.setColour (colour.withAlpha (0.8f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ("dry", empty.withY ((int) y - 18).withHeight (14), juce::Justification::centred);
            }
            else
            {
                drawLane (lane, y, (float) splitX, (float) mergeX, colour);
            }

            g.setColour (colour);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (lane == ChainRouting::branchA ? "A" : "B",
                        juce::Rectangle<float> ((float) splitX + 3.0f, y - 18.0f, 14.0f, 14.0f), juce::Justification::centred);
        }

        // Wet Only : les pédales de B ne s'enchaînent plus, elles tournent côte à côte à 100 %
        if (wetOnlyButton.getToggleState())
        {
            g.setColour (juce::Colour (0xffffa060));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 ("B: pedals side by side, 100 % wet"),
                        juce::Rectangle<float> ((float) splitX + 20.0f, yM - 9.0f, (float) (mergeX - splitX) - 40.0f, 18.0f),
                        juce::Justification::centred);
        }
    }
    else
    {
        // Pas de branche : le fil passe tout droit, et deux cases attendent des pédales
        line ((float) splitX, yM, (float) mergeX, yM, wire);

        for (auto [box, colour, text] : { std::tuple { emptyA, colourA, "Branch A" }, std::tuple { emptyB, colourB, "Branch B" } })
        {
            juce::Path outline;
            outline.addRoundedRectangle (box.toFloat().reduced (1.0f), 4.0f);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (colour.withAlpha (0.45f));
            g.fillPath (dashed);
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (juce::String (text) + "\ndrop here", box.reduced (2), juce::Justification::centred, 2);
        }
    }

    // Un point violet sur les pédales modulées
    std::array<bool, ChainRouting::numEffects + 1> modulated {};
    const auto& destinations = chain.getDestinations();
    for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
        if (chain.isSlotActive (slot))
            modulated[(size_t) destinations[(size_t) chain.getSlotDestination (slot)].effect] = true;

    g.setColour (ModulationPanel::accent());
    for (int e = 0; e < (int) pedals.size(); ++e)
        if (modulated[(size_t) e])
        {
            const auto b = pedals[(size_t) e].cell;
            g.fillEllipse ((float) b.getRight() - 7.0f, (float) b.getY() - 3.0f, 8.0f, 8.0f);
        }

    if (modulated[ChainRouting::numEffects] && parallel)
        g.fillEllipse ((float) mergeX - 4.0f, (yA + yB) * 0.5f - 4.0f, 8.0f, 8.0f);   // Balance / Spread / Cross Feedback

    // Fine ligne de la couleur de la pédale affichée (ou de la matrice), au-dessus de ses réglages
    g.setColour (juce::isPositiveAndBelow (selected, (int) pedals.size()) ? pedals[(size_t) selected].colour
                                                                            : ModulationPanel::accent());
    g.fillRect (0, headerHeight + stripHeight - 3, getWidth(), 3);
}

void ChainEditor::paintOverChildren (juce::Graphics& g)
{
    if (dragging < 0)
        return;

    const auto& pedal = pedals[(size_t) dragging];

    // Où la pédale va se poser
    const float x = markerX (drop);
    const float y = (float) rowY (drop.lane);
    g.setColour (pedal.colour);
    g.fillRoundedRectangle (x - 2.0f, y - cellHeight * 0.5f, 4.0f, (float) cellHeight - 4.0f, 2.0f);

    g.setFont (juce::FontOptions (11.0f));
    g.drawText (laneName (drop.lane), juce::Rectangle<float> (x - 50.0f, y + cellHeight * 0.5f - 2.0f, 100.0f, 13.0f),
                juce::Justification::centred);

    // La pédale qui suit la souris
    const auto ghost = pedal.cell.withHeight (nameHeight).withCentre (dragPos).toFloat();
    g.setColour (pedal.colour.withAlpha (0.85f));
    g.fillRoundedRectangle (ghost, 4.0f);
    g.setColour (juce::Colours::black);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (pedal.name, ghost, juce::Justification::centred);
}

//==============================================================================
// Mise en page

void ChainEditor::layoutDiagram()
{
    using namespace ChainRouting;

    const auto preList  = shown.effectsIn (pre);
    const auto aList    = shown.effectsIn (branchA);
    const auto bList    = shown.effectsIn (branchB);
    const auto postList = shown.effectsIn (post);

    const int nPre = (int) preList.size(), nPost = (int) postList.size();
    const int nPar = juce::jmax ((int) aList.size(), (int) bList.size(), 1);   // une case au moins, pour y glisser des pédales

    // Largeur des pédales : tout le schéma tient, sans devenir démesuré quand il y a peu de colonnes
    const auto area = diagram.reduced (diagramMargin, 8);
    const int columns = nPre + nPar + nPost;
    const int gaps = juce::jmax (0, nPre - 1) + juce::jmax (0, nPar - 1) + juce::jmax (0, nPost - 1);
    const int width = juce::jlimit (minPedalWidth, maxPedalWidth,
                                    (area.getWidth() - gaps * arrowWidth - 2 * splitWidth) / juce::jmax (1, columns));

    const int used = columns * width + gaps * arrowWidth + 2 * splitWidth;
    int x = area.getX() + (area.getWidth() - used) / 2;

    auto place = [this, width] (int effect, int cellX, int lane)
    {
        auto& pedal = pedals[(size_t) effect];
        pedal.cell = { cellX, rowY (lane) - nameHeight / 2, width, cellHeight };
        pedal.selectButton->setBounds (pedal.cell.withHeight (nameHeight));
        pedal.onButton->setBounds (pedal.cell.withTrimmedTop (nameHeight).withSizeKeepingCentre (56, cellHeight - nameHeight));
    };

    for (int i = 0; i < nPre; ++i)
    {
        place (preList[(size_t) i], x, pre);
        x += width + (i + 1 < nPre ? arrowWidth : 0);
    }

    splitX = x + splitWidth / 2;
    x += splitWidth;
    const int parallelX = x;

    for (int i = 0; i < (int) aList.size(); ++i)
        place (aList[(size_t) i], parallelX + i * (width + arrowWidth), branchA);
    for (int i = 0; i < (int) bList.size(); ++i)
        place (bList[(size_t) i], parallelX + i * (width + arrowWidth), branchB);

    // Branche vide : une case sur toute la largeur des branches
    const int parallelWidth = nPar * width + (nPar - 1) * arrowWidth;
    emptyA = { parallelX, rowY (branchA) - nameHeight / 2, parallelWidth, cellHeight - 6 };
    emptyB = { parallelX, rowY (branchB) - nameHeight / 2, parallelWidth, cellHeight - 6 };

    x = parallelX + parallelWidth;
    mergeX = x + splitWidth / 2;
    x += splitWidth;

    for (int i = 0; i < nPost; ++i)
    {
        place (postList[(size_t) i], x, post);
        x += width + arrowWidth;
    }

    routingButton.setButtonText ("Routing: " + routingName() + "  " + juce::String::fromUTF8 ("\xe2\x96\xbe"));
}

void ChainEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight);
    inputBox.setBounds (header.removeFromRight (InputMode::Box::preferredWidth + 15).withTrimmedRight (15).reduced (0, 5));
    header.removeFromLeft (170);   // le titre
    routingButton.setBounds (header.removeFromLeft (230).reduced (0, 7));
    header.removeFromLeft (8);
    modulationButton.setBounds (header.removeFromLeft (110).reduced (0, 7));

    auto strip = getLocalBounds().withTrimmedTop (headerHeight).removeFromTop (stripHeight - 3);
    diagram = strip.removeFromTop (diagramHeight).reduced (10, 0);
    layoutDiagram();

    // Wet Only, puis Balance, Spread, B Width, Cross Feedback
    auto controls = strip.reduced (10, 4);
    wetOnlyButton.setBounds (controls.removeFromLeft (110));
    const int w = controls.getWidth() / 4;
    for (auto* control : { &balance, &spread, &branchWidth, &crossFeedback })
    {
        auto col = controls.removeFromLeft (w).reduced (3, 0);
        control->label.setBounds (col.removeFromLeft (62));
        control->slider.setBounds (col);
    }

    // Réglages de la pédale affichée
    for (auto& pedal : pedals)
        pedal.editor->setTopLeftPosition ((getWidth() - pedal.editor->getWidth()) / 2, headerHeight + stripHeight);
    modulationPanel->setTopLeftPosition ((getWidth() - modulationPanel->getWidth()) / 2, headerHeight + stripHeight);
}
