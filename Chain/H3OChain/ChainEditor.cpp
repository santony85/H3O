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

    // Preset | Patch : passer de la vue à branches (celle des presets) à la baie de brassage, et retour
    presetViewButton.setTooltip ("Preset view: the routing of the preset, in branches A and B");
    patchViewButton.setTooltip ("Patch view: the same chain as free cables, to patch as you like");
    presetViewButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    patchViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    for (auto* b : { &presetViewButton, &patchViewButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a6a8a));
        addAndMakeVisible (*b);
    }
    presetViewButton.onClick = [this] { if (patchView) setPatchView (false); };
    patchViewButton.onClick = [this] { if (! patchView) setPatchView (true); };

    modulationButton.setButtonText ("Modulation");
    modulationButton.setTooltip ("The modulation matrix: LFOs, envelope, pitch and random moving your pedals' settings");
    modulationButton.setColour (juce::TextButton::buttonOnColourId, ModulationPanel::accent().darker (0.4f));
    modulationButton.onClick = [this] { select (-1); };
    addAndMakeVisible (modulationButton);

    for (auto* b : { &presetButton, &previousPreset, &nextPreset })
        b->setName ("preset");
    presetButton.setTooltip ("Your presets: load, save, delete");
    presetButton.onClick = [this] { showPresetMenu(); };
    previousPreset.setTooltip ("Previous preset");
    nextPreset.setTooltip ("Next preset");
    previousPreset.onClick = [this] { loadPreset (chain.presets.neighbour (-1)); };
    nextPreset.onClick     = [this] { loadPreset (chain.presets.neighbour (1)); };
    for (auto* b : { &previousPreset, &presetButton, &nextPreset })
        addAndMakeVisible (b);
    previousPreset.setLookAndFeel (&pedalLook);   // police plus petite : les flèches tiennent
    nextPreset.setLookAndFeel (&pedalLook);

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
    addPedal ("Compressor", juce::Colour (0xffc8d0e0));
    addPedal ("Tremolo",    juce::Colour (0xffc89060));
    addPedal ("Phaser",     juce::Colour (0xfff08030));

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

    width = juce::jmax (width, editorWidth);   // 1440 px : le schéma et l'en-tête respirent

    // La baie de brassage : 4 mélangeurs, leur panneau, et l'aide en bas du schéma
    mixerPanel = std::make_unique<MixerPanel> (chain);
    addChildComponent (*mixerPanel);
    for (int m = 0; m < ChainPatch::numMixers; ++m)
    {
        auto& cell = mixers[(size_t) m];
        cell.button = std::make_unique<juce::TextButton> ("Mixer " + juce::String (m + 1));
        cell.button->setLookAndFeel (&pedalLook);
        cell.button->setColour (juce::TextButton::buttonOnColourId, MixerPanel::accent().darker (0.5f));
        cell.button->setTooltip ("Two inputs (A and B) with level and pan. Click: its settings. Drag: move it.");
        cell.button->onClick = [this] { select (mixerSelection); };
        cell.button->addMouseListener (this, false);
        addChildComponent (*cell.button);
    }
    patchHint.setText (juce::String::fromUTF8 ("Drag from an OUT jack to an IN jack to patch \xc2\xb7 grab a cable by its IN jack to move it \xc2\xb7 "
                                               "drag a LFO / ENV jack to a MOD jack \xc2\xb7 right-click a cable or press Delete to remove it"),
                       juce::dontSendNotification);
    patchHint.setFont (juce::FontOptions (12.0f));
    patchHint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    patchHint.setJustificationType (juce::Justification::centred);
    addChildComponent (patchHint);
    setWantsKeyboardFocus (true);

    height = juce::jmax (height, mixerPanel->getHeight());
    contentHeight = height;
    shown = chain.getRouting();
    shownPatch = chain.getPatch();
    patchView = chain.isPatchMode();
    setSize (width, headerHeight + stripHeight() + height);
    select (0);

    timerCallback();
    startTimerHz (10);
}

