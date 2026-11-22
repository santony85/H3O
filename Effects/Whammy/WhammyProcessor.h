#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "WhammyEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Pitch shifter à pédale : la note glisse d'un intervalle (talon) à l'autre (pointe).
// La pédale est un réglage automatisable dans Logic, et modulable dans H3O Chain (enveloppe, LFO…).
class WhammyProcessor : public juce::AudioProcessor
{
public:
    WhammyProcessor();
    ~WhammyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Whammy"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static const juce::StringArray modeNames;

    // Pour l'interface
    WhammyEngine::Settings currentSettings() const;
    float getCurrentSemitones() const noexcept { return shownSemitones.load(); }

    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    WhammyEngine engine;

    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* heelParam = nullptr;
    std::atomic<float>* toeParam = nullptr;
    std::atomic<float>* pedalParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* detuneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    std::atomic<float> shownSemitones { 0.0f };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhammyProcessor)
};
