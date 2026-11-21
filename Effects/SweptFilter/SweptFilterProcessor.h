#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SweptFilterEngine.h"
#include "Common/InputMode.h"

// Swept Filter façon H3000 : filtre résonant balayé par un LFO ou par l'attaque du jeu (auto-wah).
class SweptFilterProcessor : public juce::AudioProcessor
{
public:
    SweptFilterProcessor();
    ~SweptFilterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Swept Filter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Les réglages actuels, sous la forme qu'attend le moteur (aussi pour dessiner la courbe)
    SweptFilterEngine::Settings currentSettings() const noexcept;

    // Position actuelle du filtre (0 = Low, 1 = High) à gauche et à droite, pour l'interface
    float getPosition (int channel) const noexcept { return position[channel & 1].load(); }

    juce::AudioProcessorValueTreeState parameters;
    InputMode::Selector input;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SweptFilterEngine engine;

    std::atomic<float>* sourceParam = nullptr;
    std::atomic<float>* typeParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* lowParam = nullptr;
    std::atomic<float>* highParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* phaseParam = nullptr;
    std::atomic<float>* sensitivityParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* directionParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::SmoothedValue<float> smoothedMix;
    std::atomic<float> position[2] { { 0.0f }, { 0.0f } };

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweptFilterProcessor)
};