ChainEditor::~ChainEditor()
{
    previousPreset.setLookAndFeel (nullptr);
    nextPreset.setLookAndFeel (nullptr);
    for (auto& m : mixers)
        m.button->setLookAndFeel (nullptr);

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

    modulationPanel->setVisible (index == -1);
    modulationButton.setToggleState (index == -1, juce::dontSendNotification);
    mixerPanel->setVisible (index == mixerSelection);
    for (auto& m : mixers)
        m.button->setToggleState (index == mixerSelection, juce::dontSendNotification);
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
        menu.addItem ((int) i + 1, list[i].name, true, ! patchView && list[i].routing == shown);

    menu.addSeparator();
    menu.addSectionHeader ("Patch Bay");
    menu.addItem (1001, juce::String::fromUTF8 ("Patch Bay \xe2\x80\x93 free cables"), true, patchView);
    menu.addItem (1002, "Rebuild cables from the branches", patchView);
    menu.addItem (1003, "Clear all cables", patchView && ! shownPatch.cables.empty());

    menu.addSeparator();
    menu.addItem (-1, patchView ? "Tip: drag a pedal to move it, drag between jacks to patch"
                                : "Tip: drag a pedal to move it, right-click it for options", false, false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (routingButton),
                        [safe = juce::Component::SafePointer<ChainEditor> (this)] (int result)
    {
        const auto& templates = ChainRouting::templates();
        if (safe == nullptr || result <= 0)
            return;

        if (result == 1001)
        {
            safe->setPatchView (! safe->patchView);
            return;
        }
        if (result == 1002)
        {
            safe->chain.convertRoutingToPatch();
            safe->shownPatch = safe->chain.getPatch();
            safe->selectedCable = -1;
            safe->layoutPatch();
            safe->repaint();
            return;
        }
        if (result == 1003)
        {
            safe->shownPatch.cables.clear();
            safe->selectedCable = -1;
            safe->commitPatch();
            return;
        }
        if (result > (int) templates.size())
            return;

        // Choisir un routage à branches quitte la baie de brassage
        if (safe->patchView)
            safe->setPatchView (false);

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

//==============================================================================
// Presets

void ChainEditor::showPresetMenu()
{
    const auto files = chain.presets.list();
    const auto current = chain.presets.current();
    const bool hasCurrent = current.existsAsFile();
    const auto shortName = presetLeafName();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Presets");
    if (files.isEmpty())
        menu.addItem (-1, "No presets yet: use Save As", false, false);

    // Un sous-menu par sous-dossier (« H3000 Inspired › Reverse › … »)
    struct Folder
    {
        juce::String name;
        juce::PopupMenu menu;
        std::vector<std::unique_ptr<Folder>> children;
        bool ticked = false;

        Folder& child (const juce::String& n)
        {
            for (auto& c : children)
                if (c->name == n)
                    return *c;
            children.push_back (std::make_unique<Folder>());
            children.back()->name = n;
            return *children.back();
        }

        void addTo (juce::PopupMenu& parent)
        {
            for (auto& c : children)
                c->addTo (menu);
            parent.addSubMenu (name, menu, true, nullptr, ticked);
        }
    } root;

    for (int i = 0; i < files.size(); ++i)
    {
        const auto parts = juce::StringArray::fromTokens (chain.presets.nameOf (files[i]), "/", {});
        const bool ticked = files[i] == current;
        if (parts.size() <= 1)
        {
            menu.addItem (i + 1, files[i].getFileNameWithoutExtension(), true, ticked);
            continue;
        }

        auto* folder = &root;
        for (int p = 0; p < parts.size() - 1; ++p)
        {
            folder = &folder->child (parts[p]);
            folder->ticked = folder->ticked || ticked;
        }
        folder->menu.addItem (i + 1, parts[parts.size() - 1], true, ticked);
    }

    // Les sous-dossiers après les presets de la racine
    for (auto& c : root.children)
        c->addTo (menu);

    menu.addSeparator();
    menu.addItem (10001, hasCurrent ? "Save \"" + shortName + "\"" : juce::String ("Save"), hasCurrent);
    menu.addItem (10002, juce::String::fromUTF8 ("Save As\xe2\x80\xa6"));
    menu.addItem (10003, hasCurrent ? juce::String::fromUTF8 ("Delete \"") + shortName + juce::String::fromUTF8 ("\"\xe2\x80\xa6")
                                    : juce::String ("Delete"), hasCurrent);
    menu.addSeparator();
    menu.addItem (10004, "Show presets folder in Finder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [safe = juce::Component::SafePointer<ChainEditor> (this), files] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result > 0 && result <= files.size())
            safe->loadPreset (files[result - 1]);
        else if (result == 10001)
            safe->saveCurrentPreset();
        else if (result == 10002)
            safe->savePresetAs();
        else if (result == 10003)
            safe->deletePreset();
        else if (result == 10004)
        {
            const auto folder = safe->chain.presets.getFolder();
            folder.createDirectory();
            folder.startAsProcess();
        }
    });
}

void ChainEditor::loadPreset (const juce::File& file)
{
    if (file == juce::File())
        return showError ("No presets yet: use Save As in the preset menu.");

    if (const auto error = chain.presets.load (file); error.isNotEmpty())
        return showError (error);

    timerCallback();   // schéma, routage et modulation à jour tout de suite
}

void ChainEditor::savePresetAs()
{
    presetDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon, this);
    presetDialog->addTextEditor ("name", presetLeafName());
    presetDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<ChainEditor> (this)] (int result)
        {
            if (safe == nullptr || safe->presetDialog == nullptr)
                return;

            const auto name = safe->presetDialog->getTextEditorContents ("name");
            safe->presetDialog.reset();

            if (result == 1)
                safe->savePreset (name, true);
        }));
}

