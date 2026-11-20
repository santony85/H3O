#include "HarmonizerProcessor.h"
#include "HarmonizerEditor.h"

namespace
{
    // Presets d'usine, inspirés des presets "H3000 Legacy" installés avec ton H3000 Factory
    struct Preset
    {
        const char* name;
        float aSemi, aCents, aDelay, aLevel;   // voix A (à gauche)
        float bSemi, bCents, bDelay, bLevel;   // voix B (à droite)
        float splice, feedback, spread, mix;
    };

    constexpr Preset presets[] = {
        // Désaccord léger gauche/droite, comme 231-MICROPITCHSHIFT (+9/-9 cents, 10/25 ms)
        { "Micro-Pitch Wide",        0,  9, 10, 100,    0, -9, 25, 100,   30,  0, 100, 50 },
        // Octave + quinte, comme 102-DUAL SHIFT (+1200 / +700 cents)
        { "Dual Shift: Octave + 5th", 12, 0, 10, 100,    7,  0, 10, 100,   65,  0, 100, 45 },
        // Octave au-dessus et en dessous
        { "Octave Up + Down",        12,  0, 10, 100,  -12,  0, 10, 100,   65,  0, 100, 45 },
        // Harmonie fixe : tierce majeure + quinte
        { "Major 3rd + 5th",          4,  0, 10, 100,    7,  0, 15, 100,   50,  0,  60, 45 },
        // Octaves qui montent en cascade grâce au feedback
        { "Octave Cascade",          12,  0, 80,  90,   12,  0, 100, 90,   65, 55, 100, 40 },
    };

    constexpr int numPresets = (int) std::size (presets);

    juce::String voiceId (int voice, const char* name)
    {
        return juce::String (voice == 0 ? "a" : "b") + name;
    }
}

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    for (int v = 0; v < 2; ++v)
    {
        voiceParams[v].semitones = parameters.getRawParameterValue (voiceId (v, "semi"));
        voiceParams[v].cents     = parameters.getRawParameterValue (voiceId (v, "cents"));
        voiceParams[v].delayMs   = parameters.getRawParameterValue (voiceId (v, "delay"));
        voiceParams[v].level     = parameters.getRawParameterValue (voiceId (v, "level"));
    }

    spliceParam   = parameters.getRawParameterValue ("splice");
    feedbackParam = parameters.getRawParameterValue ("feedback");
    spreadParam   = parameters.getRawParameterValue ("spread");
    mixParam      = parameters.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonizerProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    for (int v = 0; v < 2; ++v)
    {
        const String voice = v == 0 ? "A " : "B ";

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { voiceId (v, "semi"), 1 }, voice + "Shift", -24, 24,
            (int) (v == 0 ? init.aSemi : init.bSemi),
            AudioParameterIntAttributes().withLabel ("st")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { voiceId (v, "cents"), 1 }, voice + "Fine",
            NormalisableRange<float> (-50.0f, 50.0f, 0.1f), v == 0 ? init.aCents : init.bCents,
            AudioParameterFloatAttributes().withLabel ("ct")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { voiceId (v, "delay"), 1 }, voice + "Delay",
            NormalisableRange<float> (0.0f, (float) (maxVoiceDelaySeconds * 1000.0), 0.1f),
            v == 0 ? init.aDelay : init.bDelay,
            AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { voiceId (v, "level"), 1 }, voice + "Level",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), v == 0 ? init.aLevel : init.bLevel,
            AudioParameterFloatAttributes().withLabel ("%")));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "splice", 1 }, "Splice",
        NormalisableRange<float> (20.0f, (float) (maxSpliceSeconds * 1000.0), 0.1f), init.splice,
        AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "feedback", 1 }, "Feedback",
        NormalisableRange<float> (0.0f, 90.0f, 0.1f), init.feedback,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "spread", 1 }, "Spread",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.spread,
        AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.mix,
        AudioParameterFloatAttributes().withLabel ("%")));

    return layout;
}

