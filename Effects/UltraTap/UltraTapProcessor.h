#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "UltraTapEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Ultra-Tap façon H3000 : delay multi-répétitions (jusqu'à 24), régulières, en rebond
// ou en ralenti, qui s'éteignent ou qui montent, alternées gauche / droite.
class UltraTapProcessor : public juce::AudioProcessor
{
public:
    UltraTapProcessor();
    ~UltraTapProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Ultra-Tap"; }
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

    // Les réglages actuels, sous la forme qu'attend le moteur (aussi pour dessiner les taps)
    UltraTapEngine::Settings currentSettings() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

    static constexpr float maxPreDelayMs = 500.0f;
    static constexpr float maxLengthMs   = 2000.0f;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    UltraTapEngine engine;
    juce::dsp::StateVariableTPTFilter<float> toneFilter;   // passe-bas sur les répétitions

    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* tapsParam = nullptr;
    std::atomic<float>* slopeParam = nullptr;
    std::atomic<float>* taperParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::SmoothedValue<float> smoothedFeedback, smoothedMix;

    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltraTapProcessor)
};
