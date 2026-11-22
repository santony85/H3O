#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BandDelayEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Band Delay façon H3000 : jusqu'à 8 échos, chacun dans son propre filtre résonant
// (sa couleur), avec son propre retour, alternés gauche / droite.
class BandDelayProcessor : public juce::AudioProcessor
{
public:
    BandDelayProcessor();
    ~BandDelayProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Band Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Les réglages actuels, sous la forme qu'attend le moteur (aussi pour dessiner les bandes)
    BandDelayEngine::Settings currentSettings() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

    static constexpr float maxTimeMs = 2000.0f;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    BandDelayEngine engine;

    std::atomic<float>* bandsParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* lowParam = nullptr;
    std::atomic<float>* highParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* patternParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::SmoothedValue<float> smoothedFeedback, smoothedMix;

    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandDelayProcessor)
};
