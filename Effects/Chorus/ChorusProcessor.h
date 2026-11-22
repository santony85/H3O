#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ChorusEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Chorus stéréo multi-voix façon H3000 : jusqu'à 4 voix par côté,
// LFO en quadrature gauche/droite, feedback positif ou négatif, filtre Tone.
class ChorusProcessor : public juce::AudioProcessor
{
public:
    ChorusProcessor();
    ~ChorusProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Chorus"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

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

    static constexpr float maxBaseDelayMs = 40.0f;
    static constexpr float maxDepthMs     = 10.0f;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ChorusEngine engine;
    juce::dsp::StateVariableTPTFilter<float> toneFilter;   // passe-bas sur le son traité

    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* delayParam = nullptr;
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::SmoothedValue<float> smoothedDelay, smoothedDepth, smoothedFeedback, smoothedWidth, smoothedMix;

    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusProcessor)
};
