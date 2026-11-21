#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "LayeredShiftEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Layered Shift façon H3000 : deux couches transposées, doublées gauche / droite et
// désaccordées, avec un lent balayage de hauteur, pour des nappes et des cascades.
class LayeredShiftProcessor : public juce::AudioProcessor
{
public:
    LayeredShiftProcessor();
    ~LayeredShiftProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Layered Shift"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    // Presets d'usine : apparaissent dans le menu de presets de Logic
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    LayeredShiftEngine::Settings currentSettings() const noexcept;

    LayeredShiftEngine engine;
    juce::dsp::StateVariableTPTFilter<float> toneFilter;   // passe-bas sur le son traité

    struct LayerParams
    {
        std::atomic<float>* semitones = nullptr;
        std::atomic<float>* cents = nullptr;
        std::atomic<float>* delayMs = nullptr;
        std::atomic<float>* level = nullptr;
    };

    LayerParams layerParams[2];
    std::atomic<float>* detuneParam = nullptr;
    std::atomic<float>* sweepRateParam = nullptr;
    std::atomic<float>* sweepDepthParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::SmoothedValue<float> smoothedFeedback, smoothedMix;

    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayeredShiftProcessor)
};
