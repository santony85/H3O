#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChainProcessor.h"
#include "ModulationPanel.h"
#include "MixerPanel.h"

// Un pédalier dessiné comme un schéma du signal :
//
//            ┌─ A : pédales… ─┐
//   pédales ─┤                ├─ pédales
//            └─ B : pédales… ─┘
//
// - un clic sur le nom d'une pédale affiche ses réglages en dessous ;
// - on glisse une pédale pour changer sa place : dans la série, ou dans une des branches A / B ;
// - clic droit sur une pédale : menu pour la ranger ou la décaler ;
// - le menu Routing propose des routages tout prêts ;
// - Balance, Spread et Cross Feedback règlent le mélange des deux branches ;
// - le bouton Modulation affiche la matrice de modulation (un point violet marque les pédales modulées) ;
// - ◀ Preset ▶ : charger, enregistrer, parcourir tes presets.
//
// Baie de brassage (menu Routing › Patch Bay) : les mêmes pédales, posées librement, reliées par des câbles
// (voir ChainEditorPatch.cpp) : on tire un câble d'une prise OUT vers une prise IN, d'une source de modulation
// vers la prise MOD d'une pédale ; on reprend un câble par sa prise IN ; clic droit ou Suppr pour le retirer.
class ChainEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit ChainEditor (ChainProcessor&);
    ~ChainEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // Reçus des boutons des pédales (glisser-déposer, clic droit)
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Pedal
    {
        juce::String name;
        juce::Colour colour;
        std::unique_ptr<juce::AudioProcessorEditor> editor;   // l'interface de l'effet, réutilisée telle quelle
        std::unique_ptr<juce::TextButton> selectButton;       // affiche ses réglages ; se glisse pour changer de place
        std::unique_ptr<juce::ToggleButton> onButton;         // allume / coupe l'effet
        std::unique_ptr<ButtonAttachment> onAttachment;
        juce::Rectangle<int> cell;                            // sa place dans le schéma
    };

    struct BranchControl
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // Endroit où lâcher une pédale : une section et une place dans cette section
    struct Drop
    {
        int lane = ChainRouting::pre, index = 0;
    };

    void addPedal (const juce::String& name, juce::Colour);
    void setupBranchControl (BranchControl&, const juce::String& paramId, const juce::String& name, const juce::String& tooltip);
    void select (int index);

    void applyRouting (const ChainRouting::Routing&);
    void layoutDiagram();
    void showRoutingMenu();

    // Presets
    void showPresetMenu();
    void loadPreset (const juce::File&);
    void savePresetAs();
    void savePreset (const juce::String& name, bool confirmReplace);
    void saveCurrentPreset();
    juce::String presetLeafName() const;
    void deletePreset();
    void showError (const juce::String&);
    void confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> onOk);
    juce::ScopedMessageBox messageBox;   // se ferme avec l'interface
    void showPedalMenu (int effect);
    juce::String routingName() const;

    int pedalForComponent (const juce::Component*) const;
    Drop findDrop (juce::Point<int>) const;
    float markerX (const Drop&) const;
    int rowY (int lane) const;   // centre vertical de la rangée d'une section

    // Assombrit les réglages des effets coupés, suit les changements de routage (session rechargée)
    void timerCallback() override;

    ChainProcessor& chain;
    InputMode::Box inputBox { chain.parameters, juce::Colours::white };
    juce::TextButton routingButton, modulationButton;
    juce::TextButton presetButton, previousPreset { juce::String::fromUTF8 ("\xe2\x97\x80") }, nextPreset { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    std::unique_ptr<juce::AlertWindow> presetDialog;
    std::unique_ptr<ModulationPanel> modulationPanel;
    juce::TooltipWindow tooltips { this, 600 };

    // Police un peu plus petite pour les noms des pédales
    struct PedalLook : juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.6f));
        }
    };
    PedalLook pedalLook;   // déclaré avant les pédales : détruit après leurs boutons

    std::vector<Pedal> pedals;   // indexé par ChainRouting::Effect
    BranchControl balance, spread, branchWidth, crossFeedback;
    juce::ToggleButton wetOnlyButton { "B Wet Only" };   // Wet/Dry/Wet : effets de B à 100 %, côte à côte
    std::unique_ptr<ButtonAttachment> wetOnlyAttachment;
    void setParameter (const juce::String& id, float value);
    int selected = 0;   // pédale affichée, -1 pour la matrice de modulation, mixerSelection pour les mélangeurs
    static constexpr int mixerSelection = -2;

    //==============================================================================
    // Baie de brassage (ChainEditorPatch.cpp)
    struct MixerCell
    {
        std::unique_ptr<juce::TextButton> button;
        juce::Rectangle<int> cell;
    };

    struct CableDrag
    {
        bool active = false, mod = false;
        int from = -1;               // nœud de départ (audio) ou source de modulation
        juce::Point<float> end;
    };

    bool patchView = false;
    ChainPatch::Patch shownPatch;
    std::array<MixerCell, ChainPatch::numMixers> mixers;
    std::unique_ptr<MixerPanel> mixerPanel;
    juce::Label patchHint;
    int movingNode = -1;             // pédale ou mélangeur que l'on glisse
    juce::Point<int> moveOffset;
    CableDrag cableDrag;
    int selectedCable = -1;          // câble audio sélectionné (index dans shownPatch.cables)
    int hoverCable = -1, hoverMod = -1;
    int contentHeight = 0;

    int stripHeight() const { return patchView ? patchStripHeight : laneStripHeight; }
    void setPatchView (bool);
    void layoutPatch();
    void commitPatch();
    void paintPatch (juce::Graphics&);
    void paintPatchOver (juce::Graphics&);
    bool patchMouseDown (const juce::MouseEvent&);
    bool patchMouseDrag (const juce::MouseEvent&);
    bool patchMouseUp (const juce::MouseEvent&);
    juce::Rectangle<int> nodeCell (int node) const;
    juce::Colour nodeColour (int node) const;
    juce::String nodeName (int node) const;
    juce::Point<float> inJack (int node, int port) const;
    juce::Point<float> outJack (int node) const;
    juce::Point<float> modJack (int node) const;
    juce::Point<float> sourceJack (int source) const;
    juce::Path cablePath (juce::Point<float> from, juce::Point<float> to) const;
    int nodeForComponent (const juce::Component*) const;
    int modNodeForSlot (int slot) const;
    int cableAt (juce::Point<float>) const;
    int modCableAt (juce::Point<float>) const;
    void showCableMenu (int cable);
    void showModCableMenu (int slot);
    void showModTargetMenu (int node, int source);
    void showNodeMenu (int node);

    // Schéma : routage affiché, séparation et fusion des branches, cases des branches vides
    ChainRouting::Routing shown = ChainRouting::Routing::series();
    juce::Rectangle<int> diagram, emptyA, emptyB;
    int splitX = 0, mergeX = 0;

    // Glisser-déposer
    int dragging = -1;
    juce::Point<int> dragPos;
    Drop drop;

    static constexpr int headerHeight   = 40;   // titre, Routing, Input
    static constexpr int nameHeight     = 24;
    static constexpr int cellHeight     = 44;   // nom + interrupteur
    static constexpr int rowGap         = 8;
    static constexpr int diagramHeight  = 3 * cellHeight + 2 * rowGap + 16;
    static constexpr int controlsHeight = 30;   // Balance, Spread, Cross Feedback
    static constexpr int laneStripHeight = diagramHeight + controlsHeight + 6;
    static constexpr int patchDiagramHeight = 340;
    static constexpr int patchStripHeight   = patchDiagramHeight + controlsHeight + 6;
    static constexpr int arrowWidth     = 12;
    static constexpr int splitWidth     = 22;   // place des embranchements
    static constexpr int minPedalWidth  = 68;   // de quoi lire « Harmonizer »
    static constexpr int maxPedalWidth  = 124;
    static constexpr int editorWidth    = 1440;
    static constexpr int diagramMargin  = 12;   // marge à gauche et à droite, dans le schéma

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainEditor)
};
