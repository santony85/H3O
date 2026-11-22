#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include "PhaserEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Phaser stéréo de 2 à 12 étages, libre ou calé sur le tempo de Logic.
class PhaserProcessor : public juce::AudioProcessor
{
public:
    PhaserProcessor();
    ~PhaserProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Phaser"; }
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

    static const juce::StringArray stageNames, shapeNames, divisionNames;

    // Pour l'interface
    PhaserEngine::Settings currentSettings() const;
    float getCurrentRate() const noexcept   { return currentRate.load(); }   // Hz, tempo compris
    float getCurrentBpm() const noexcept    { return currentBpm.load(); }
    double getPhase() const noexcept        { return shownPhase.load(); }
    bool isSynced() const noexcept          { return syncParam->load() >= 0.5f; }

    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float computeRate();

    PhaserEngine engine;

    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* stagesParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* centreParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* stereoPhaseParam = nullptr;

    std::atomic<float> currentRate { 5.0f }, currentBpm { 120.0f };
    std::atomic<double> shownPhase { 0.0 };
    double beatsPerCycle = 0.5;

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaserProcessor)
};
