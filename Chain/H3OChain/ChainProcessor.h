#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SweptFilter/SweptFilterProcessor.h"
#include "Amp/AmpProcessor.h"
#include "Harmonizer/HarmonizerProcessor.h"
#include "Reverse/ReverseProcessor.h"
#include "LayeredShift/LayeredShiftProcessor.h"
#include "Chorus/ChorusProcessor.h"
#include "Delay/DelayProcessor.h"
#include "BandDelay/BandDelayProcessor.h"
#include "UltraTap/UltraTapProcessor.h"
#include "Compressor/CompressorProcessor.h"
#include "Tremolo/TremoloProcessor.h"
#include "Phaser/PhaserProcessor.h"
#include "Flanger/FlangerProcessor.h"
#include "Whammy/WhammyProcessor.h"
#include "Reverb/ReverbProcessor.h"
#include "ChainRouting.h"
#include "ChainPatch.h"
#include "ModulationSources.h"
#include "Common/ModTargets.h"
#include "ChainPresets.h"

// Multi-effets : fait passer le son dans chaque effet, dans l'ordre choisi,
// avec deux branches parallèles A et B possibles (voir ChainRouting.h).
// Les effets sont les mêmes classes que dans les plugins séparés.
class ChainProcessor : public juce::AudioProcessor
{
public:
    ChainProcessor();
    ~ChainProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Chain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // État complet (paramètres, effets, routage, modulation) : sessions et presets.
    // keepInput : garde l'entrée actuelle (chargement d'un preset)
    juce::ValueTree createState();
    bool restoreState (const juce::ValueTree&, bool keepInput);

    // Nom du preset chargé ou enregistré en dernier (vide : aucun). Gardé avec la session.
    juce::String presetName;
    ChainPresets presets { *this };

    // Paramètres propres à la chaîne : activer / court-circuiter chaque effet
    juce::AudioProcessorValueTreeState parameters;
    InputMode::Selector input;

    // Les effets (dans leur ordre d'origine : celui de ChainRouting::Effect)
    SweptFilterProcessor filter;
    AmpProcessor amp;
    HarmonizerProcessor harmonizer;
    ReverseProcessor reverse;
    LayeredShiftProcessor layered;
    ChorusProcessor chorus;
    DelayProcessor delay;
    BandDelayProcessor bandDelay;
    UltraTapProcessor ultraTap;
    ReverbProcessor reverb;
    CompressorProcessor compressor;
    TremoloProcessor tremolo;
    PhaserProcessor phaser;
    FlangerProcessor flanger;
    WhammyProcessor whammy;

    bool isFilterOn() const noexcept     { return filterOn->load() >= 0.5f; }
    bool isAmpOn() const noexcept        { return ampOn->load() >= 0.5f; }
    bool isHarmonizerOn() const noexcept { return harmonizerOn->load() >= 0.5f; }
    bool isLayeredOn() const noexcept    { return layeredOn->load() >= 0.5f; }
    bool isReverseOn() const noexcept    { return reverseOn->load() >= 0.5f; }
    bool isChorusOn() const noexcept     { return chorusOn->load() >= 0.5f; }
    bool isDelayOn() const noexcept      { return delayOn->load() >= 0.5f; }
    bool isBandDelayOn() const noexcept  { return bandDelayOn->load() >= 0.5f; }
    bool isUltraTapOn() const noexcept   { return ultraTapOn->load() >= 0.5f; }
    bool isReverbOn() const noexcept     { return reverbOn->load() >= 0.5f; }

    // Routage : modifiable depuis l'interface pendant que le son passe
    ChainRouting::Routing getRouting() const noexcept { return ChainRouting::Routing::unpack (routing.load()); }
    void setRouting (const ChainRouting::Routing&);

    // Matrice de modulation : 8 lignes « source → destination × quantité »
    static constexpr int numSlots = 8;

    struct Destination
    {
        juce::String key;          // "delay/time" : enregistré dans les sessions
        juce::String effectName;   // "Delay"
        juce::String paramName;    // "Time"
        int effect = 0;            // ChainRouting::Effect, ou numEffects pour les branches
        int numSteps = 0;          // réglage par crans : nombre de crans de toute la course, sinon 0
        juce::String stepUnit;     // "st" pour des demi-tons, sinon vide
        ModTargets::Target* target = nullptr;
    };

    const std::vector<Destination>& getDestinations() const noexcept { return destinations; }
    int getSlotDestination (int slot) const noexcept   { return slotDestination[(size_t) slot].load(); }
    void setSlotDestination (int slot, int destination);
    int findDestination (const juce::String& key) const;

    static juce::String slotParamId (int slot, const char* name) { return "mod" + juce::String (slot + 1) + name; }

    // Pour l'affichage : la valeur de chaque source, et ce que chaque ligne ajoute en ce moment
    float getSourceValue (int source) const noexcept   { return sourceValues[(size_t) juce::jlimit (0, (int) ModulationSources::numSources - 1, source)].load(); }
    float getSlotOutput (int slot) const noexcept      { return slotOutput[(size_t) slot].load(); }
    bool isSlotActive (int slot) const noexcept;

    // Tempo suivi par les LFO synchronisés : celui de Logic, ou 120 BPM sans hôte (application autonome)
    float getTempo() const noexcept { return tempo.load(); }

