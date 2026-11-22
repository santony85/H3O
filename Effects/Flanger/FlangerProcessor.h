#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include "FlangerEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Flanger stéréo (classique ou through-zero), libre ou calé sur le tempo de Logic.
class FlangerProcessor : public juce::AudioProcessor
{
public:
    FlangerProcessor();
    ~FlangerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Flanger"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static const juce::StringArray modeNames, shapeNames, divisionNames;

    // Pour l'interface
    FlangerEngine::Settings currentSettings() const;
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

    FlangerEngine engine;

    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* manualParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* stereoPhaseParam = nullptr;

    std::atomic<float> currentRate { 5.0f }, currentBpm { 120.0f };
    std::atomic<double> shownPhase { 0.0 };
    double beatsPerCycle = 0.5;

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlangerProcessor)
};
