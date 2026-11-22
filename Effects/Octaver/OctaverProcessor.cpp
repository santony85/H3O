#include "OctaverProcessor.h"
#include "OctaverEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        int mode;
        float dry, sub1, sub2, up, tone;
    };

    constexpr int an = OctaverEngine::analog, po = OctaverEngine::poly;

    constexpr Preset presets[] = {
        //  nom                           mode  direct  −1   −2   +1   tone (Hz)
        { "Classic Sub Octave",           an,   100,    80,  0,   0,   1200 },
        { "Deep Two Octaves",             an,   90,     50,  70,  0,   700 },
        { "Synth Bass (Analog)",          an,   20,     100, 40,  0,   500 },
        { "Octave Up Fuzz",               an,   70,     0,   0,   100, 4000 },
        { "Three-Octave Monster",         an,   80,     70,  50,  60,  2000 },
        { "Subtle Thickener",             an,   100,    35,  0,   0,   900 },
        { "Poly Sub",                     po,   100,    70,  0,   0,   2500 },
        { "Poly Organ",                   po,   80,     60,  30,  50,  3000 },
        { "Twelve-String (Poly Up)",      po,   100,    0,   0,   45,  6000 },
        { "Poly Bass Guitar",             po,   0,      100, 0,   0,   1500 },
        { "Poly Deep Pad",                po,   70,     60,  60,  0,   1000 },
        { "Octaves Only (Poly)",          po,   0,      70,  0,   70,  4000 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

const juce::StringArray OctaverProcessor::modeNames { "Analog", "Poly" };

OctaverProcessor::OctaverProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    modeParam = modulation.get (parameters, "mode");
    dryParam  = modulation.get (parameters, "dry");
    sub1Param = modulation.get (parameters, "sub1");
    sub2Param = modulation.get (parameters, "sub2");
    upParam   = modulation.get (parameters, "up");
    toneParam = modulation.get (parameters, "tone");
}

juce::AudioProcessorValueTreeState::ParameterLayout OctaverProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", modeNames, init.mode));

    auto level = [&layout] (const char* id, const char* name, float value)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), value,
            AudioParameterFloatAttributes().withLabel ("%")));
    };
    level ("dry",  "Dry",           init.dry);
    level ("sub1", "Octave Down",   init.sub1);
    level ("sub2", "Two Octaves Down", init.sub2);
    level ("up",   "Octave Up",     init.up);

    NormalisableRange<float> toneRange (200.0f, 8000.0f, 1.0f);
    toneRange.setSkewForCentre (1200.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "tone", 1 }, "Tone", toneRange, init.tone, AudioParameterFloatAttributes().withLabel ("Hz")));

    return layout;
}

//==============================================================================
int OctaverProcessor::getNumPrograms() { return numPresets; }

const juce::String OctaverProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void OctaverProcessor::setCurrentProgram (int index)
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

    set ("mode", (float) p.mode);
    set ("dry", p.dry);
    set ("sub1", p.sub1);
    set ("sub2", p.sub2);
    set ("up", p.up);
    set ("tone", p.tone);
}

//==============================================================================
bool OctaverProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

OctaverEngine::Settings OctaverProcessor::currentSettings() const
{
    OctaverEngine::Settings s;
    s.mode   = juce::jlimit (0, OctaverEngine::numModes - 1, (int) modeParam->load());
    s.dry    = dryParam->load() * 0.01f;
    s.sub1   = sub1Param->load() * 0.01f;
    s.sub2   = sub2Param->load() * 0.01f;
    s.up     = upParam->load() * 0.01f;
    s.toneHz = toneParam->load();
    return s;
}

void OctaverProcessor::prepareToPlay (double sampleRate, int)
{
    modulation.update();   // réglages + modulation de H3O Chain
    engine.setSettings (currentSettings());
    engine.prepare (sampleRate);
}

void OctaverProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de H3O Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    engine.setSettings (currentSettings());
    engine.process (buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr, buffer.getNumSamples());
}

juce::AudioProcessorEditor* OctaverProcessor::createEditor()
{
    return new OctaverEditor (*this);
}

void OctaverProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OctaverProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
