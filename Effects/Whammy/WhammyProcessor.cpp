#include "WhammyProcessor.h"
#include "WhammyEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        int mode;
        float heel, toe, pedal, glide, detune, mix;
    };

    constexpr int wh = WhammyEngine::whammy, dt = WhammyEngine::detune;

    constexpr Preset presets[] = {
        //  nom                           mode talon pointe pédale glide detune mix
        { "Octave Up (Pedal)",            wh,  0,    12,    0,     60,   10,    100 },
        { "Two Octaves Up (Pedal)",       wh,  0,    24,    0,     80,   10,    100 },
        { "Octave Down (Pedal)",          wh,  0,    -12,   0,     60,   10,    100 },
        { "Dive Bomb",                    wh,  0,    -24,   0,     250,  10,    100 },
        { "Whole-Step Bend",              wh,  0,    2,     0,     90,   10,    100 },
        { "Harmony: b3 to 3",             wh,  3,    4,     0,     40,   10,    50 },
        { "Harmony: 4th to 5th",          wh,  5,    7,     0,     40,   10,    50 },
        { "Harmony: 5th to Octave",       wh,  7,    12,    0,     40,   10,    50 },
        { "Octave Up Static",             wh,  12,   12,    1,     0,    10,    100 },
        { "Octave Down + Dry",            wh,  -12,  -12,   1,     0,    10,    50 },
        { "Shallow Detune",               dt,  0,    0,     0,     0,    8,     50 },
        { "Deep Detune",                  dt,  0,    0,     0,     0,    22,    50 },
    };

    constexpr int numPresets = (int) std::size (presets);
}

const juce::StringArray WhammyProcessor::modeNames { "Whammy", "Detune" };

WhammyProcessor::WhammyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    input.attach (parameters);

    modeParam   = modulation.get (parameters, "mode");
    heelParam   = modulation.get (parameters, "heel");
    toeParam    = modulation.get (parameters, "toe");
    pedalParam  = modulation.get (parameters, "pedal");
    glideParam  = modulation.get (parameters, "glide");
    detuneParam = modulation.get (parameters, "detune");
    mixParam    = modulation.get (parameters, "mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout WhammyProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (InputMode::createParameter());

    const auto& init = presets[0];   // valeurs par défaut = premier preset

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", modeNames, init.mode));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "heel", 1 }, "Heel", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), init.heel,
        AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "toe", 1 }, "Toe", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), init.toe,
        AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "pedal", 1 }, "Pedal", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.pedal * 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    NormalisableRange<float> glideRange (0.0f, 1000.0f, 1.0f);
    glideRange.setSkewForCentre (120.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "glide", 1 }, "Glide", glideRange, init.glide, AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "detune", 1 }, "Detune", NormalisableRange<float> (0.0f, 50.0f, 0.1f), init.detune,
        AudioParameterFloatAttributes().withLabel ("ct")));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), init.mix,
        AudioParameterFloatAttributes().withLabel ("%")));

    return layout;
}

//==============================================================================
int WhammyProcessor::getNumPrograms() { return numPresets; }

const juce::String WhammyProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void WhammyProcessor::setCurrentProgram (int index)
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
    set ("heel", p.heel);
    set ("toe", p.toe);
    set ("pedal", p.pedal * 100.0f);
    set ("glide", p.glide);
    set ("detune", p.detune);
    set ("mix", p.mix);
}

//==============================================================================
bool WhammyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Entrée mono ou stéréo, sortie toujours stéréo
    return InputMode::isLayoutSupported (layouts);
}

WhammyEngine::Settings WhammyProcessor::currentSettings() const
{
    WhammyEngine::Settings s;
    s.mode        = juce::jlimit (0, WhammyEngine::numModes - 1, (int) modeParam->load());
    s.heel        = heelParam->load();
    s.toe         = toeParam->load();
    s.pedal       = pedalParam->load() * 0.01f;
    s.glideMs     = glideParam->load();
    s.detuneCents = detuneParam->load();
    s.mix         = mixParam->load() * 0.01f;
    return s;
}

void WhammyProcessor::prepareToPlay (double sampleRate, int)
{
    modulation.update();   // réglages + modulation de H3O Chain
    engine.setSettings (currentSettings());
    engine.prepare (sampleRate);
}

void WhammyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    modulation.update();   // réglages + modulation de H3O Chain
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Choix de l'entrée (Stereo / Mono L / Mono R) : ensuite G et D contiennent le signal à traiter
    input.process (buffer, numIn);

    engine.setSettings (currentSettings());
    engine.process (buffer.getWritePointer (0), numOut > 1 ? buffer.getWritePointer (1) : nullptr, buffer.getNumSamples());
    shownSemitones.store (engine.currentSemitones());
}

juce::AudioProcessorEditor* WhammyProcessor::createEditor()
{
    return new WhammyEditor (*this);
}

void WhammyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void WhammyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
            parameters.replaceState (state);
        }
}
