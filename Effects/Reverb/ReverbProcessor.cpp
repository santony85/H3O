#include "ReverbProcessor.h"
#include "ReverbEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float type, preDelay, size, decay, damping, diffusion, modulation, lowCut, highCut, width, mix;
    };

    // type : 0 = Room, 1 = Hall, 2 = Plate
    constexpr Preset presets[] = {
        //  nom              type  pre ms  taille %  s    amort. Hz  diff %  mod %  coupe-bas  coupe-haut  larg. %  mix %
        { "Concert Hall",     1,    25,    110,     2.6f,  6000,      80,     35,      80,      10000,      100,     30 },
        { "Small Room",       0,     5,     80,     0.6f,  7000,      70,     20,     100,       9000,       80,     25 },
        { "Studio Room",      0,    10,    100,     1.0f,  9000,      75,     25,      80,      12000,      100,     25 },
        { "Large Hall",       1,    40,    140,     4.5f,  5000,      85,     40,     100,       9000,      100,     30 },
        { "Bright Plate",     2,     0,     90,     1.8f, 14000,     100,     30,     150,      16000,      100,     25 },
        { "Vocal Plate",      2,    15,    100,     2.4f,  9000,     100,     30,     120,      12000,      100,     25 },
        { "Ambient Wash",     1,    60,    150,    12.0f,  4000,      90,     60,     150,       7000,      100,     45 },
        { "Dark Chamber",     0,    15,    120,     1.6f,  3000,      80,     20,      60,       5000,       90,     30 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

ReverbProcessor::ReverbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    typeParam       = parameters.getRawParameterValue ("type");
    preDelayParam   = parameters.getRawParameterValue ("predelay");
    sizeParam       = parameters.getRawParameterValue ("size");
    decayParam      = parameters.getRawParameterValue ("decay");
    dampingParam    = parameters.getRawParameterValue ("damping");
    diffusionParam  = parameters.getRawParameterValue ("diffusion");
    modulationParam = parameters.getRawParameterValue ("modulation");
    lowCutParam     = parameters.getRawParameterValue ("lowcut");
    highCutParam    = parameters.getRawParameterValue ("highcut");
    widthParam      = parameters.getRawParameterValue ("width");
    mixParam        = parameters.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout ReverbProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    auto add = [&layout] (const char* id, const char* name, float lo, float hi, float step, float centre,
                          float def, const char* unit)
    {
        NormalisableRange<float> range (lo, hi, step);
        if (centre > 0.0f)
            range.setSkewForCentre (centre);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def, AudioParameterFloatAttributes().withLabel (unit)));
    };

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "type", 1 }, "Type", StringArray { "Room", "Hall", "Plate" }, (int) init.type));

    add ("predelay",   "Pre-Delay",  0.0f, ReverbEngine::maxPreDelayMs, 0.1f, 40.0f,   init.preDelay,   "ms");
    add ("size",       "Size",       50.0f, 150.0f, 0.1f, 0.0f,                         init.size,       "%");
    add ("decay",      "Decay",      0.2f, 20.0f, 0.01f, 2.5f,                          init.decay,      "s");
    add ("damping",    "Damping",    1000.0f, 20000.0f, 1.0f, 6000.0f,                  init.damping,    "Hz");
    add ("diffusion",  "Diffusion",  0.0f, 100.0f, 0.1f, 0.0f,                          init.diffusion,  "%");
    add ("modulation", "Modulation", 0.0f, 100.0f, 0.1f, 0.0f,                          init.modulation, "%");
    add ("lowcut",     "Low Cut",    20.0f, 1000.0f, 1.0f, 150.0f,                      init.lowCut,     "Hz");
    add ("highcut",    "High Cut",   1000.0f, 20000.0f, 1.0f, 6000.0f,                  init.highCut,    "Hz");
    add ("width",      "Width",      0.0f, 100.0f, 0.1f, 0.0f,                          init.width,      "%");
    add ("mix",        "Mix",        0.0f, 100.0f, 0.1f, 0.0f,                          init.mix,        "%");

    return layout;
}

ReverbEngine::Settings ReverbProcessor::currentSettings() const noexcept
{
    ReverbEngine::Settings s;
    s.type         = (ReverbEngine::Type) juce::jlimit (0, 2, (int) typeParam->load());
    s.preDelayMs   = preDelayParam->load();
    s.size         = sizeParam->load() * 0.01f;
    s.decaySeconds = decayParam->load();
    s.dampHz       = dampingParam->load();
    s.diffusion    = diffusionParam->load() * 0.01f;
    s.modulation   = modulationParam->load() * 0.01f;
    return s;
}

//==============================================================================
int ReverbProcessor::getNumPrograms() { return numPresets; }

const juce::String ReverbProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void ReverbProcessor::setCurrentProgram (int index)
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

    set ("type", p.type);
    set ("predelay", p.preDelay);
    set ("size", p.size);
    set ("decay", p.decay);
    set ("damping", p.damping);
    set ("diffusion", p.diffusion);
    set ("modulation", p.modulation);
    set ("lowcut", p.lowCut);
    set ("highcut", p.highCut);
    set ("width", p.width);
    set ("mix", p.mix);
}

//==============================================================================
bool ReverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void ReverbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    engine.prepare (sampleRate);
    engine.setSettings (currentSettings());

    for (auto* f : { &lowCut, &highCut })
    {
        f->prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
        f->setResonance (0.6f);
        f->reset();
    }
    lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    highCut.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    smoothedWidth.reset (sampleRate, 0.02);
    smoothedMix.reset (sampleRate, 0.02);
    smoothedWidth.setCurrentAndTargetValue (widthParam->load() * 0.01f);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);
}

void ReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    // Réglages lus une fois par bloc ; le moteur fait glisser la taille en douceur
    engine.setSettings (currentSettings());
    const float nyquistSafe = 0.45f * (float) currentSampleRate;
    lowCut.setCutoffFrequency (juce::jmin (lowCutParam->load(), nyquistSafe));
    highCut.setCutoffFrequency (juce::jmin (highCutParam->load(), nyquistSafe));
    smoothedWidth.setTargetValue (widthParam->load() * 0.01f);
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        float wetL, wetR;
        engine.process (dryL, dryR, wetL, wetR);

        wetL = highCut.processSample (0, lowCut.processSample (0, wetL));
        wetR = highCut.processSample (1, lowCut.processSample (1, wetR));

        // Width : 0 % = réverbe au centre (mono), 100 % = stéréo complète
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

juce::AudioProcessorEditor* ReverbProcessor::createEditor()
{
    return new ReverbEditor (*this);
}

void ReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
