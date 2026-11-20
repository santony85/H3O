#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ReverseEngine.h"
#include "Common/InputMode.h"

// Reverse Shift façon H3000 : le son revient joué à l'envers, par tranches,
// éventuellement transposé (octave au-dessus, quinte…), avec feedback pour des nappes.
class ReverseProcessor : public juce::AudioProcessor
{
public:
    ReverseProcessor();
    ~ReverseProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Reverse"; }
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
    InputMode::Selector input;

    static constexpr float minLengthMs = 50.0f;
    static constexpr float maxLengthMs = 2000.0f;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ReverseEngine engines[2];                              // gauche, droite (tranches décalées)
    juce::dsp::StateVariableTPTFilter<float> toneFilter;   // passe-bas sur le son traité

    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* shiftParam = nullptr;
    std::atomic<float>* fineParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedRatio;
    juce::SmoothedValue<float> smoothedFeedback, smoothedSpread, smoothedMix;

    float feedbackSample[2] = { 0.0f, 0.0f };
    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseProcessor)
};
