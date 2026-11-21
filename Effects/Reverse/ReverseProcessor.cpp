#include "ReverseProcessor.h"
#include "ReverseEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float length, shift, fine, feedback, spread, tone, mix;
    };

    constexpr Preset presets[] = {
        //  nom                          ms    st  ct  fb %  spread %  Tone Hz  mix %
        { "Reverse Guitar",              450,   0,  0,   0,     60,    12000,   55 },
        { "Backwards Solo (100% Wet)",   350,   0,  0,   0,      0,    20000,  100 },
        { "Reverse Octave Up",           400,  12,  0,   0,     80,    10000,   45 },
        { "Reverse Octave Down",         600, -12,  0,   0,     80,     8000,   45 },
        { "Reverse 5th Swell",           800,   7,  0,  35,    100,     8000,   45 },
        { "Long Reverse Wash",          1500,   0,  0,  50,    100,     6000,   50 },
        { "Reverse Micro-Detune",        300,   0, 12,  20,    100,    11000,   40 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

ReverseProcessor::ReverseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    lengthParam   = modulation.get (parameters, "length");
    shiftParam    = modulation.get (parameters, "shift");
    fineParam     = modulation.get (parameters, "fine");
    feedbackParam = modulation.get (parameters, "feedback");
    spreadParam   = modulation.get (parameters, "spread");
    toneParam     = modulation.get (parameters, "tone");
    mixParam      = modulation.get (parameters, "mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout ReverseProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    // Longueur des tranches jouées à l'envers
    NormalisableRange<float> lengthRange (minLengthMs, maxLengthMs, 1.0f);
    lengthRange.setSkewForCentre (450.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "length", 1 }, "Length", lengthRange, init.length,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "shift", 1 }, "Shift", -12, 12, (int) init.shift,
        AudioParameterIntAttributes().withLabel ("st")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "fine", 1 }, "Fine", NormalisableRange<float> (-50.0f, 50.0f, 0.1f), init.fine,
        AudioParameterFloatAttributes().withLabel ("ct")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (0.0f, 90.0f, 0.1f), init.feedback,
        AudioParameterFloatAttributes().withLabel ("%")));

    // 0 % : même son des deux côtés ; 100 % : la droite coupe ses tranches ailleurs que la gauche
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "spread", 1 }, "Spread", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.spread,
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
int ReverseProcessor::getNumPrograms() { return numPresets; }

const juce::String ReverseProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void ReverseProcessor::setCurrentProgram (int index)
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

    set ("length", p.length);
    set ("shift", p.shift);
    set ("fine", p.fine);
    set ("feedback", p.feedback);
    set ("spread", p.spread);
    set ("tone", p.tone);
    set ("mix", p.mix);
}

//==============================================================================
bool ReverseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void ReverseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    modulation.update();   // réglages + modulation de Simple Chain
    currentSampleRate = sampleRate;

    for (int c = 0; c < 2; ++c)
    {
        engines[c].prepare (sampleRate, maxLengthMs * 0.001);
        engines[c].reset (c == 0 ? 0.0f : 0.25f);   // la droite coupe un quart de tranche plus tard
        feedbackSample[c] = 0.0f;
    }

    toneFilter.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.setResonance (0.5f);
    toneFilter.reset();

    smoothedRatio.reset (sampleRate, 0.03);
    for (auto* s : { &smoothedFeedback, &smoothedSpread, &smoothedMix })
        s->reset (sampleRate, 0.02);

    smoothedRatio.setCurrentAndTargetValue (std::pow (2.0f, (shiftParam->load() * 100.0f + fineParam->load()) / 1200.0f));
    smoothedFeedback.setCurrentAndTargetValue (feedbackParam->load() * 0.01f);
    smoothedSpread.setCurrentAndTargetValue (spreadParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);
}

void ReverseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de Simple Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Réglages lus une fois par bloc ; la longueur s'applique au début de chaque tranche
    const float length = lengthParam->load() * 0.001f * (float) currentSampleRate;
    toneFilter.setCutoffFrequency (juce::jmin (toneParam->load(), 0.45f * (float) currentSampleRate));

    smoothedRatio.setTargetValue (std::pow (2.0f, (shiftParam->load() * 100.0f + fineParam->load()) / 1200.0f));
    smoothedFeedback.setTargetValue (feedbackParam->load() * 0.01f);
    smoothedSpread.setTargetValue (spreadParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        const float ratio = smoothedRatio.getNextValue();
        const float fb    = smoothedFeedback.getNextValue();

        // Le feedback renvoie le son inversé dans l'entrée : les tranches suivantes
        // le rejouent (et le transposent) encore, en s'assombrissant avec Tone
        float wetL = engines[0].process (dryL + fb * feedbackSample[0], length, ratio);
        float wetR = engines[1].process (dryR + fb * feedbackSample[1], length, ratio);

        wetL = toneFilter.processSample (0, wetL);
        wetR = toneFilter.processSample (1, wetR);
        feedbackSample[0] = wetL;
        feedbackSample[1] = wetR;

        // Spread : 0 % = son traité au centre, 100 % = gauche et droite indépendantes
        const float spread = smoothedSpread.getNextValue();
        const float mid    = 0.5f * (wetL + wetR);
        const float side   = 0.5f * (wetL - wetR) * spread;
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

juce::AudioProcessorEditor* ReverseProcessor::createEditor()
{
    return new ReverseEditor (*this);
}

void ReverseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ReverseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
