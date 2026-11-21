#include "UltraTapProcessor.h"
#include "UltraTapEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float preDelay, length, taps, slope, taper, spread, feedback, tone, mix;
    };

    constexpr Preset presets[] = {
        //  nom                       pre ms  len ms  taps  slope %  taper %  spread %  fb %  Tone Hz  mix %
        { "Ultra-Tap Rhythm",            0,    600,    8,      0,     -40,      60,     0,    9000,    35 },
        { "Bouncing Ball",               0,    900,   16,     70,     -80,      30,     0,   10000,    40 },
        { "Slowing Echoes",              0,   1500,   12,    -70,     -60,      80,     0,    8000,    35 },
        { "Reverse Swell Taps",          0,    700,   12,    -30,      90,      80,     0,    9000,    40 },
        { "Slapback Cluster",           40,     60,    6,      0,     -50,     100,     0,    7000,    30 },
        { "Ping-Pong Cascade",           0,   1200,    6,      0,     -30,     100,    30,    8000,    35 },
        { "Ambient Smear",              20,   1800,   24,    -40,     -60,     100,    40,    5000,    40 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

UltraTapProcessor::UltraTapProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    preDelayParam = modulation.get (parameters, "predelay");
    lengthParam   = modulation.get (parameters, "length");
    tapsParam     = modulation.get (parameters, "taps");
    slopeParam    = modulation.get (parameters, "slope");
    taperParam    = modulation.get (parameters, "taper");
    spreadParam   = modulation.get (parameters, "spread");
    feedbackParam = modulation.get (parameters, "feedback");
    toneParam     = modulation.get (parameters, "tone");
    mixParam      = modulation.get (parameters, "mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout UltraTapProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    auto percent = [&layout] (const char* id, const char* name, float lo, float hi, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%")));
    };

    NormalisableRange<float> preRange (0.0f, maxPreDelayMs, 0.1f);
    preRange.setSkewForCentre (80.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "predelay", 1 }, "Pre-Delay", preRange, init.preDelay,
        AudioParameterFloatAttributes().withLabel ("ms")));

    NormalisableRange<float> lengthRange (20.0f, maxLengthMs, 1.0f);
    lengthRange.setSkewForCentre (500.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "length", 1 }, "Length", lengthRange, init.length,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "taps", 1 }, "Taps", 1, UltraTapEngine::maxTaps, (int) init.taps));

    // Positif : les répétitions se resserrent ; négatif : elles s'espacent
    percent ("slope", "Slope", -100.0f, 100.0f, init.slope);
    // Négatif : elles s'éteignent ; positif : elles montent
    percent ("taper", "Taper", -100.0f, 100.0f, init.taper);
    percent ("spread", "Spread", 0.0f, 100.0f, init.spread);
    percent ("feedback", "Feedback", 0.0f, 90.0f, init.feedback);

    NormalisableRange<float> toneRange (1000.0f, 20000.0f, 1.0f);
    toneRange.setSkewForCentre (6000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "tone", 1 }, "Tone", toneRange, init.tone,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    percent ("mix", "Mix", 0.0f, 100.0f, init.mix);

    return layout;
}

UltraTapEngine::Settings UltraTapProcessor::currentSettings() const noexcept
{
    UltraTapEngine::Settings s;
    s.preDelayMs = preDelayParam->load();
    s.lengthMs   = lengthParam->load();
    s.taps       = (int) tapsParam->load();
    s.slope      = slopeParam->load() * 0.01f;
    s.taper      = taperParam->load() * 0.01f;
    s.spread     = spreadParam->load() * 0.01f;
    return s;
}

//==============================================================================
int UltraTapProcessor::getNumPrograms() { return numPresets; }

const juce::String UltraTapProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void UltraTapProcessor::setCurrentProgram (int index)
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

    set ("predelay", p.preDelay);
    set ("length", p.length);
    set ("taps", p.taps);
    set ("slope", p.slope);
    set ("taper", p.taper);
    set ("spread", p.spread);
    set ("feedback", p.feedback);
    set ("tone", p.tone);
    set ("mix", p.mix);
}

//==============================================================================
bool UltraTapProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void UltraTapProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    modulation.update();   // réglages + modulation de Simple Chain
    currentSampleRate = sampleRate;

    engine.prepare (sampleRate, (maxPreDelayMs + maxLengthMs) * 0.001 + 0.05);
    engine.setSettings (currentSettings());

    toneFilter.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.setResonance (0.5f);
    toneFilter.reset();

    smoothedFeedback.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);
    smoothedFeedback.setCurrentAndTargetValue (feedbackParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);
}

void UltraTapProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de Simple Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Réglages lus une fois par bloc ; le moteur fait glisser les répétitions en douceur
    engine.setSettings (currentSettings());
    toneFilter.setCutoffFrequency (juce::jmin (toneParam->load(), 0.45f * (float) currentSampleRate));
    smoothedFeedback.setTargetValue (feedbackParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        float wetL, wetR;
        engine.process (0.5f * (dryL + dryR), smoothedFeedback.getNextValue(), wetL, wetR);

        wetL = toneFilter.processSample (0, wetL);
        wetR = toneFilter.processSample (1, wetR);

        const float mix = smoothedMix.getNextValue();

        if (right != nullptr)
        {
            left[i]  = dryL * (1.0f - mix) + wetL * mix;
            right[i] = dryR * (1.0f - mix) + wetR * mix;
        }
        else
        {
            left[i] = dryL * (1.0f - mix) + 0.5f * (wetL + wetR) * mix;
        }
    }
}

juce::AudioProcessorEditor* UltraTapProcessor::createEditor()
{
    return new UltraTapEditor (*this);
}

void UltraTapProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void UltraTapProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
