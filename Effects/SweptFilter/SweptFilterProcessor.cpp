#include "SweptFilterProcessor.h"
#include "SweptFilterEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        float source, type, shape, low, high, resonance, rate, phase, sensitivity, attack, release, direction, mix;
    };

    // source : 0 = LFO, 1 = Envelope ; type : 0 = Low Pass, 1 = Band Pass, 2 = High Pass
    // shape : 0 = Sine, 1 = Triangle, 2 = Ramp Up, 3 = Ramp Down, 4 = Square ; direction : 0 = Up, 1 = Down
    constexpr Preset presets[] = {
        //  nom                       src type shape  bas   haut  réso %  Hz     °   sens dB  att ms  rel ms  dir  mix %
        { "Classic Auto-Wah",          1,   1,   0,   350,  2200,   65,  0.5f,   0,     0,      5,    120,    0,  100 },
        { "Funk Envelope Filter",      1,   0,   0,   250,  3000,   75,  0.5f,   0,     6,      3,     90,    0,  100 },
        { "Slow Filter Sweep",         0,   0,   0,   300,  5000,   55,  0.2f,   0,     0,     10,    200,    0,  100 },
        { "Stereo Wah Swirl",          0,   1,   0,   400,  2500,   70,  0.8f, 180,     0,     10,    200,    0,  100 },
        { "Choppy Square Filter",      0,   0,   4,   400,  4000,   60,  4.0f,   0,     0,     10,    200,    0,  100 },
        { "Closing Envelope (Down)",   1,   0,   0,   300,  4000,   60,  0.5f,   0,     0,     10,    300,    1,  100 },
        { "High-Pass Riser",           0,   2,   2,   100,  2000,   40, 0.25f,   0,     0,     10,    200,    0,  100 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

SweptFilterProcessor::SweptFilterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    sourceParam      = parameters.getRawParameterValue ("source");
    typeParam        = parameters.getRawParameterValue ("type");
    shapeParam       = parameters.getRawParameterValue ("shape");
    lowParam         = parameters.getRawParameterValue ("low");
    highParam        = parameters.getRawParameterValue ("high");
    resonanceParam   = parameters.getRawParameterValue ("resonance");
    rateParam        = parameters.getRawParameterValue ("rate");
    phaseParam       = parameters.getRawParameterValue ("phase");
    sensitivityParam = parameters.getRawParameterValue ("sensitivity");
    attackParam      = parameters.getRawParameterValue ("attack");
    releaseParam     = parameters.getRawParameterValue ("release");
    directionParam   = parameters.getRawParameterValue ("direction");
    mixParam         = parameters.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout SweptFilterProcessor::createParameterLayout()
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

    auto choice = [&layout] (const char* id, const char* name, StringArray items, float def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, (int) def));
    };

    choice ("source", "Source", { "LFO", "Envelope" }, init.source);
    choice ("type", "Type", { "Low Pass", "Band Pass", "High Pass" }, init.type);
    choice ("shape", "Shape", { "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square" }, init.shape);

    add ("low",         "Low",          40.0f, 4000.0f,  1.0f,  400.0f, init.low,         "Hz");
    add ("high",        "High",        200.0f, 16000.0f, 1.0f, 2500.0f, init.high,        "Hz");
    add ("resonance",   "Resonance",     0.0f, 100.0f,   0.1f,    0.0f, init.resonance,   "%");
    add ("rate",        "Rate",         0.02f, 10.0f,    0.01f,   1.0f, init.rate,        "Hz");
    add ("phase",       "Stereo Phase",  0.0f, 180.0f,   1.0f,    0.0f, init.phase,       "deg");
    add ("sensitivity", "Sensitivity", -20.0f, 20.0f,    0.1f,    0.0f, init.sensitivity, "dB");
    add ("attack",      "Attack",        0.5f, 200.0f,   0.1f,   20.0f, init.attack,      "ms");
    add ("release",     "Release",      10.0f, 2000.0f,  1.0f,  200.0f, init.release,     "ms");

    choice ("direction", "Direction", { "Up", "Down" }, init.direction);

    add ("mix", "Mix", 0.0f, 100.0f, 0.1f, 0.0f, init.mix, "%");

    return layout;
}

SweptFilterEngine::Settings SweptFilterProcessor::currentSettings() const noexcept
{
    using E = SweptFilterEngine;
    E::Settings s;
    s.source        = (E::Source) juce::jlimit (0, 1, (int) sourceParam->load());
    s.type          = (E::Type) juce::jlimit (0, 2, (int) typeParam->load());
    s.shape         = (E::Shape) juce::jlimit (0, 4, (int) shapeParam->load());
    s.lowHz         = lowParam->load();
    s.highHz        = highParam->load();
    s.resonance     = resonanceParam->load() * 0.01f;
    s.rateHz        = rateParam->load();
    s.stereoPhase   = phaseParam->load() / 180.0f;
    s.sensitivityDb = sensitivityParam->load();
    s.attackMs      = attackParam->load();
    s.releaseMs     = releaseParam->load();
    s.downward      = directionParam->load() > 0.5f;
    return s;
}

//==============================================================================
int SweptFilterProcessor::getNumPrograms() { return numPresets; }

const juce::String SweptFilterProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void SweptFilterProcessor::setCurrentProgram (int index)
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

    set ("source", p.source);
    set ("type", p.type);
    set ("shape", p.shape);
    set ("low", p.low);
    set ("high", p.high);
    set ("resonance", p.resonance);
    set ("rate", p.rate);
    set ("phase", p.phase);
    set ("sensitivity", p.sensitivity);
    set ("attack", p.attack);
    set ("release", p.release);
    set ("direction", p.direction);
    set ("mix", p.mix);
}

//==============================================================================
bool SweptFilterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

void SweptFilterProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setSettings (currentSettings());

    smoothedMix.reset (sampleRate, 0.02);
    smoothedMix.setCurrentAndTargetValue (mixParam->load() * 0.01f);
}

void SweptFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    engine.setSettings (currentSettings());
    smoothedMix.setTargetValue (mixParam->load() * 0.01f);

    float* left  = buffer.getWritePointer (0);
    float* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : dryL;

        float wetL = dryL, wetR = dryR;
        engine.process (wetL, wetR);

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

    position[0] = engine.getPosition (0);
    position[1] = engine.getPosition (1);
}

juce::AudioProcessorEditor* SweptFilterProcessor::createEditor()
{
    return new SweptFilterEditor (*this);
}

void SweptFilterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SweptFilterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
