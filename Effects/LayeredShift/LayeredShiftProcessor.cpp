#include "LayeredShiftProcessor.h"
#include "LayeredShiftEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float aSemi, aCents, aDelay, aLevel;   // couche A
        float bSemi, bCents, bDelay, bLevel;   // couche B
        float detune, sweepRate, sweepDepth, feedback, tone, mix;
    };

    constexpr Preset presets[] = {
        //  nom                  A st  ct   ms   %      B st  ct   ms   %     detune  Hz    ct   fb %  Tone Hz  mix %
        { "Layered Shimmer",     12,  0,    0,  70,      7,  0, 150,  50,     12,  0.20f,  6,   30,   9000,    35 },
        { "Wide Detune Pad",      0,  0,   15, 100,      0,  0,  30, 100,     18,  0.15f,  8,    0,  12000,    50 },
        { "Octave Layers",       12,  0,   20,  80,    -12,  0,  40,  80,      8,  0.10f,  4,    0,  10000,    40 },
        { "Fifths in Motion",     7,  0,   80,  80,     -5,  0, 160,  70,     10,  0.35f, 12,   20,   9000,    40 },
        { "Seasick Sweep",        0,  0,   10, 100,      0,  0,  25, 100,      0,  1.20f, 35,   10,  12000,    50 },
        { "Rising Cascade",      12,  0,  250,  80,      7,  0, 380,  60,     10,  0.20f,  5,   60,   7000,    35 },
    };

    constexpr int numPresets = (int) std::size (presets);

    juce::String layerId (int layer, const char* name)
    {
        return juce::String (layer == 0 ? "a" : "b") + name;
    }
}

LayeredShiftProcessor::LayeredShiftProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    for (int l = 0; l < 2; ++l)
    {
        layerParams[l].semitones = parameters.getRawParameterValue (layerId (l, "semi"));
        layerParams[l].cents     = parameters.getRawParameterValue (layerId (l, "cents"));
        layerParams[l].delayMs   = parameters.getRawParameterValue (layerId (l, "delay"));
        layerParams[l].level     = parameters.getRawParameterValue (layerId (l, "level"));
    }

    detuneParam     = parameters.getRawParameterValue ("detune");
    sweepRateParam  = parameters.getRawParameterValue ("sweeprate");
    sweepDepthParam = parameters.getRawParameterValue ("sweepdepth");
    feedbackParam   = parameters.getRawParameterValue ("feedback");
    toneParam       = parameters.getRawParameterValue ("tone");
    mixParam        = parameters.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout LayeredShiftProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    auto add = [&layout] (const String& id, const String& name, float lo, float hi, float step, float centre,
                          float def, const char* unit)
    {
        NormalisableRange<float> range (lo, hi, step);
        if (centre > 0.0f)
            range.setSkewForCentre (centre);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def, AudioParameterFloatAttributes().withLabel (unit)));
    };

    for (int l = 0; l < 2; ++l)
    {
        const String layer = l == 0 ? "A " : "B ";
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { layerId (l, "semi"), 1 }, layer + "Shift", -24, 24,
            (int) (l == 0 ? init.aSemi : init.bSemi), AudioParameterIntAttributes().withLabel ("st")));
        add (layerId (l, "cents"), layer + "Fine",  -50.0f, 50.0f, 0.1f, 0.0f, l == 0 ? init.aCents : init.bCents, "ct");
        add (layerId (l, "delay"), layer + "Delay", 0.0f, (float) LayeredShiftEngine::maxDelaySeconds * 1000.0f, 0.1f, 80.0f,
             l == 0 ? init.aDelay : init.bDelay, "ms");
        add (layerId (l, "level"), layer + "Level", 0.0f, 100.0f, 0.1f, 0.0f, l == 0 ? init.aLevel : init.bLevel, "%");
    }

    add ("detune",     "Detune",      0.0f, 50.0f, 0.1f, 0.0f,  init.detune,     "ct");
    add ("sweeprate",  "Sweep Rate",  0.02f, 5.0f, 0.01f, 0.5f, init.sweepRate,  "Hz");
    add ("sweepdepth", "Sweep Depth", 0.0f, 50.0f, 0.1f, 0.0f,  init.sweepDepth, "ct");
    add ("feedback",   "Feedback",    0.0f, 90.0f, 0.1f, 0.0f,  init.feedback,   "%");
    add ("tone",       "Tone",        1000.0f, 20000.0f, 1.0f, 6000.0f, init.tone, "Hz");
    add ("mix",        "Mix",         0.0f, 100.0f, 0.1f, 0.0f, init.mix,        "%");

    return layout;
}

LayeredShiftEngine::Settings LayeredShiftProcessor::currentSettings() const noexcept
{
    LayeredShiftEngine::Settings s;
    for (int l = 0; l < 2; ++l)
    {
        auto& layer = l == 0 ? s.a : s.b;
        layer.semitones = layerParams[l].semitones->load();
        layer.cents     = layerParams[l].cents->load();
        layer.delayMs   = layerParams[l].delayMs->load();
        layer.level     = layerParams[l].level->load() * 0.01f;
    }
    s.detuneCents = detuneParam->load();
    s.sweepRateHz = sweepRateParam->load();
    s.sweepCents  = sweepDepthParam->load();
    return s;
}

//==============================================================================
int LayeredShiftProcessor::getNumPrograms() { return numPresets; }

const juce::String LayeredShiftProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void LayeredShiftProcessor::setCurrentProgram (int index)
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
    set ("detune", p.detune);
    set ("sweeprate", p.sweepRate);
    set ("sweepdepth", p.sweepDepth);
    set ("feedback", p.feedback);
    set ("tone", p.tone);
    set ("mix", p.mix);
}

//==============================================================================
bool LayeredShiftProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void LayeredShiftProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    engine.prepare (sampleRate);
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

void LayeredShiftProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Réglages lus une fois par bloc ; délais et niveaux glissent en douceur dans le moteur
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

juce::AudioProcessorEditor* LayeredShiftProcessor::createEditor()
{
    return new LayeredShiftEditor (*this);
}

void LayeredShiftProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LayeredShiftProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