    // Baie de brassage : des câbles libres à la place des branches A / B (voir ChainPatch.h)
    bool isPatchMode() const noexcept { return patchMode.load(); }
    void setPatchMode (bool);                       // la première fois, le routage à branches est converti en câbles
    ChainPatch::Patch getPatch() const;
    void setPatch (const ChainPatch::Patch&);       // depuis l'interface : appliqué au son au bloc suivant
    void convertRoutingToPatch();                   // remplace les câbles par la traduction du routage à branches
    static juce::String mixerParamId (int mixer, const char* name) { return "mix" + juce::String (mixer + 1) + name; }

    juce::AudioProcessor& effect (int id) noexcept { return *effects[(size_t) id]; }
    bool isEffectOn (int id) const noexcept        { return onParams[(size_t) id]->load() >= 0.5f; }
    static const char* onParamId (int id);

private:
    std::array<juce::AudioProcessor*, ChainRouting::numEffects> effects {};
    std::array<std::atomic<float>*, ChainRouting::numEffects> onParams {};
    std::atomic<std::uint64_t> routing { ChainRouting::Routing::series().pack() };

    std::atomic<float>* branchBalance = nullptr;
    std::atomic<float>* branchSpread = nullptr;
    std::atomic<float>* crossFeedback = nullptr;
    std::atomic<float>* branchWidth = nullptr;
    std::atomic<float>* wetOnly = nullptr;
    std::array<int, ChainRouting::numEffects> mixDestination {};   // destination « Mix » de chaque effet (-1 : pas de Mix)

    // Branches A et B, et la sortie de B renvoyée vers l'entrée de A (retour croisé)
    juce::AudioBuffer<float> laneA, laneB, laneWet, laneEffect, feedbackLine;
    int feedbackPos = 0;

    // Gains de mélange lissés : tourner Balance ou Spread ne fait pas de clic
    std::array<juce::SmoothedValue<float>, 4> mixGains;   // A gauche, A droite, B gauche, B droite
    juce::SmoothedValue<float> feedbackGain, widthGain;

    void processPatch (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void gather (int node, int port, juce::AudioBuffer<float>& dest, int n);
    void publishPlan (const ChainPatch::Patch&);

    std::atomic<bool> patchMode { false };
    mutable juce::CriticalSection patchLock;            // la baie modifiée par l'interface (et les sessions)
    ChainPatch::Patch patch = ChainPatch::Patch::defaultLayout();
    juce::String patchSource;                           // l'état des branches quand on a quitté (ou rempli) la baie
    juce::String branchSignature();                     // routage, Balance, Spread, Wet Only et pédales allumées
    juce::SpinLock planLock;                            // passage du plan de l'interface au son, sans attente
    ChainPatch::Plan pendingPlan, activePlan;
    bool planPending = false;
    std::array<juce::AudioBuffer<float>, ChainPatch::numNodes> nodeOut;   // ce qui sort de chaque prise OUT
    std::array<int, ChainPatch::numNodes> nodeLength {};
    juce::AudioBuffer<float> gatherA, gatherB;
    std::array<std::atomic<float>*, ChainPatch::numMixers * 4> mixerParams {};   // niveau A, niveau B, pan A, pan B
    std::array<float, ChainPatch::numMixers * 4> mixerGains {};                  // gains du paquet précédent (rampes)
    bool mixerGainsReady = false;

    void processLane (int lane, const ChainRouting::Routing&, juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void processRouting (const ChainRouting::Routing&, juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void updateModulation (const juce::AudioBuffer<float>&, int startInBlock, const ChainRouting::Routing&);

    // Position du morceau au début du bloc (pour caler les LFO)
    std::atomic<float> tempo { 120.0f };
    double blockPpq = 0.0, quartersPerBar = 4.0;
    bool hostPlaying = false;
    std::atomic<float>* lfoSync[2] {}, *lfoDivision[2] {};

    // Modulation
    ModTargets chainTargets;   // Balance, Spread, Cross Feedback
    std::vector<Destination> destinations;
    std::array<std::atomic<int>, numSlots> slotDestination;
    std::array<std::atomic<float>*, numSlots> slotSource {}, slotAmount {};
    std::array<std::atomic<float>, numSlots> slotOutput;
    std::array<std::atomic<float>, ModulationSources::numSources> sourceValues;
    std::vector<float> modSum, modApplied;   // décalage de chaque destination (calculé, déjà posé)
    std::vector<float> monoScratch;
    ModulationSources sources;
    std::atomic<float>* lfoRate[2] {}, *lfoShape[2] {}, *randomRate = nullptr, *envAttack = nullptr, *envRelease = nullptr;

    std::atomic<float>* harmonizerOn = nullptr;
    std::atomic<float>* ampOn = nullptr;
    std::atomic<float>* filterOn = nullptr;
    std::atomic<float>* reverseOn = nullptr;
    std::atomic<float>* layeredOn = nullptr;
    std::atomic<float>* chorusOn = nullptr;
    std::atomic<float>* delayOn = nullptr;
    std::atomic<float>* bandDelayOn = nullptr;
    std::atomic<float>* ultraTapOn = nullptr;
    std::atomic<float>* reverbOn = nullptr;
    std::atomic<float>* compressorOn = nullptr;
    std::atomic<float>* tremoloOn = nullptr;
    std::atomic<float>* phaserOn = nullptr;
    std::atomic<float>* flangerOn = nullptr;
    std::atomic<float>* whammyOn = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainProcessor)
};
