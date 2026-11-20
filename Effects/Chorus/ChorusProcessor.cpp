#include "ChorusProcessor.h"
#include "ChorusEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float rate, depth, delay, voices, feedback, width, tone, mix;
    };

    constexpr Preset presets[] = {
        //  nom                     Hz    ms   ms  voix  fb %  larg. %  Tone Hz  mix %
        { "Classic Stereo Chorus",  0.6f, 3.0f, 12, 1,     0,  100,      9000,   50 },
        { "Lush Ensemble",          0.35f, 4.0f, 18, 3,   10,  100,      7000,   55 },
        { "Slow Wide Shimmer",      0.12f, 6.0f, 25, 4,   25,  100,     10000,   45 },
        { "Subtle Doubler",         0.2f, 1.5f, 22, 2,     0,  100,     12000,   35 },
        { "Vibrato",                5.0f, 1.5f,  5, 1,     0,    0,     20000,  100 },
        { "Jet Flange",             0.15f, 2.5f,  2, 1,   70,  100,     12000,   50 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

ChorusProcessor::ChorusProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    rateParam     = parameters.getRawParameterValue ("rate");
    depthParam    = parameters.getRawParameterValue ("depth");
    delayParam    = parameters.getRawParameterValue ("delay");
    voicesParam   = parameters.getRawParameterValue ("voices");
    feedbackParam = parameters.getRawParameterValue ("feedback");
    widthParam    = parameters.getRawParameterValue ("width");
    toneParam     = parameters.getRawParameterValue ("tone");
    mixParam      = parameters.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout ChorusProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    NormalisableRange<float> rateRange (0.05f, 8.0f, 0.01f);
    rateRange.setSkewForCentre (0.8f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "rate", 1 }, "Rate", rateRange, init.rate,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "depth", 1 }, "Depth", NormalisableRange<float> (0.0f, maxDepthMs, 0.01f), init.depth,
        AudioParameterFloatAttributes().withLabel ("ms")));

    NormalisableRange<float> delayRange (1.0f, maxBaseDelayMs, 0.1f);
    delayRange.setSkewForCentre (12.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "delay", 1 }, "Delay", delayRange, init.delay,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "voices", 1 }, "Voices", 1, ChorusEngine::maxVoices, (int) init.voices));

    // Négatif : retour en opposition de phase, son plus creux (façon flanger)
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (-80.0f, 80.0f, 0.1f), init.feedback,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.width,
        AudioParameterFloatAttributes().withLabel ("%")));

    NormalisableRange<float> toneRange (1000.0f, 20000.0f, 1.0f);
    toneRange.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "tone", 1 }, "Tone", toneRange, init.tone,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.mix,
        AudioParameterFloatAttributes().withLabel ("%")));

    return layout;
}

//==============================================================================
int ChorusProcessor::getNumPrograms() { return numPresets; }

const juce::String ChorusProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void ChorusProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets))
        return;

    currentProgram = index;
    const auto& p = presets[index];

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = parameters.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set ("rate", p.rate);
    set ("depth", p.depth);
    set ("delay", p.delay);
    set ("voices", p.voices);
    set ("feedback", p.feedback);
    set ("width", p.width);
    set ("tone", p.tone);
    set ("mix", p.mix);
}

//==============================================================================
bool ChorusProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void ChorusProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    engine.setVoices ((int) voicesParam->load());
    engine.prepare (sampleRate, (maxBaseDelayMs + maxDepthMs + 10.0f) * 0.001);

    toneFilter.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.setResonance (0.5f);   // pente douce, sans bosse
    toneFilter.reset();

    const auto msToSamples = (float) sampleRate * 0.001f;

    for (auto* s : { &smoothedDelay, &smoothedDepth })
        s->reset (sampleRate, 0.1);
    for (auto* s : { &smoothedFeedback, &smoothedWidth, &smoothedMix })
        s->reset (sampleRate, 0.02);

    smoothedDelay.setCurrentAndTargetValue (delayParam->load() * msToSamples);
    smoothedDepth.setCurrentAndTargetValue (depthParam->load() * msToSamples);
    smoothedFeedback.setCurrentAndTargetValue (feedbackParam->load() * 0.01f);
    smoothedWidth.setCurrentAndTargetValue (widthParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);
}

void ChorusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Réglages lus une fois par bloc
    const auto msToSamples = (float) currentSampleRate * 0.001f;
    engine.setRate (rateParam->load());
    engine.setVoices ((int) voicesParam->load());
    toneFilter.setCutoffFrequency (juce::jmin (toneParam->load(), 0.45f * (float) currentSampleRate));

    smoothedDelay.setTargetValue (delayParam->load() * msToSamples);
    smoothedDepth.setTargetValue (depthParam->load() * msToSamples);
    smoothedFeedback.setTargetValue (feedbackParam->load() * 0.01f);
    smoothedWidth.setTargetValue (widthParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        float wetL, wetR;
        engine.process (dryL, dryR, smoothedDelay.getNextValue(), smoothedDepth.getNextValue(),
                        smoothedFeedback.getNextValue(), wetL, wetR);

        wetL = toneFilter.processSample (0, wetL);
        wetR = toneFilter.processSample (1, wetR);

        // Width : 0 % = son traité au centre (mono), 100 % = stéréo complète
        const float width = smoothedWidth.getNextValue();
        const float mid   = 0.5f * (wetL + wetR);
        const float side  = 0.5f * (wetL - wetR) * width;
        wetL = mid + side;
        wetR = mid - side;

        const float mix = smoothedMix.getNextValue();

        if (right != nullptr)
        {
            left[i]  = dryL * (1.0f - mix) + wetL * mix;
            right[i] = dryR * (1.0f - mix) + wetR * mix;
        }
        else
        {
            left[i] = dryL * (1.0f - mix) + mid * mix;
        }
    }
}

juce::AudioProcessorEditor* ChorusProcessor::createEditor()
{
    return new ChorusEditor (*this);
}

void ChorusProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChorusProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
