#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchShifter.h"
#include "Common/InputMode.h"

class HarmonizerProcessor : public juce::AudioProcessor
{
public:
    HarmonizerProcessor();
    ~HarmonizerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Harmonizer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr double maxVoiceDelaySeconds = 0.1;   // réglage Delay de chaque voix
    static constexpr double maxSpliceSeconds     = 0.1;   // réglage Splice

    // Une voix = une transposition, avec sa propre hauteur, son délai et son niveau
    struct VoiceParams
    {
        std::atomic<float>* semitones = nullptr;
        std::atomic<float>* cents = nullptr;
        std::atomic<float>* delayMs = nullptr;
        std::atomic<float>* level = nullptr;
    };

    PitchShifter shifter;                 // tampon partagé par les deux voix
    PitchShifter::Voice voiceState[2];
    VoiceParams voiceParams[2];

    juce::SmoothedValue<float> smoothedDelay[2], smoothedLevel[2];
    juce::SmoothedValue<float> smoothedSplice, smoothedFeedback, smoothedSpread, smoothedMix;

    std::atomic<float>* spliceParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    double currentSampleRate = 44100.0;
    float feedbackSample = 0.0f;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerProcessor)
};