//==============================================================================
int HarmonizerProcessor::getNumPrograms() { return numPresets; }

const juce::String HarmonizerProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void HarmonizerProcessor::setCurrentProgram (int index)
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

    set ("asemi", p.aSemi);  set ("acents", p.aCents);  set ("adelay", p.aDelay);  set ("alevel", p.aLevel);
    set ("bsemi", p.bSemi);  set ("bcents", p.bCents);  set ("bdelay", p.bDelay);  set ("blevel", p.bLevel);
    set ("splice", p.splice);
    set ("feedback", p.feedback);
    set ("spread", p.spread);
    set ("mix", p.mix);
}

//==============================================================================
bool HarmonizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void HarmonizerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    shifter.prepare (sampleRate, maxVoiceDelaySeconds, maxSpliceSeconds);
    for (auto& v : voiceState)
        PitchShifter::resetVoice (v);

    const auto sr = (float) sampleRate;

    for (int v = 0; v < 2; ++v)
    {
        smoothedDelay[v].reset (sampleRate, 0.05);
        smoothedLevel[v].reset (sampleRate, 0.02);
        smoothedDelay[v].setCurrentAndTargetValue (voiceParams[v].delayMs->load() * 0.001f * sr);
        smoothedLevel[v].setCurrentAndTargetValue (voiceParams[v].level->load() * 0.01f);
    }

    smoothedSplice.reset (sampleRate, 0.05);
    smoothedFeedback.reset (sampleRate, 0.02);
    smoothedSpread.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);

    smoothedSplice.setCurrentAndTargetValue (spliceParam->load() * 0.001f * sr);
    smoothedFeedback.setCurrentAndTargetValue (feedbackParam->load() * 0.01f);
    smoothedSpread.setCurrentAndTargetValue (spreadParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);

    feedbackSample = 0.0f;
}

void HarmonizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    const auto sr = (float) currentSampleRate;
    float ratio[2];

    for (int v = 0; v < 2; ++v)
    {
        // Demi-tons et cents → rapport de fréquence (1200 cents = une octave = ×2)
        const float totalCents = voiceParams[v].semitones->load() * 100.0f + voiceParams[v].cents->load();
        ratio[v] = std::pow (2.0f, totalCents / 1200.0f);

        smoothedDelay[v].setTargetValue (voiceParams[v].delayMs->load() * 0.001f * sr);
        smoothedLevel[v].setTargetValue (voiceParams[v].level->load() * 0.01f);
    }

    smoothedSplice.setTargetValue (spliceParam->load() * 0.001f * sr);
    smoothedFeedback.setTargetValue (feedbackParam->load() * 0.01f);
    smoothedSpread.setTargetValue (spreadParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        // Les deux voix transposent la même entrée mono (somme G + D),
        // plus le retour des voix si Feedback > 0 (cascade de transpositions)
        const float monoIn = 0.5f * (dryL + dryR);
        shifter.write (monoIn + smoothedFeedback.getNextValue() * feedbackSample);

        const float window = smoothedSplice.getNextValue();
        const float a = shifter.process (voiceState[0], ratio[0], smoothedDelay[0].getNextValue(), window)
                      * smoothedLevel[0].getNextValue();
        const float b = shifter.process (voiceState[1], ratio[1], smoothedDelay[1].getNextValue(), window)
                      * smoothedLevel[1].getNextValue();

        feedbackSample = 0.5f * (a + b);

        // Spread 100 % : voix A à gauche, B à droite. 0 % : les deux au centre.
        const float spread = smoothedSpread.getNextValue();
        const float gNear  = 0.5f + 0.5f * spread;
        const float gFar   = 0.5f - 0.5f * spread;
        const float wetL   = a * gNear + b * gFar;
        const float wetR   = b * gNear + a * gFar;

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

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor()
{
    return new HarmonizerEditor (*this);
}

void HarmonizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
