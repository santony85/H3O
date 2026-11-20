#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Amp/AmpProcessor.h"
#include "Harmonizer/HarmonizerProcessor.h"
#include "Reverse/ReverseProcessor.h"
#include "Chorus/ChorusProcessor.h"
#include "Delay/DelayProcessor.h"

// Multi-effets : fait passer le son dans chaque effet, l'un après l'autre.
// Les effets sont les mêmes classes que dans les plugins séparés.
class ChainProcessor : public juce::AudioProcessor
{
public:
    ChainProcessor();
    ~ChainProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Chain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Paramètres propres à la chaîne : activer / court-circuiter chaque effet
    juce::AudioProcessorValueTreeState parameters;
    InputMode::Selector input;

    // Les effets, dans l'ordre du signal
    AmpProcessor amp;
    HarmonizerProcessor harmonizer;
    ReverseProcessor reverse;
    ChorusProcessor chorus;
    DelayProcessor delay;

    bool isAmpOn() const noexcept        { return ampOn->load() >= 0.5f; }
    bool isHarmonizerOn() const noexcept { return harmonizerOn->load() >= 0.5f; }
    bool isReverseOn() const noexcept    { return reverseOn->load() >= 0.5f; }
    bool isChorusOn() const noexcept     { return chorusOn->load() >= 0.5f; }
    bool isDelayOn() const noexcept      { return delayOn->load() >= 0.5f; }

private:
    std::atomic<float>* harmonizerOn = nullptr;
    std::atomic<float>* ampOn = nullptr;
    std::atomic<float>* reverseOn = nullptr;
    std::atomic<float>* chorusOn = nullptr;
    std::atomic<float>* delayOn = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainProcessor)
};
