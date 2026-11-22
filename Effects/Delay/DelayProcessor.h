#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

class DelayProcessor : public juce::AudioProcessor
{
public:
    DelayProcessor();
    ~DelayProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return maxDelaySeconds; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Paramètres exposés à Logic (automatisables)
    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

    // Divisions de note proposées en mode Sync (affichées dans l'interface)
    static const juce::StringArray divisionNames;

    // Valeurs lues par l'interface pour l'affichage
    float getCurrentDelayMs() const noexcept { return currentDelayMs.load(); }
    float getCurrentBpm() const noexcept     { return currentBpm.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Calcule le temps de délai cible (ms) selon le mode libre ou synchronisé
    float computeTargetDelayMs();

    // Met à jour les fréquences de coupure des filtres (une fois par bloc)
    void updateFilters();

    // 4 s : une ronde (1/1) à 60 BPM. En dessous de 60 BPM, le délai est plafonné.
    static constexpr double maxDelaySeconds = 4.0;

    // Ligne à retard avec interpolation linéaire (temps de délai fractionnaire)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;

    // Filtres sur les répétitions : placés dans la boucle de feedback,
    // chaque écho repasse dedans et devient un peu plus filtré que le précédent.
    // (Filtres TPT : stables même quand on change la fréquence en cours de lecture)
    juce::dsp::StateVariableTPTFilter<float> lowCutFilter;   // passe-haut : retire les graves
    juce::dsp::StateVariableTPTFilter<float> highCutFilter;  // passe-bas : retire les aigus

    // Lissage pour éviter les clics quand on tourne les boutons
    juce::SmoothedValue<float> smoothedDelaySamples, smoothedFeedback, smoothedMix;

    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* pingPongParam = nullptr;

    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    // Dernier tempo connu : conservé si l'hôte ne fournit pas de tempo (ex. Standalone)
    std::atomic<float> currentBpm { 120.0f };
    std::atomic<float> currentDelayMs { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayProcessor)
};
