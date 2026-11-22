// La baie de brassage de ChainEditor : les pédales posées librement, reliées par des câbles.
//
//   - on glisse une pédale (ou un mélangeur) par son nom pour la placer où l'on veut ;
//   - on tire un câble d'une prise OUT (à droite) vers une prise IN (à gauche) ;
//   - on reprend un câble par sa prise IN pour le rebrancher ailleurs (ou le lâcher dans le vide pour l'enlever) ;
//   - on tire une source de modulation (en bas : LFO, Envelope…) vers la prise MOD (au-dessus) d'une pédale ;
//   - clic droit sur un câble, ou clic puis Suppr : le retirer.
#include "ChainEditor.h"

namespace
{
    constexpr float jackRadius = 5.0f;
    constexpr float grabDistance = 12.0f;   // tolérance pour attraper une prise
    constexpr int pedalWidth = 84, mixerWidth = 84, ioWidth = 54, ioHeight = 28;
    constexpr int topReserve = 14, bottomReserve = 34;   // place des prises MOD, puis de la rangée des sources

    const juce::Colour jackRing (0xff9aa0ab);
    const juce::Colour modColour (0xffc080ff);
}

//==============================================================================
// Géométrie

juce::Rectangle<int> ChainEditor::nodeCell (int node) const
{
    using namespace ChainPatch;
    const int w = isEffect (node) ? pedalWidth : (isMixer (node) ? mixerWidth : ioWidth);
    const int h = (node == inputNode || node == outputNode) ? ioHeight : cellHeight;

    const auto area = diagram.reduced (diagramMargin + 14, 0).withTrimmedTop (topReserve + 4).withTrimmedBottom (bottomReserve);
    const auto pos = shownPatch.position[(size_t) node];
    const int cx = area.getX() + w / 2 + juce::roundToInt (pos.x * (float) (area.getWidth() - w));
    const int cy = area.getY() + h / 2 + juce::roundToInt (pos.y * (float) (area.getHeight() - h));
    return { cx - w / 2, cy - h / 2, w, h };
}

juce::Colour ChainEditor::nodeColour (int node) const
{
    if (ChainPatch::isEffect (node))
        return pedals[(size_t) node].colour;
    return ChainPatch::isMixer (node) ? MixerPanel::accent() : juce::Colours::white;
}

juce::String ChainEditor::nodeName (int node) const
{
    if (ChainPatch::isEffect (node))
        return pedals[(size_t) node].name;
    if (ChainPatch::isMixer (node))
        return "Mixer " + juce::String (node - ChainPatch::numEffects + 1);
    return node == ChainPatch::inputNode ? "Input" : "Output";
}

juce::Point<float> ChainEditor::inJack (int node, int port) const
{
    const auto cell = nodeCell (node).toFloat();
    if (node == ChainPatch::outputNode)
        return { cell.getX() - jackRadius - 2.0f, cell.getCentreY() };
    return { cell.getX() - jackRadius - 2.0f, cell.getY() + (port == 0 ? nameHeight * 0.5f : cellHeight - 9.0f) };
}

juce::Point<float> ChainEditor::outJack (int node) const
{
    const auto cell = nodeCell (node).toFloat();
    if (node == ChainPatch::inputNode)
        return { cell.getRight() + jackRadius + 2.0f, cell.getCentreY() };
    return { cell.getRight() + jackRadius + 2.0f, cell.getY() + (ChainPatch::isMixer (node) ? cell.getHeight() * 0.5f : nameHeight * 0.5f) };
}

juce::Point<float> ChainEditor::modJack (int node) const
{
    const auto cell = nodeCell (node).toFloat();
    return { cell.getCentreX(), cell.getY() - jackRadius - 3.0f };
}

juce::Point<float> ChainEditor::sourceJack (int source) const
{
    return { (float) diagram.getX() + 130.0f + 105.0f * (float) (source - 1), (float) diagram.getBottom() - 16.0f };
}

