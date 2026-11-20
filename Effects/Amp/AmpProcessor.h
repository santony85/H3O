#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Common/InputMode.h"
#include "RateConverter.h"

namespace nam { class DSP; }

// Préampli / ampli à base de Neural Amp Modeler (NAM).
//
// Chaîne mono : Input → Noise Gate → modèle .nam → EQ 3 bandes → baffle (IR ou simulation) → Output,
// puis le résultat est envoyé à gauche et à droite.
class AmpProcessor : public juce::AudioProcessor
{
public:
    AmpProcessor();
    ~AmpProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Amp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;
    InputMode::Selector input;

    // Chargement des fichiers (hors du fil audio). Renvoie un message d'erreur, vide si tout va bien.
    juce::String loadModel (const juce::File& file);
    void clearModel();
    juce::String loadImpulseResponse (const juce::File& file);
    void clearImpulseResponse();

    // Pour l'interface
    struct ModelInfo
    {
        juce::String name;           // vide si aucun modèle
        double sampleRate = 0.0;     // fréquence d'entraînement du modèle
        bool resampled = false;      // converti vers / depuis la fréquence de l'hôte
        bool hasLoudness = false;
    };

    ModelInfo getModelInfo() const;
    juce::File getModelFile() const { return modelFile; }

    // Dernière erreur de chargement (ex. modèle d'une session introuvable), vide si aucune
    juce::String getLastModelError() const { return lastModelError; }
    juce::File getImpulseResponseFile() const { return irFile; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Un modèle prêt à jouer, avec sa conversion de fréquence
    struct LoadedModel
    {
        std::unique_ptr<nam::DSP> dsp;
        RateConverter converter;
        double modelRate = 48000.0;
        bool resampled = false;
        bool hasLoudness = false;
        float normaliseGain = 1.0f;   // ramène le modèle à un volume de référence (-18 dB)
        juce::String name;
    };

    // Prépare un modèle pour la fréquence et la taille de bloc courantes (peut allouer)
    void configure (LoadedModel&) const;
    juce::String tryLoadModel (const juce::File&);

    void updateFilters();
    void processModel (float* data, int n);

    // Protège l'échange de modèle : le fil audio ne bloque jamais (tryLock), l'interface oui
    mutable juce::SpinLock modelLock;
    std::unique_ptr<LoadedModel> model;

    juce::File modelFile, irFile;
    juce::String lastModelError;

    juce::dsp::NoiseGate<float> gate;
    juce::dsp::IIR::Filter<float> bassFilter, midFilter, trebleFilter;
    juce::dsp::IIR::Filter<float> cabHighPass, cabLowPass1, cabLowPass2;   // baffle simulé
    juce::dsp::Convolution cabinet;                                        // baffle par IR
    std::atomic<bool> irLoaded { false };

    float lastBass = -100.0f, lastMid = -100.0f, lastTreble = -100.0f;

    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* gateParam = nullptr;
    std::atomic<float>* bassParam = nullptr;
    std::atomic<float>* midParam = nullptr;
    std::atomic<float>* trebleParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* normaliseParam = nullptr;
    std::atomic<float>* cabParam = nullptr;

    juce::SmoothedValue<float> smoothedInput, smoothedOutput;

    std::vector<float> mono, scratch;
    double currentSampleRate = 48000.0;
    int maxBlockSize = 512;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};