void ChainEditor::savePreset (const juce::String& name, bool confirmReplace)
{
    // Un autre preset porte déjà ce nom : on demande avant de le remplacer
    if (confirmReplace && chain.presets.exists (name)
        && chain.presets.fileFor (name).getFileNameWithoutExtension() != chain.presetName)
    {
        const auto existing = chain.presets.fileFor (name).getFileNameWithoutExtension();
        confirm ("Replace preset?", "A preset called \"" + existing + "\" already exists. Replace it with the current sound?", "Replace",
                 [this, name] { savePreset (name, false); });
        return;
    }

    if (const auto error = chain.presets.save (name); error.isNotEmpty())
        showError (error);

    timerCallback();
}

void ChainEditor::saveCurrentPreset()
{
    if (const auto error = chain.presets.saveTo (chain.presets.current()); error.isNotEmpty())
        showError (error);

    timerCallback();
}

// « H3000 Inspired/Reverse/Backwards Lead » → « Backwards Lead »
juce::String ChainEditor::presetLeafName() const
{
    return chain.presetName.fromLastOccurrenceOf ("/", false, false);
}

void ChainEditor::deletePreset()
{
    const auto file = chain.presets.current();
    confirm ("Delete preset?", "\"" + presetLeafName() + "\" will be moved to the Trash (you can still get it back from there).",
             "Move to Trash", [this, file]
             {
                 if (! chain.presets.remove (file))
                     showError ("Couldn't move " + file.getFileName() + " to the Trash.");
                 timerCallback();
             });
}

// Boîte « OK / Annuler » ; onOk n'est appelé que sur OK, et jamais si l'interface a été fermée entre-temps
void ChainEditor::confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk)
{
    messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions()
                                                         .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                         .withTitle (title)
                                                         .withMessage (message)
                                                         .withButton (okText, 1)
                                                         .withButton ("Cancel", 0)
                                                         .withAssociatedComponent (this),
                                                     [safe = juce::Component::SafePointer<ChainEditor> (this), onOk] (int result)
                                                     {
                                                         // Après la fermeture de la boîte (onOk peut en ouvrir une autre)
                                                         if (safe != nullptr && result == 1)
                                                             juce::MessageManager::callAsync ([safe, onOk] { if (safe != nullptr) onOk(); });
                                                     });
}

void ChainEditor::showError (const juce::String& message)
{
    messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions()
                                                         .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                         .withTitle ("Presets")
                                                         .withMessage (message)
                                                         .withButton ("OK", 0)
                                                         .withAssociatedComponent (this),
                                                     nullptr);
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
    if (patchView)
    {
        patchMouseDown (e);
        return;
    }

    const int effect = pedalForComponent (e.eventComponent);
    if (effect >= 0 && e.mods.isPopupMenu())
        showPedalMenu (effect);
}

void ChainEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (patchView)
    {
        patchMouseDrag (e);
        return;
    }

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
    if (patchView)
    {
        patchMouseUp (e);
        return;
    }

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

    // Nom du preset (chargé, enregistré, ou session rouverte)
    const auto presetText = (chain.presetName.isNotEmpty() ? presetLeafName() : juce::String ("No preset"))
                            + "  " + juce::String::fromUTF8 ("\xe2\x96\xbe");
    if (presetButton.getButtonText() != presetText)
        presetButton.setButtonText (presetText);

    // Baie de brassage : mode ou câbles changés ailleurs (session rechargée, preset)
    if (chain.isPatchMode() != patchView && movingNode < 0 && ! cableDrag.active)
        setPatchView (chain.isPatchMode());
    if (patchView && movingNode < 0 && ! cableDrag.active && chain.getPatch() != shownPatch)
    {
        shownPatch = chain.getPatch();
        selectedCable = -1;
        layoutPatch();
    }

    // Routage changé ailleurs (session rechargée, automation…)
    const auto current = chain.getRouting();
    if (current != shown && dragging < 0 && ! patchView)
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

    repaint (0, headerHeight, getWidth(), stripHeight());
}

void ChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("H3O CHAIN", getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (15),
                juce::Justification::centredLeft);

    // Fond du schéma
    g.setColour (juce::Colour (0xff1b1d22));
    g.fillRoundedRectangle (diagram.toFloat(), 6.0f);

    if (patchView)
    {
        paintPatch (g);
        return;
    }

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
    g.fillRect (0, headerHeight + stripHeight() - 3, getWidth(), 3);
}

void ChainEditor::paintOverChildren (juce::Graphics& g)
{
    if (patchView)
    {
        paintPatchOver (g);
        return;
    }

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
    header.removeFromLeft (150);   // le titre
    presetViewButton.setBounds (header.removeFromLeft (66).reduced (0, 7));
    patchViewButton.setBounds (header.removeFromLeft (66).reduced (0, 7));
    presetViewButton.setToggleState (! patchView, juce::dontSendNotification);
    patchViewButton.setToggleState (patchView, juce::dontSendNotification);
    header.removeFromLeft (8);
    routingButton.setBounds (header.removeFromLeft (220).reduced (0, 7));
    header.removeFromLeft (8);
    modulationButton.setBounds (header.removeFromLeft (110).reduced (0, 7));

    // ◀ Preset ▶ : la place qui reste avant Input
    header.removeFromLeft (12);
    header.removeFromRight (8);
    previousPreset.setBounds (header.removeFromLeft (30).reduced (0, 7));
    nextPreset.setBounds (header.removeFromRight (30).reduced (0, 7));
    presetButton.setBounds (header.reduced (2, 7));

    auto strip = getLocalBounds().withTrimmedTop (headerHeight).removeFromTop (stripHeight() - 3);
    diagram = strip.removeFromTop (patchView ? patchDiagramHeight : diagramHeight).reduced (10, 0);

    // Schéma à branches ou baie de brassage : chacun ses commandes sous le schéma
    for (auto* control : { &balance, &spread, &branchWidth, &crossFeedback })
    {
        control->slider.setVisible (! patchView);
        control->label.setVisible (! patchView);
    }
    wetOnlyButton.setVisible (! patchView);
    patchHint.setVisible (patchView);
    patchHint.setBounds (strip.reduced (10, 4));
    for (auto& m : mixers)
        m.button->setVisible (patchView);

    if (patchView)
        layoutPatch();
    else
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
        pedal.editor->setTopLeftPosition ((getWidth() - pedal.editor->getWidth()) / 2, headerHeight + stripHeight());
    modulationPanel->setTopLeftPosition ((getWidth() - modulationPanel->getWidth()) / 2, headerHeight + stripHeight());
    mixerPanel->setTopLeftPosition ((getWidth() - mixerPanel->getWidth()) / 2, headerHeight + stripHeight());
}