// Un câble qui pend un peu, comme un vrai cordon ; il fait une boucle quand il revient en arrière
juce::Path ChainEditor::cablePath (juce::Point<float> from, juce::Point<float> to) const
{
    const float reach = juce::jmax (40.0f, std::abs (to.x - from.x) * 0.5f);
    const float sag = 18.0f + 0.12f * from.getDistanceFrom (to);
    juce::Path p;
    p.startNewSubPath (from);
    p.cubicTo (from.x + reach, from.y + sag, to.x - reach, to.y + sag, to.x, to.y);
    return p;
}

namespace
{
    // Câble de modulation : monte de la rangée des sources et arrive par le dessus de la pédale
    juce::Path modPath (juce::Point<float> from, juce::Point<float> to)
    {
        juce::Path p;
        p.startNewSubPath (from);
        p.cubicTo (from.x, from.y - 60.0f, to.x, to.y - 70.0f, to.x, to.y);
        return p;
    }

    float distanceToPath (const juce::Path& path, juce::Point<float> p)
    {
        juce::Point<float> nearest;
        path.getNearestPoint (p, nearest);
        return p.getDistanceFrom (nearest);
    }
}

int ChainEditor::nodeForComponent (const juce::Component* c) const
{
    const int pedal = pedalForComponent (c);
    if (pedal >= 0)
        return pedal;
    for (int m = 0; m < ChainPatch::numMixers; ++m)
        if (mixers[(size_t) m].button.get() == c)
            return ChainPatch::mixerNode (m);
    return -1;
}

// La pédale (ou le mélangeur) que vise une ligne de la matrice ; -1 pour Balance, Spread… (sans prise dans la baie)
int ChainEditor::modNodeForSlot (int slot) const
{
    if (! chain.isSlotActive (slot))
        return -1;
    const auto& d = chain.getDestinations()[(size_t) chain.getSlotDestination (slot)];
    if (d.effect < ChainRouting::numEffects)
        return d.effect;
    if (d.key.startsWith ("branches/mix"))
        return ChainPatch::mixerNode (juce::jlimit (0, ChainPatch::numMixers - 1, d.key.substring (12, 13).getIntValue() - 1));
    return -1;
}

int ChainEditor::cableAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 7.0f;
    for (size_t i = 0; i < shownPatch.cables.size(); ++i)
    {
        const auto& c = shownPatch.cables[i];
        const float d = distanceToPath (cablePath (outJack (c.from), inJack (c.to, c.port)), p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = (int) i;
        }
    }
    return best;
}

int ChainEditor::modCableAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 7.0f;
    for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
    {
        const int node = modNodeForSlot (slot);
        if (node < 0)
            continue;
        const int source = (int) chain.parameters.getRawParameterValue (ChainProcessor::slotParamId (slot, "source"))->load();
        const float d = distanceToPath (modPath (sourceJack (source), modJack (node)), p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = slot;
        }
    }
    return best;
}

//==============================================================================
// Mode et mise en page

void ChainEditor::setPatchView (bool on)
{
    chain.setPatchMode (on);
    patchView = chain.isPatchMode();
    shownPatch = chain.getPatch();
    shown = chain.getRouting();
    selectedCable = hoverCable = hoverMod = -1;
    movingNode = -1;
    cableDrag = {};

    if (! patchView && selected == mixerSelection)
        select (0);

    setSize (getWidth(), headerHeight + stripHeight() + contentHeight);
    resized();
    repaint();
}

void ChainEditor::layoutPatch()
{
    const auto plan = ChainPatch::compile (shownPatch);

    for (int e = 0; e < ChainRouting::numEffects; ++e)
    {
        auto& pedal = pedals[(size_t) e];
        pedal.cell = nodeCell (e);
        pedal.selectButton->setBounds (pedal.cell.withHeight (nameHeight));
        pedal.onButton->setBounds (pedal.cell.withTrimmedTop (nameHeight).withSizeKeepingCentre (56, cellHeight - nameHeight));

        // Pas branchée de l'entrée à la sortie : elle ne s'entend pas
        const float alpha = plan.live[(size_t) e] ? 1.0f : 0.45f;
        pedal.selectButton->setAlpha (alpha);
        pedal.onButton->setAlpha (alpha);
    }

    for (int m = 0; m < ChainPatch::numMixers; ++m)
    {
        const int node = ChainPatch::mixerNode (m);
        mixers[(size_t) m].cell = nodeCell (node);
        mixers[(size_t) m].button->setBounds (mixers[(size_t) m].cell.withHeight (nameHeight));
        mixers[(size_t) m].button->setAlpha (plan.live[(size_t) node] ? 1.0f : 0.45f);
    }

    routingButton.setButtonText ("Routing: Patch Bay  " + juce::String::fromUTF8 ("\xe2\x96\xbe"));
}

