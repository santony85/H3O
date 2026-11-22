#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Compresseur de studio : trois caractères (Clean, Opto, Punch), genou doux, coupe-bas du détecteur,
// relâche automatique, gain automatique et compression parallèle (Mix).
class CompressorProcessor : public juce::AudioProcessor
{
public:
    CompressorProcessor();
    ~CompressorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Compressor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Les réglages du moment (réglages + modulation de H3O Chain), pour l'interface
    CompressorEngine::Settings currentSettings() const;

    // Pour l'interface : réduction de gain (dB) et niveau d'entrée vu par le détecteur (dB)
    float getGainReduction() const noexcept { return gainReduction.load(); }
    float getInputLevel() const noexcept    { return inputLevel.load(); }

    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    CompressorEngine engine;

    std::atomic<float>* styleParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam = nullptr;
    std::atomic<float>* kneeParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* autoReleaseParam = nullptr;
    std::atomic<float>* sidechainParam = nullptr;
    std::atomic<float>* makeupParam = nullptr;
    std::atomic<float>* autoMakeupParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    std::atomic<float> gainReduction { 0.0f }, inputLevel { -120.0f };

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorProcessor)
};
