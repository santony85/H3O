#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ReverbEngine.h"
#include "Common/InputMode.h"

// Réverbération stéréo façon H3000 : Room, Hall et Plate,
// avec pre-delay, taille, durée, amortissement des aigus et filtres de sortie.
class ReverbProcessor : public juce::AudioProcessor
{
public:
    ReverbProcessor();
    ~ReverbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 25.0; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Les réglages actuels, sous la forme qu'attend le moteur (aussi pour dessiner la courbe)
    ReverbEngine::Settings currentSettings() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    InputMode::Selector input;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ReverbEngine engine;
    juce::dsp::StateVariableTPTFilter<float> lowCut, highCut;   // filtres sur la réverbe seule

    std::atomic<float>* typeParam = nullptr;
    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* diffusionParam = nullptr;
    std::atomic<float>* modulationParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::SmoothedValue<float> smoothedWidth, smoothedMix;

    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbProcessor)
};