void ChainEditor::commitPatch()
{
    chain.setPatch (shownPatch);
    shownPatch = chain.getPatch();
    if (! juce::isPositiveAndBelow (selectedCable, (int) shownPatch.cables.size()))
        selectedCable = -1;
    layoutPatch();
    repaint();
}

//==============================================================================
// Dessin

void ChainEditor::paintPatch (juce::Graphics& g)
{
    using namespace ChainPatch;
    const auto plan = compile (shownPatch);

    auto drawJack = [&g] (juce::Point<float> p, juce::Colour ring, bool highlight)
    {
        const auto r = juce::Rectangle<float> (jackRadius * 2.0f, jackRadius * 2.0f).withCentre (p);
        g.setColour (juce::Colour (0xff0c0d10));
        g.fillEllipse (r);
        g.setColour (highlight ? juce::Colours::white : ring);
        g.drawEllipse (r, highlight ? 2.0f : 1.5f);
    };

    // Compatibles avec le câble tiré : les prises qui s'allument
    const bool draggingAudio = cableDrag.active && ! cableDrag.mod;
    const bool draggingMod   = cableDrag.active && cableDrag.mod;

    // Entrée et sortie de la chaîne
    for (int node : { inputNode, outputNode })
    {
        const auto cell = nodeCell (node).toFloat();
        g.setColour (juce::Colour (0xff2a2d35));
        g.fillRoundedRectangle (cell, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (node == inputNode ? "IN" : "OUT", cell, juce::Justification::centred);
    }

    // Les mélangeurs : repères A et B à côté de leurs prises
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (int m = 0; m < numMixers; ++m)
    {
        const auto cell = nodeCell (mixerNode (m)).toFloat();
        g.setColour (juce::Colour (0xff23252c));
        g.fillRoundedRectangle (cell.withTrimmedTop ((float) nameHeight - 2.0f), 3.0f);
        g.setColour (MixerPanel::accent().withAlpha (0.7f));
        const auto jackA = inJack (mixerNode (m), 0), jackB = inJack (mixerNode (m), 1);
        g.drawText ("A", juce::Rectangle<float> (jackA.x - 20.0f, jackA.y - 6.0f, 10.0f, 12.0f), juce::Justification::centredRight);
        g.drawText ("B", juce::Rectangle<float> (jackB.x - 20.0f, jackB.y - 6.0f, 10.0f, 12.0f), juce::Justification::centredRight);
        g.drawText ("A + B", cell.withTrimmedTop ((float) nameHeight), juce::Justification::centred);
    }

    // Câbles audio : de la couleur de la pédale d'où ils partent ; en pointillé ceux qui reviennent en arrière (boucle)
    for (size_t i = 0; i < shownPatch.cables.size(); ++i)
    {
        const auto& c = shownPatch.cables[i];
        const auto path = cablePath (outJack (c.from), inJack (c.to, c.port));
        bool delayed = false;
        for (int k = 0; k < plan.numLinks; ++k)
        {
            const auto& link = plan.links[(size_t) k];
            delayed = delayed || (link.from == c.from && link.to == c.to && link.port == c.port && link.delayed);
        }

        const bool live = plan.live[c.from] && plan.live[c.to];
        const bool selectedOne = (int) i == selectedCable, hovered = (int) i == hoverCable;
        auto colour = nodeColour (c.from);
        if (! live)
            colour = colour.withMultipliedAlpha (0.4f);

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (path, juce::PathStrokeType (selectedOne ? 7.0f : 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (selectedOne)
        {
            g.setColour (juce::Colours::white);
            g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (hovered ? colour.brighter (0.4f) : colour);
        if (delayed)
        {
            juce::Path dashed;
            const float dashes[] = { 9.0f, 5.0f };
            juce::PathStrokeType (3.0f).createDashedStroke (dashed, path, dashes, 2);
            g.fillPath (dashed);
        }
        else
        {
            g.strokePath (path, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // Câbles de modulation
    for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
    {
        const int node = modNodeForSlot (slot);
        if (node < 0)
            continue;
        const int source = (int) chain.parameters.getRawParameterValue (ChainProcessor::slotParamId (slot, "source"))->load();
        const auto path = modPath (sourceJack (source), modJack (node));
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.strokePath (path, juce::PathStrokeType (4.0f));
        g.setColour (slot == hoverMod ? modColour.brighter (0.4f) : modColour.withAlpha (0.85f));
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Les prises
    for (int node = 0; node < numNodes; ++node)
    {
        for (int port = 0; port < numInputs (node); ++port)
            drawJack (inJack (node, port), jackRing, draggingAudio);
        if (hasOutput (node))
            drawJack (outJack (node), jackRing, false);
        if (isEffect (node) || isMixer (node))
            drawJack (modJack (node), modColour.withAlpha (0.7f), draggingMod);
    }

    // Les sources de modulation, en bas
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (modColour.withAlpha (0.8f));
    g.drawText ("MOD SOURCES", juce::Rectangle<float> ((float) diagram.getX() + 14.0f, (float) diagram.getBottom() - 24.0f, 100.0f, 16.0f),
                juce::Justification::centredLeft);
    for (int s = 1; s < ModulationSources::numSources; ++s)
    {
        const auto p = sourceJack (s);
        const float value = chain.getSourceValue (s);
        drawJack (p, modColour, false);
        g.setColour (modColour.withAlpha (0.35f + 0.6f * juce::jlimit (0.0f, 1.0f, std::abs (value))));
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (p));
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (ModulationSources::sourceName (s), juce::Rectangle<float> (p.x + 9.0f, p.y - 7.0f, 80.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    // Fine ligne de la couleur de ce qui est affiché en dessous
    g.setColour (selected == mixerSelection ? MixerPanel::accent()
                 : (juce::isPositiveAndBelow (selected, (int) pedals.size()) ? pedals[(size_t) selected].colour : ModulationPanel::accent()));
    g.fillRect (0, headerHeight + stripHeight() - 3, getWidth(), 3);
}

// Le câble que l'on tire, au-dessus des pédales
void ChainEditor::paintPatchOver (juce::Graphics& g)
{
    if (! cableDrag.active)
        return;

    if (cableDrag.mod)
    {
        g.setColour (modColour);
        g.strokePath (modPath (sourceJack (cableDrag.from), cableDrag.end), juce::PathStrokeType (2.5f));
    }
    else
    {
        g.setColour (nodeColour (cableDrag.from));
        g.strokePath (cablePath (outJack (cableDrag.from), cableDrag.end),
                      juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (cableDrag.end));
}

//==============================================================================
// Souris et clavier

bool ChainEditor::patchMouseDown (const juce::MouseEvent& e)
{
    using namespace ChainPatch;
    grabKeyboardFocus();

    const auto pos = e.getEventRelativeTo (this).position;

    // Sur le nom d'une pédale ou d'un mélangeur : menu (clic droit), ou début d'un déplacement
    const int node = nodeForComponent (e.eventComponent);
    if (node >= 0)
    {
        if (e.mods.isPopupMenu())
            showNodeMenu (node);
        else
            moveOffset = pos.toInt() - nodeCell (node).getCentre();
        return true;
    }

    if (e.eventComponent != this)
        return false;

    // Une prise OUT : un nouveau câble
    for (int n = 0; n < numNodes; ++n)
        if (hasOutput (n) && outJack (n).getDistanceFrom (pos) < grabDistance)
        {
            cableDrag = { true, false, n, pos };
            selectedCable = -1;
            repaint();
            return true;
        }

    // Une prise IN : on reprend le dernier câble branché dessus
    for (int n = 0; n < numNodes; ++n)
        for (int port = 0; port < numInputs (n); ++port)
            if (inJack (n, port).getDistanceFrom (pos) < grabDistance)
            {
                for (int i = (int) shownPatch.cables.size() - 1; i >= 0; --i)
                {
                    const auto c = shownPatch.cables[(size_t) i];
                    if (c.to == n && c.port == port)
                    {
                        shownPatch.cables.erase (shownPatch.cables.begin() + i);
                        cableDrag = { true, false, c.from, pos };
                        selectedCable = -1;
                        repaint();
                        return true;
                    }
                }
                return true;
            }

    // Une source de modulation
    for (int s = 1; s < ModulationSources::numSources; ++s)
        if (sourceJack (s).getDistanceFrom (pos) < grabDistance)
        {
            cableDrag = { true, true, s, pos };
            repaint();
            return true;
        }

    // Un câble : sélectionné (Suppr pour l'enlever), menu au clic droit
    if (const int c = cableAt (pos); c >= 0)
    {
        selectedCable = c;
        repaint();
        if (e.mods.isPopupMenu())
            showCableMenu (c);
        return true;
    }

    if (const int slot = modCableAt (pos); slot >= 0)
    {
        showModCableMenu (slot);
        return true;
    }

    selectedCable = -1;
    repaint();
    return true;
}

bool ChainEditor::patchMouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (this).position;

    if (cableDrag.active)
    {
        cableDrag.end = pos;
        repaint (diagram.expanded (40));
        return true;
    }

    // Déplacer une pédale ou un mélangeur
    const int node = nodeForComponent (e.eventComponent);
    if (node < 0 || e.mods.isPopupMenu() || (movingNode < 0 && e.getDistanceFromDragStart() < 6))
        return false;

    movingNode = node;
    const auto cell = nodeCell (node);
    const auto area = diagram.reduced (diagramMargin + 14, 0).withTrimmedTop (topReserve + 4).withTrimmedBottom (bottomReserve);
    const auto centre = pos.toInt() - moveOffset;
    auto& p = shownPatch.position[(size_t) node];
    p.x = juce::jlimit (0.0f, 1.0f, (float) (centre.x - area.getX() - cell.getWidth() / 2) / (float) juce::jmax (1, area.getWidth() - cell.getWidth()));
    p.y = juce::jlimit (0.0f, 1.0f, (float) (centre.y - area.getY() - cell.getHeight() / 2) / (float) juce::jmax (1, area.getHeight() - cell.getHeight()));
    layoutPatch();
    repaint (diagram.expanded (40));
    return true;
}

bool ChainEditor::patchMouseUp (const juce::MouseEvent& e)
{
    using namespace ChainPatch;

    if (movingNode >= 0)
    {
        movingNode = -1;
        commitPatch();
        return true;
    }

    if (! cableDrag.active)
        return false;

    const auto drag = cableDrag;
    cableDrag = {};
    const auto pos = e.getEventRelativeTo (this).position;

    if (drag.mod)
    {
        // Lâché sur la prise MOD d'une pédale (ou sur la pédale elle-même) : choisir le réglage
        for (int n = 0; n < outputNode; ++n)
            if (n != inputNode && (modJack (n).getDistanceFrom (pos) < grabDistance || nodeCell (n).contains (pos.toInt())))
            {
                showModTargetMenu (n, drag.from);
                break;
            }
        repaint();
        return true;
    }

    // Lâché sur une prise IN (ou sur une pédale : son entrée) : branché. Ailleurs : le câble est enlevé.
    int target = -1, port = 0;
    float best = grabDistance + 4.0f;
    for (int n = 0; n < numNodes; ++n)
        for (int p = 0; p < numInputs (n); ++p)
            if (const float d = inJack (n, p).getDistanceFrom (pos); d < best)
            {
                best = d;
                target = n;
                port = p;
            }

    if (target < 0)
        for (int n = 0; n < numNodes; ++n)
            if (numInputs (n) > 0 && nodeCell (n).contains (pos.toInt()))
            {
                target = n;
                port = (isMixer (n) && pos.y > (float) nodeCell (n).getCentreY()) ? 1 : 0;
            }

    if (target >= 0)
        shownPatch.connect (drag.from, target, port);

    commitPatch();
    return true;
}

void ChainEditor::mouseMove (const juce::MouseEvent& e)
{
    if (! patchView || e.eventComponent != this)
        return;

    const auto pos = e.position;
    const int cable = cableAt (pos);
    const int mod = cable < 0 ? modCableAt (pos) : -1;
    if (cable != hoverCable || mod != hoverMod)
    {
        hoverCable = cable;
        hoverMod = mod;
        repaint (diagram);
    }
}

bool ChainEditor::keyPressed (const juce::KeyPress& key)
{
    if (patchView && juce::isPositiveAndBelow (selectedCable, (int) shownPatch.cables.size())
        && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        shownPatch.cables.erase (shownPatch.cables.begin() + selectedCable);
        selectedCable = -1;
        commitPatch();
        return true;
    }
    return false;
}

//==============================================================================
// Menus

void ChainEditor::showCableMenu (int cable)
{
    const auto c = shownPatch.cables[(size_t) cable];
    juce::String title = nodeName (c.from) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + nodeName (c.to);
    if (ChainPatch::isMixer (c.to))
        title << (c.port == 0 ? " (A)" : " (B)");

    juce::PopupMenu menu;
    menu.addSectionHeader (title);
    menu.addItem (1, "Disconnect");

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<ChainEditor> (this), c] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        safe->shownPatch.disconnect (c);
        safe->selectedCable = -1;
        safe->commitPatch();
    });
}

void ChainEditor::showModCableMenu (int slot)
{
    const int source = (int) chain.parameters.getRawParameterValue (ChainProcessor::slotParamId (slot, "source"))->load();
    const float amount = chain.parameters.getRawParameterValue (ChainProcessor::slotParamId (slot, "amount"))->load();
    const auto& d = chain.getDestinations()[(size_t) chain.getSlotDestination (slot)];

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (ModulationSources::sourceName (source)) + juce::String::fromUTF8 (" \xe2\x86\x92 ")
                           + d.effectName + juce::String::fromUTF8 (" \xc2\xb7 ") + d.paramName);
    menu.addItem (-1, "Amount: " + juce::String (amount, 0) + " %", false, false);
    menu.addItem (1, "Edit in the modulation matrix");
    menu.addItem (2, "Disconnect");

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<ChainEditor> (this), slot] (int result)
    {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->select (-1);
        else if (result == 2)
        {
            safe->setParameter (ChainProcessor::slotParamId (slot, "source"), 0.0f);
            safe->chain.setSlotDestination (slot, -1);
            safe->repaint();
        }
    });
}

void ChainEditor::showModTargetMenu (int node, int source)
{
    const auto& list = chain.getDestinations();

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (ModulationSources::sourceName (source)) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + nodeName (node));
    const juce::String mixerKey = ChainPatch::isMixer (node) ? "branches/mix" + juce::String (node - ChainPatch::numEffects + 1) : juce::String();
    for (size_t i = 0; i < list.size(); ++i)
        if ((ChainPatch::isEffect (node) && list[i].effect == node) || (mixerKey.isNotEmpty() && list[i].key.startsWith (mixerKey)))
            menu.addItem ((int) i + 1, list[i].paramName);

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<ChainEditor> (this), source] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        // Une ligne libre de la matrice
        for (int slot = 0; slot < ChainProcessor::numSlots; ++slot)
            if (! safe->chain.isSlotActive (slot))
            {
                safe->setParameter (ChainProcessor::slotParamId (slot, "source"), (float) source);
                safe->chain.setSlotDestination (slot, result - 1);
                safe->repaint();
                return;
            }

        safe->showError ("All 8 modulation slots are in use: disconnect one first.");
    });
}

void ChainEditor::showNodeMenu (int node)
{
    int count = 0;
    for (auto& c : shownPatch.cables)
        count += (c.from == node || c.to == node) ? 1 : 0;

    juce::PopupMenu menu;
    menu.addSectionHeader (nodeName (node));
    menu.addItem (1, "Show its settings");
    menu.addItem (2, "Disconnect its cables (" + juce::String (count) + ")", count > 0);

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<ChainEditor> (this), node] (int result)
    {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->select (ChainPatch::isEffect (node) ? node : mixerSelection);
        else if (result == 2)
        {
            auto& cables = safe->shownPatch.cables;
            cables.erase (std::remove_if (cables.begin(), cables.end(),
                                          [node] (const ChainPatch::Cable& c) { return c.from == node || c.to == node; }),
                          cables.end());
            safe->selectedCable = -1;
            safe->commitPatch();
        }
    });
}
