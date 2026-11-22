#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "OctaverEngine.h"
#include "Common/InputMode.h"
#include "Common/ModTargets.h"

// Octaver : une et deux octaves en dessous, une octave au-dessus, mélangées au son direct.
// Analog (bascules, une note à la fois, grain des octavers analogiques) ou Poly (pitch shifter, accords justes).
class OctaverProcessor : public juce::AudioProcessor
{
public:
    OctaverProcessor();
    ~OctaverProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "H3O Octaver"; }
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
    OctaverEngine::Settings currentSettings() const;

    juce::AudioProcessorValueTreeState parameters;
    ModTargets modulation;   // valeurs lues par l'effet (réglage + modulation)
    InputMode::Selector input;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    OctaverEngine engine;

    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* sub1Param = nullptr;
    std::atomic<float>* sub2Param = nullptr;
    std::atomic<float>* upParam = nullptr;
    std::atomic<float>* toneParam = nullptr;

    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaverProcessor)
};
